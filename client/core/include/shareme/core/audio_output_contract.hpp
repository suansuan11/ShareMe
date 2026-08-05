#pragma once

#include "shareme/core/playback_failure.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace shareme::core {

enum class AudioSampleFormat {
  signed_int16,
  float32,
};

enum class AudioInterleaving {
  interleaved,
  planar,
};

using SampleFormat = AudioSampleFormat;
using Interleaving = AudioInterleaving;

struct AudioOutputFormat {
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  AudioSampleFormat sample_format = AudioSampleFormat::signed_int16;
  AudioInterleaving interleaving = AudioInterleaving::interleaved;
};

using AudioPcmFormat = AudioOutputFormat;
using OutputFormat = AudioOutputFormat;

enum class AudioPcmValidationError {
  none,
  sample_rate_zero,
  channel_count_zero,
  invalid_sample_format,
  invalid_interleaving,
  payload_shape_invalid,
};

[[nodiscard]] constexpr bool is_valid_audio_sample_format(
    AudioSampleFormat format) noexcept {
  switch (format) {
    case AudioSampleFormat::signed_int16:
    case AudioSampleFormat::float32:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool is_valid_audio_interleaving(
    AudioInterleaving interleaving) noexcept {
  switch (interleaving) {
    case AudioInterleaving::interleaved:
    case AudioInterleaving::planar:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool is_valid_audio_output_format(
    AudioOutputFormat format) noexcept {
  return format.sample_rate != 0 && format.channel_count != 0 &&
      is_valid_audio_sample_format(format.sample_format) &&
      is_valid_audio_interleaving(format.interleaving);
}

struct AudioPcmPayloadView {
  // The span is non-owning and is valid only for the synchronous operation
  // receiving the AudioPcmBlockView.
  std::span<const std::byte> bytes{};
  std::uint32_t frame_stride_bytes = 0;
};

struct AudioPcmBlockView {
  // Counts audio frames. A frame contains one sample for every channel.
  std::uint64_t receiver_sequence = 0;
  std::uint64_t frame_count = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  AudioSampleFormat sample_format = AudioSampleFormat::signed_int16;
  AudioInterleaving interleaving = AudioInterleaving::interleaved;
  AudioPcmPayloadView payload{};
};

[[nodiscard]] constexpr AudioPcmValidationError validate_audio_pcm_block(
    AudioPcmBlockView pcm) noexcept {
  if (pcm.sample_rate == 0) {
    return AudioPcmValidationError::sample_rate_zero;
  }
  if (pcm.channel_count == 0) {
    return AudioPcmValidationError::channel_count_zero;
  }
  if (!is_valid_audio_sample_format(pcm.sample_format)) {
    return AudioPcmValidationError::invalid_sample_format;
  }
  if (!is_valid_audio_interleaving(pcm.interleaving)) {
    return AudioPcmValidationError::invalid_interleaving;
  }
  if (!pcm.payload.bytes.empty()) {
    if (pcm.frame_count == 0 || pcm.payload.frame_stride_bytes == 0 ||
        pcm.frame_count >
            std::numeric_limits<std::size_t>::max() /
                pcm.payload.frame_stride_bytes ||
        pcm.payload.bytes.size_bytes() !=
            pcm.frame_count * pcm.payload.frame_stride_bytes) {
      return AudioPcmValidationError::payload_shape_invalid;
    }
  }
  return AudioPcmValidationError::none;
}

[[nodiscard]] constexpr bool is_valid_audio_pcm_block(
    AudioPcmBlockView pcm) noexcept {
  return validate_audio_pcm_block(pcm) == AudioPcmValidationError::none;
}

// Returns the floored duration in milliseconds. Channel count is deliberately
// absent: frame_count is already measured per channel.
[[nodiscard]] constexpr std::optional<std::uint64_t> audio_duration_ms(
    std::uint64_t frame_count, std::uint32_t sample_rate) noexcept {
  if (sample_rate == 0) {
    return std::nullopt;
  }

  const auto whole_seconds = frame_count / sample_rate;
  const auto remainder = frame_count % sample_rate;
  if (whole_seconds > std::numeric_limits<std::uint64_t>::max() / 1'000U) {
    return std::nullopt;
  }

  const auto whole_ms = whole_seconds * 1'000U;
  const auto fractional_ms =
      (remainder * 1'000U) / static_cast<std::uint64_t>(sample_rate);
  if (whole_ms > std::numeric_limits<std::uint64_t>::max() - fractional_ms) {
    return std::nullopt;
  }
  return whole_ms + fractional_ms;
}

[[nodiscard]] constexpr std::optional<std::uint64_t> audio_duration_ms(
    AudioPcmBlockView pcm) noexcept {
  if (!is_valid_audio_pcm_block(pcm)) {
    return std::nullopt;
  }
  return audio_duration_ms(pcm.frame_count, pcm.sample_rate);
}

enum class OpenStatus {
  opened,
  failed,
};

struct OpenResult {
  OpenStatus status = OpenStatus::failed;
  std::optional<PlaybackCategory> failure_category = std::nullopt;
};

using AudioOpenStatus = OpenStatus;
using AudioOpenResult = OpenResult;

[[nodiscard]] constexpr bool is_valid_open_result(
    OpenResult result) noexcept {
  if (result.status == OpenStatus::opened) {
    return !result.failure_category.has_value();
  }
  return result.failure_category.has_value();
}

enum class WriteStatus {
  accepted,
  would_block,
  failed,
};

struct WriteResult {
  WriteStatus status = WriteStatus::failed;
  std::uint64_t accepted_frames = 0;
  std::optional<PlaybackCategory> failure_category = std::nullopt;
};

[[nodiscard]] constexpr bool is_valid_write_result(
    WriteResult result) noexcept {
  switch (result.status) {
    case WriteStatus::accepted:
      return result.accepted_frames > 0 && !result.failure_category.has_value();
    case WriteStatus::would_block:
      return result.accepted_frames == 0 &&
          !result.failure_category.has_value();
    case WriteStatus::failed:
      return result.accepted_frames == 0 &&
          result.failure_category.has_value();
  }
  return false;
}

struct AudioDeviceSnapshot {
  std::uint64_t device_instance_id = 0;
  std::uint64_t snapshot_sequence = 0;
  std::uint64_t accepted_frames_total = 0;
  std::uint64_t device_consumed_frames_total = 0;
  std::uint64_t device_queue_frames = 0;
  std::optional<std::uint64_t> output_latency_frames = std::nullopt;
  std::uint64_t underrun_count = 0;
  std::uint64_t discontinuity_count = 0;
  std::optional<PlaybackCategory> last_discontinuity_reason = std::nullopt;
  bool active = false;
};

struct FinalDeviceSnapshot {
  std::uint64_t device_instance_id = 0;
  std::uint64_t snapshot_sequence = 0;
  std::uint64_t accepted_frames_total = 0;
  std::uint64_t device_consumed_frames_total = 0;
  std::uint64_t device_queue_frames = 0;
  std::optional<std::uint64_t> output_latency_frames = std::nullopt;
  std::uint64_t underrun_count = 0;
  std::uint64_t discontinuity_count = 0;
  std::optional<PlaybackCategory> last_discontinuity_reason = std::nullopt;
  bool active = false;
  bool quiesced = false;
  bool exact_consumption = false;
};

[[nodiscard]] constexpr bool is_valid_audio_device_snapshot(
    std::uint64_t accepted_frames_total,
    std::uint64_t device_consumed_frames_total,
    std::uint64_t device_queue_frames) noexcept {
  return device_consumed_frames_total <= accepted_frames_total &&
      device_queue_frames <=
          accepted_frames_total - device_consumed_frames_total;
}

[[nodiscard]] constexpr bool is_valid_final_device_snapshot(
    FinalDeviceSnapshot snapshot) noexcept {
  return snapshot.quiesced && is_valid_audio_device_snapshot(
                                 snapshot.accepted_frames_total,
                                 snapshot.device_consumed_frames_total,
                                 snapshot.device_queue_frames);
}

class AudioOutputDevice {
 public:
  virtual ~AudioOutputDevice() = default;

  virtual WriteResult try_write(AudioPcmBlockView pcm) = 0;
  virtual AudioDeviceSnapshot snapshot() = 0;
  virtual FinalDeviceSnapshot quiesce_and_snapshot() = 0;
  // Open configures the backend; start begins consumption only after open.
  virtual OpenResult open(AudioOutputFormat format) = 0;
  virtual bool start() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
};

}  // namespace shareme::core
