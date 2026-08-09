#include "video_preview_adapter.hpp"

#include <QImage>
#include <QMetaObject>
#include <QAbstractVideoBuffer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

#include "api/video/i420_buffer.h"
#include "libyuv/convert_argb.h"
#include "rtc_base/time_utils.h"

namespace shareme::tools {
namespace {

struct PreparedFrame {
  QVideoFrame frame;
  PreviewPath path{PreviewPath::rejected};
  std::size_t owned_bytes{0};
  std::uint32_t rtp_timestamp{0};
  std::int64_t capture_timestamp_us{0};
};

class I420VideoBuffer final : public QAbstractVideoBuffer {
public:
  I420VideoBuffer(
      const webrtc::scoped_refptr<webrtc::I420BufferInterface>& source,
                  QVideoFrameFormat format)
      : source_(source), format_(std::move(format)),
        strides_{source_->StrideY(), source_->StrideU(), source_->StrideV()},
        heights_{source_->height(), (source_->height() + 1) / 2,
                 (source_->height() + 1) / 2} {}

  MapData map(QVideoFrame::MapMode) override {
    MapData data;
    data.planeCount = 3;
    const std::uint8_t* planes[] = {
        source_->DataY(), source_->DataU(), source_->DataV()};
    for (int plane = 0; plane < 3; ++plane) {
      data.bytesPerLine[plane] = strides_[plane];
      data.data[plane] = const_cast<uchar*>(planes[plane]);
      data.dataSize[plane] = strides_[plane] * heights_[plane];
    }
    return data;
  }

  QVideoFrameFormat format() const override { return format_; }

private:
  webrtc::scoped_refptr<webrtc::I420BufferInterface> source_;
  QVideoFrameFormat format_;
  int strides_[3];
  int heights_[3];
};

PreparedFrame make_planar_frame(
    const webrtc::scoped_refptr<webrtc::I420BufferInterface>& buffer,
    std::atomic<std::uint64_t>& mapping_failures,
    std::atomic<std::uint64_t>& fallback_copies) {
  const auto width = buffer->width();
  const auto height = buffer->height();
  try {
    const QVideoFrameFormat format{
        QSize(width, height), QVideoFrameFormat::Format_YUV420P};
    QVideoFrame planar{new I420VideoBuffer(buffer, format), format};
    const auto chroma_height = (height + 1) / 2;
    const auto owned_bytes =
        static_cast<std::size_t>(buffer->StrideY()) *
            static_cast<std::size_t>(height) +
        static_cast<std::size_t>(buffer->StrideU()) *
            static_cast<std::size_t>(chroma_height) +
        static_cast<std::size_t>(buffer->StrideV()) *
            static_cast<std::size_t>(chroma_height);
    return {std::move(planar), PreviewPath::planar_yuv, owned_bytes};
  } catch (const std::bad_alloc&) {
    mapping_failures.fetch_add(1, std::memory_order_relaxed);
  }

  QImage image(width, height, QImage::Format_ARGB32);
  if (image.isNull() ||
      libyuv::I420ToARGB(
          buffer->DataY(), buffer->StrideY(), buffer->DataU(),
          buffer->StrideU(), buffer->DataV(), buffer->StrideV(), image.bits(),
          image.bytesPerLine(),
          width, height) != 0) {
    return {};
  }
  fallback_copies.fetch_add(1, std::memory_order_relaxed);
  return {QVideoFrame(image.copy()), PreviewPath::argb_fallback,
          static_cast<std::size_t>(image.sizeInBytes())};
}

}  // namespace

struct VideoPreviewState {
  explicit VideoPreviewState(QObject* target) : queue_target(target) {}

  static constexpr std::size_t kDelaySampleCapacity = 64;

  mutable std::mutex mutex;
  QPointer<QObject> queue_target;
  QPointer<QVideoSink> sink;
  std::function<void(std::uint32_t)> submitted_callback;
  std::optional<PreparedFrame> pending_frame;
  bool drain_scheduled{false};
  bool closed{false};
  bool destroyed{false};
  std::atomic<std::uint64_t> submissions{0};
  std::atomic<std::uint64_t> coalesced{0};
  std::atomic<std::uint64_t> remote_callbacks{0};
  std::atomic<std::uint64_t> fallback_copies{0};
  std::atomic<std::uint64_t> mapping_failures{0};
  std::atomic<std::uint64_t> max_pending_depth{0};
  std::atomic<std::uint64_t> pending_callback_bytes{0};
  std::atomic<std::uint64_t> peak_pending_callback_bytes{0};
  std::array<std::uint64_t, kDelaySampleCapacity> delay_samples{};
  std::size_t delay_sample_count{0};
  std::size_t delay_sample_next{0};
  std::uint64_t presentation_callback_delay_max{0};
  std::uint64_t presentation_epoch{0};
  std::uint64_t presentation_recovery_count{0};
};

void update_peak(
    std::atomic<std::uint64_t>& peak,
    std::uint64_t value) noexcept {
  auto observed = peak.load(std::memory_order_relaxed);
  while (observed < value &&
         !peak.compare_exchange_weak(
             observed, value, std::memory_order_relaxed)) {
  }
}

void record_presentation_delay(VideoPreviewState& state,
                               std::int64_t frame_timestamp_us) {
  const auto now_us = webrtc::TimeMicros();
  if (frame_timestamp_us < 0 || now_us < frame_timestamp_us)
    return;
  const auto delay_ms = static_cast<std::uint64_t>(
      (now_us - frame_timestamp_us) / 1'000);
  state.delay_samples[state.delay_sample_next] = delay_ms;
  state.delay_sample_next =
      (state.delay_sample_next + 1) % state.kDelaySampleCapacity;
  state.delay_sample_count = std::min(
      state.delay_sample_count + 1, state.kDelaySampleCapacity);
  state.presentation_callback_delay_max =
      std::max(state.presentation_callback_delay_max, delay_ms);
}

void clear_pending_frame(VideoPreviewState& state) {
  state.pending_frame.reset();
  state.drain_scheduled = false;
  state.pending_callback_bytes.store(0, std::memory_order_release);
}

void drain_pending_frame(const std::shared_ptr<VideoPreviewState>& state);

bool schedule_drain(const std::shared_ptr<VideoPreviewState>& state) {
  QPointer<QObject> target;
  {
    std::lock_guard lock(state->mutex);
    target = state->queue_target;
  }
  if (target.isNull() || QMetaObject::invokeMethod(
                             target,
                             [state] { drain_pending_frame(state); },
                             Qt::QueuedConnection)) {
    return !target.isNull();
  }

  std::lock_guard lock(state->mutex);
  clear_pending_frame(*state);
  return false;
}

void drain_pending_frame(const std::shared_ptr<VideoPreviewState>& state) {
  std::optional<PreparedFrame> prepared;
  QPointer<QVideoSink> sink;
  std::function<void(std::uint32_t)> submitted_callback;
  {
    std::lock_guard lock(state->mutex);
    if (state->closed) {
      clear_pending_frame(*state);
      return;
    }
    if (state->pending_frame.has_value()) {
      prepared = std::move(state->pending_frame);
      state->pending_frame.reset();
      state->pending_callback_bytes.store(0, std::memory_order_release);
    }
    sink = state->sink;
    submitted_callback = state->submitted_callback;
  }

  if (prepared.has_value() && !sink.isNull()) {
    const auto rtp_timestamp = prepared->rtp_timestamp;
    const auto capture_timestamp_us = prepared->capture_timestamp_us;
    auto submitted = std::move(prepared->frame);
    submitted.setStartTime(capture_timestamp_us);
    sink->setVideoFrame(std::move(submitted));
    state->submissions.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard lock(state->mutex);
      record_presentation_delay(*state, capture_timestamp_us);
    }
    if (submitted_callback)
      submitted_callback(rtp_timestamp);
  }

  bool schedule_next = false;
  {
    std::lock_guard lock(state->mutex);
    if (state->closed) {
      clear_pending_frame(*state);
    } else if (state->pending_frame.has_value()) {
      schedule_next = true;
    } else {
      state->drain_scheduled = false;
    }
  }
  if (schedule_next && !schedule_drain(state)) {
    std::lock_guard lock(state->mutex);
    clear_pending_frame(*state);
  }
}

VideoPreviewAdapter::VideoPreviewAdapter(QObject* queue_target)
    : state_(std::make_shared<VideoPreviewState>(queue_target)) {}

VideoPreviewAdapter::~VideoPreviewAdapter() {
  std::lock_guard lock(state_->mutex);
  state_->closed = true;
  state_->destroyed = true;
  state_->pending_frame.reset();
  state_->submitted_callback = {};
  state_->queue_target = nullptr;
  state_->sink = nullptr;
  state_->pending_callback_bytes.store(0, std::memory_order_release);
}

void VideoPreviewAdapter::set_sink(QVideoSink* sink) noexcept {
  std::lock_guard lock(state_->mutex);
  state_->sink = sink;
}

void VideoPreviewAdapter::set_submitted_callback(
    std::function<void(std::uint32_t)> callback) {
  std::lock_guard lock(state_->mutex);
  state_->submitted_callback = std::move(callback);
}

void VideoPreviewAdapter::close_ingress() noexcept {
  std::lock_guard lock(state_->mutex);
  if (state_->destroyed)
    return;
  state_->closed = true;
  state_->sink = nullptr;
  clear_pending_frame(*state_);
}

void VideoPreviewAdapter::reopen_ingress(QVideoSink* sink) noexcept {
  std::lock_guard lock(state_->mutex);
  if (state_->destroyed || sink == nullptr)
    return;
  clear_pending_frame(*state_);
  state_->sink = sink;
  state_->closed = false;
  ++state_->presentation_epoch;
  ++state_->presentation_recovery_count;
}

VideoPreviewResult VideoPreviewAdapter::submit(const webrtc::VideoFrame& source) {
  VideoPreviewResult result{.rtp_timestamp = source.rtp_timestamp()};
  state_->remote_callbacks.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard lock(state_->mutex);
    if (state_->closed || state_->sink.isNull() ||
        state_->queue_target.isNull()) {
      result.path = PreviewPath::no_sink;
      return result;
    }
  }

  const auto source_buffer = source.video_frame_buffer();
  const auto i420 = source_buffer ? source_buffer->ToI420() : nullptr;
  if (!i420) {
    return result;
  }
  auto prepared = make_planar_frame(
      i420, state_->mapping_failures, state_->fallback_copies);
  if (!prepared.frame.isValid()) {
    return result;
  }
  prepared.rtp_timestamp = result.rtp_timestamp;
  prepared.capture_timestamp_us = source.timestamp_us();
  result.submitted = true;
  result.path = prepared.path;
  bool schedule = false;
  {
    std::lock_guard lock(state_->mutex);
    if (state_->closed) {
      result.submitted = false;
      result.path = PreviewPath::rejected;
      return result;
    }
    if (state_->pending_frame.has_value()) {
      state_->coalesced.fetch_add(1, std::memory_order_relaxed);
      result.path = PreviewPath::coalesced;
    }
    state_->pending_frame = std::move(prepared);
    state_->pending_callback_bytes.store(
        static_cast<std::uint64_t>(state_->pending_frame->owned_bytes),
        std::memory_order_release);
    update_peak(
        state_->peak_pending_callback_bytes,
        static_cast<std::uint64_t>(state_->pending_frame->owned_bytes));
    state_->max_pending_depth.store(1, std::memory_order_relaxed);
    if (!state_->drain_scheduled) {
      state_->drain_scheduled = true;
      schedule = true;
    }
  }
  if (schedule && !schedule_drain(state_)) {
    result.submitted = false;
    result.path = PreviewPath::rejected;
  }
  return result;
}

VideoPreviewCounters VideoPreviewAdapter::counters() const noexcept {
  std::uint64_t delay_p95 = 0;
  std::uint64_t delay_max = 0;
  std::uint64_t pending_callbacks = 0;
  {
    std::lock_guard lock(state_->mutex);
    pending_callbacks = state_->drain_scheduled ? 1U : 0U;
    if (state_->delay_sample_count > 0) {
      std::array<std::uint64_t, VideoPreviewState::kDelaySampleCapacity>
          samples{};
      std::copy_n(state_->delay_samples.begin(), state_->delay_sample_count,
                  samples.begin());
      std::sort(samples.begin(), samples.begin() + state_->delay_sample_count);
      const auto index = (state_->delay_sample_count * 95 + 99) / 100 - 1;
      delay_p95 = samples[index];
      delay_max = state_->presentation_callback_delay_max;
    }
  }
  return {
      .submissions = state_->submissions.load(std::memory_order_relaxed),
      .coalesced = state_->coalesced.load(std::memory_order_relaxed),
      .remote_callbacks =
          state_->remote_callbacks.load(std::memory_order_relaxed),
      .sink_submissions =
          state_->submissions.load(std::memory_order_relaxed),
      .presentation_coalesced =
          state_->coalesced.load(std::memory_order_relaxed),
      .presentation_callback_delay_p95 = delay_p95,
      .presentation_callback_delay_max = delay_max,
      .fallback_copies =
          state_->fallback_copies.load(std::memory_order_relaxed),
      .mapping_failures =
          state_->mapping_failures.load(std::memory_order_relaxed),
      .max_pending_depth =
          state_->max_pending_depth.load(std::memory_order_relaxed),
      .pending_callbacks =
          pending_callbacks,
      .pending_callback_bytes =
          state_->pending_callback_bytes.load(std::memory_order_acquire),
      .peak_pending_callback_bytes =
          state_->peak_pending_callback_bytes.load(std::memory_order_relaxed),
      .presentation_epoch = state_->presentation_epoch,
      .presentation_recovery_count = state_->presentation_recovery_count,
  };
}

}  // namespace shareme::tools
