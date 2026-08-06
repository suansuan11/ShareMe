#include "qt_audio_output_device.hpp"

#include <QAudio>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

using shareme::core::AudioInterleaving;
using shareme::core::AudioOutputFormat;
using shareme::core::AudioSampleFormat;
using shareme::core::PlaybackCategory;

constexpr AudioOutputFormat kQtMovieAudioFormat{
    .sample_rate = 48'000,
    .channel_count = 2,
    .sample_format = AudioSampleFormat::signed_int16,
    .interleaving = AudioInterleaving::interleaved,
};

// Qt 6.11 deprecates the UnderrunError symbol because that backend no longer
// emits it, but keeps the enum value for older multimedia backends.
constexpr int kQtAudioUnderrunError = 3;

std::atomic<std::uint64_t> next_device_instance_id{1};

[[nodiscard]] bool is_qt_movie_audio_format(
    AudioOutputFormat format) noexcept {
  return format.sample_rate == kQtMovieAudioFormat.sample_rate &&
      format.channel_count == kQtMovieAudioFormat.channel_count &&
      format.sample_format == kQtMovieAudioFormat.sample_format &&
      format.interleaving == kQtMovieAudioFormat.interleaving;
}

[[nodiscard]] std::uint64_t allocate_device_instance_id() noexcept {
  auto id = next_device_instance_id.fetch_add(1, std::memory_order_relaxed);
  if (id == 0) {
    id = next_device_instance_id.fetch_add(1, std::memory_order_relaxed);
  }
  return id;
}

}  // namespace

QtAudioOutputDevice::QtAudioOutputDevice(QAudioDevice device)
    : device_{std::move(device)}, device_instance_id_{
                                      allocate_device_instance_id()} {}

QtAudioOutputDevice::QtAudioOutputDevice()
    : QtAudioOutputDevice{QMediaDevices::defaultAudioOutput()} {}

QtAudioOutputDevice::~QtAudioOutputDevice() {
  stop();
}

shareme::core::OpenResult QtAudioOutputDevice::open(
    AudioOutputFormat format) {
  stop();
  sink_.reset();
  opened_ = false;
  QAudioFormat qt_format;
  qt_format.setSampleRate(static_cast<int>(format.sample_rate));
  qt_format.setChannelCount(static_cast<int>(format.channel_count));
  qt_format.setSampleFormat(QAudioFormat::Int16);
  if (!is_qt_movie_audio_format(format)) {
    return shareme::core::OpenResult{
        .status = shareme::core::OpenStatus::failed,
        .failure_category = PlaybackCategory::audio_format_change,
    };
  }
  if (device_.isNull()) {
    return shareme::core::OpenResult{
        .status = shareme::core::OpenStatus::failed,
        .failure_category = PlaybackCategory::audio_output_device_lost,
    };
  }
  if (!device_.isFormatSupported(qt_format)) {
    return shareme::core::OpenResult{
        .status = shareme::core::OpenStatus::failed,
        .failure_category = PlaybackCategory::audio_format_change,
    };
  }

  sink_ = std::make_unique<QAudioSink>(device_, qt_format);
  sink_->setBufferSize(38'400);
  format_ = format;
  reset_runtime_state();
  opened_ = true;
  return shareme::core::OpenResult{
      .status = shareme::core::OpenStatus::opened,
      .failure_category = std::nullopt,
  };
}

bool QtAudioOutputDevice::start() {
  if (!opened_ || sink_ == nullptr) {
    return false;
  }
  controlled_suspension_ = false;
  controlled_suspension_pending_ = false;
  if (sink_->state() == QAudio::SuspendedState) {
    sink_->resume();
  } else {
    io_device_ = sink_->start();
  }
  refresh_sink_state();
  if (sink_->error() == QAudio::NoError) {
    const auto state = sink_->state();
    active_ = io_device_ != nullptr && state == QAudio::ActiveState;
  }
  return active_;
}

shareme::core::WriteResult QtAudioOutputDevice::try_write(
    shareme::core::AudioPcmBlockView pcm) {
  refresh_sink_state();
  if (!active_ || io_device_ == nullptr || sink_ == nullptr) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_output_device_lost,
    };
  }
  if (!shareme::core::is_valid_audio_pcm_block(pcm) ||
      !is_qt_movie_audio_format(AudioOutputFormat{
          .sample_rate = pcm.sample_rate,
          .channel_count = pcm.channel_count,
          .sample_format = pcm.sample_format,
          .interleaving = pcm.interleaving,
      }) ||
      pcm.payload.frame_stride_bytes != 4 ||
      pcm.frame_count > std::numeric_limits<std::size_t>::max() / 4) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_format_change,
    };
  }

  const auto bytes_free = sink_->bytesFree();
  if (bytes_free <= 0) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::would_block,
        .accepted_frames = 0,
        .failure_category = std::nullopt,
    };
  }
  const auto frame_bytes = std::uint64_t{4};
  if (pcm.frame_count > std::numeric_limits<std::uint64_t>::max() /
          frame_bytes) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_format_change,
    };
  }
  const auto requested_bytes = pcm.frame_count * frame_bytes;
  const auto writable_bytes = std::min<std::uint64_t>(
      requested_bytes, static_cast<std::uint64_t>(bytes_free));
  const auto writable_frames = writable_bytes / frame_bytes;
  if (writable_frames == 0 ||
      writable_frames >
          static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()) /
              frame_bytes) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::would_block,
        .accepted_frames = 0,
        .failure_category = std::nullopt,
    };
  }

  const auto write_bytes = writable_frames * frame_bytes;
  const auto written = io_device_->write(
      reinterpret_cast<const char*>(pcm.payload.bytes.data()),
      static_cast<qint64>(write_bytes));
  if (written < 0) {
    active_ = false;
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_output_failure,
    };
  }
  if (written == 0) {
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::would_block,
        .accepted_frames = 0,
        .failure_category = std::nullopt,
    };
  }
  if (written % static_cast<qint64>(frame_bytes) != 0) {
    ++discontinuity_count_;
    active_ = false;
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_output_failure,
    };
  }

  const auto accepted_frames =
      static_cast<std::uint64_t>(written / static_cast<qint64>(frame_bytes));
  if (accepted_frames >
      std::numeric_limits<std::uint64_t>::max() - accepted_frames_total_) {
    accepted_frames_total_ = std::numeric_limits<std::uint64_t>::max();
    active_ = false;
    if (discontinuity_count_ != std::numeric_limits<std::uint64_t>::max()) {
      ++discontinuity_count_;
    }
    return shareme::core::WriteResult{
        .status = shareme::core::WriteStatus::failed,
        .accepted_frames = 0,
        .failure_category = PlaybackCategory::audio_output_failure,
    };
  }
  accepted_frames_total_ += accepted_frames;
  return shareme::core::WriteResult{
      .status = shareme::core::WriteStatus::accepted,
      .accepted_frames = accepted_frames,
      .failure_category = std::nullopt,
  };
}

shareme::core::AudioDeviceSnapshot QtAudioOutputDevice::snapshot() {
  return read_snapshot();
}

shareme::core::FinalDeviceSnapshot
QtAudioOutputDevice::quiesce_and_snapshot() {
  if (sink_ != nullptr && active_ &&
      sink_->state() == QAudio::ActiveState) {
    controlled_suspension_pending_ = true;
    sink_->suspend();
  } else if (!controlled_suspension_) {
    controlled_suspension_pending_ = false;
    controlled_suspension_ = false;
  }
  active_ = false;
  const auto ordinary = read_snapshot();
  return shareme::core::FinalDeviceSnapshot{
      .device_instance_id = ordinary.device_instance_id,
      .snapshot_sequence = ordinary.snapshot_sequence,
      .accepted_frames_total = ordinary.accepted_frames_total,
      .device_consumed_frames_total = ordinary.device_consumed_frames_total,
      .device_queue_frames = ordinary.device_queue_frames,
      .output_latency_frames = ordinary.output_latency_frames,
      .underrun_count = ordinary.underrun_count,
      .discontinuity_count = ordinary.discontinuity_count,
      .last_discontinuity_reason = ordinary.last_discontinuity_reason,
      .active = false,
      .quiesced = true,
      .exact_consumption = has_trusted_device_facts(),
  };
}

void QtAudioOutputDevice::pause() {
  controlled_suspension_ = false;
  controlled_suspension_pending_ = sink_ != nullptr && active_ &&
      sink_->state() == QAudio::ActiveState;
  if (controlled_suspension_pending_) {
    sink_->suspend();
  }
  active_ = false;
}

void QtAudioOutputDevice::stop() {
  if (sink_ != nullptr) {
    sink_->stop();
  }
  io_device_ = nullptr;
  active_ = false;
  controlled_suspension_ = false;
  controlled_suspension_pending_ = false;
  processed_duration_valid_ = false;
  queue_facts_valid_ = false;
  opened_ = false;
}

shareme::core::AudioDeviceSnapshot QtAudioOutputDevice::read_snapshot() {
  refresh_sink_state();
  std::uint64_t consumed = device_consumed_frames_total_;
  if (active_ || controlled_suspension_) {
    consumed = processed_frames();
  } else {
    processed_duration_valid_ = false;
    queue_facts_valid_ = false;
  }
  if (consumed > accepted_frames_total_) {
    consumed = accepted_frames_total_;
  }
  device_consumed_frames_total_ = consumed;

  std::uint64_t queue_frames = 0;
  queue_facts_valid_ = false;
  const bool facts_allowed = !controlled_suspension_pending_ &&
      (active_ || controlled_suspension_) && processed_duration_valid_;
  if (sink_ != nullptr && facts_allowed) {
    const auto buffer_size = sink_->bufferSize();
    const auto bytes_free = sink_->bytesFree();
    if (buffer_size > 0 && bytes_free >= 0 && bytes_free <= buffer_size) {
      const auto occupied_bytes = buffer_size - bytes_free;
      queue_frames = static_cast<std::uint64_t>(occupied_bytes / 4);
      const auto maximum_queue = accepted_frames_total_ - consumed;
      queue_frames = std::min(queue_frames, maximum_queue);
      queue_facts_valid_ = true;
    }
  }

  if (snapshot_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
    ++snapshot_sequence_;
  } else {
    active_ = false;
    if (discontinuity_count_ != std::numeric_limits<std::uint64_t>::max()) {
      ++discontinuity_count_;
    }
  }
  return shareme::core::AudioDeviceSnapshot{
      .device_instance_id = device_instance_id_,
      .snapshot_sequence = snapshot_sequence_,
      .accepted_frames_total = accepted_frames_total_,
      .device_consumed_frames_total = device_consumed_frames_total_,
      .device_queue_frames = queue_frames,
      // bytesFree() describes queued bytes, not trusted acoustic latency.
      .output_latency_frames = std::nullopt,
      .underrun_count = underrun_count_,
      .discontinuity_count = discontinuity_count_,
      .last_discontinuity_reason = discontinuity_count_ == 0
          ? std::nullopt
          : std::optional<PlaybackCategory>{
                PlaybackCategory::audio_output_failure},
      .active = active_,
  };
}

std::uint64_t QtAudioOutputDevice::processed_frames() noexcept {
  if (sink_ == nullptr) {
    return device_consumed_frames_total_;
  }
  const auto processed_usecs = sink_->processedUSecs();
  if (processed_usecs < 0 ||
      static_cast<std::uint64_t>(processed_usecs) < last_processed_usecs_) {
    processed_duration_valid_ = false;
    active_ = false;
    return device_consumed_frames_total_;
  }
  last_processed_usecs_ = static_cast<std::uint64_t>(processed_usecs);

  const auto whole_seconds = last_processed_usecs_ / 1'000'000U;
  const auto remainder_usecs = last_processed_usecs_ % 1'000'000U;
  const auto sample_rate = static_cast<std::uint64_t>(format_.sample_rate);
  if (sample_rate == 0) {
    processed_duration_valid_ = false;
    active_ = false;
    return device_consumed_frames_total_;
  }
  if (whole_seconds > std::numeric_limits<std::uint64_t>::max() / sample_rate) {
    processed_duration_valid_ = false;
    active_ = false;
    return std::numeric_limits<std::uint64_t>::max();
  }
  auto frames = whole_seconds * sample_rate;
  const auto fractional_frames = remainder_usecs * sample_rate / 1'000'000U;
  if (fractional_frames > std::numeric_limits<std::uint64_t>::max() - frames) {
    processed_duration_valid_ = false;
    active_ = false;
    return std::numeric_limits<std::uint64_t>::max();
  }
  frames += fractional_frames;
  processed_duration_valid_ = true;
  return frames;
}

bool QtAudioOutputDevice::has_trusted_device_facts() const noexcept {
  return processed_duration_valid_ && queue_facts_valid_;
}

void QtAudioOutputDevice::refresh_sink_state() noexcept {
  if (sink_ == nullptr) {
    active_ = false;
    controlled_suspension_ = false;
    controlled_suspension_pending_ = false;
    return;
  }

  const auto error = sink_->error();
  const auto error_value = static_cast<int>(error);
  if (error != QAudio::NoError) {
    if (error_value == kQtAudioUnderrunError &&
        last_audio_error_ != error_value) {
      if (underrun_count_ != std::numeric_limits<std::uint64_t>::max()) {
        ++underrun_count_;
      }
      if (discontinuity_count_ != std::numeric_limits<std::uint64_t>::max()) {
        ++discontinuity_count_;
      }
    }
    last_audio_error_ = error_value;
    active_ = false;
    controlled_suspension_ = false;
    controlled_suspension_pending_ = false;
    processed_duration_valid_ = false;
    queue_facts_valid_ = false;
    return;
  }
  last_audio_error_ = static_cast<int>(QAudio::NoError);

  const auto state = sink_->state();
  if (controlled_suspension_pending_ || controlled_suspension_) {
    if (state == QAudio::SuspendedState) {
      controlled_suspension_ = true;
      controlled_suspension_pending_ = false;
      active_ = false;
      return;
    }
    controlled_suspension_ = false;
    controlled_suspension_pending_ = false;
    active_ = false;
    processed_duration_valid_ = false;
    queue_facts_valid_ = false;
    return;
  }
  if (state == QAudio::SuspendedState) {
    active_ = false;
    processed_duration_valid_ = false;
    queue_facts_valid_ = false;
    return;
  }
  const bool running = io_device_ != nullptr &&
      state == QAudio::ActiveState;
  if (!running) {
    active_ = false;
    processed_duration_valid_ = false;
    queue_facts_valid_ = false;
  }
}

void QtAudioOutputDevice::reset_runtime_state() noexcept {
  snapshot_sequence_ = 0;
  accepted_frames_total_ = 0;
  device_consumed_frames_total_ = 0;
  last_processed_usecs_ = 0;
  underrun_count_ = 0;
  discontinuity_count_ = 0;
  last_audio_error_ = -1;
  io_device_ = nullptr;
  active_ = false;
  controlled_suspension_ = false;
  controlled_suspension_pending_ = false;
  processed_duration_valid_ = false;
  queue_facts_valid_ = false;
  device_instance_id_ = allocate_device_instance_id();
}
