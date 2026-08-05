#pragma once

#include "shareme/core/playback_failure.hpp"

#include <cstdint>
#include <limits>
#include <optional>

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

struct AudioPcmBlockView {
  // Counts audio frames. A frame contains one sample for every channel.
  std::uint64_t receiver_sequence = 0;
  std::uint64_t frame_count = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  AudioSampleFormat sample_format = AudioSampleFormat::signed_int16;
  AudioInterleaving interleaving = AudioInterleaving::interleaved;
};

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
  return audio_duration_ms(pcm.frame_count, pcm.sample_rate);
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

class AudioOutputDevice {
 public:
  virtual ~AudioOutputDevice() = default;

  virtual WriteResult try_write(AudioPcmBlockView pcm) = 0;
  virtual AudioDeviceSnapshot snapshot() = 0;
  virtual FinalDeviceSnapshot quiesce_and_snapshot() = 0;
  virtual bool start() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
};

}  // namespace shareme::core
