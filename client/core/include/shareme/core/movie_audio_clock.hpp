#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace shareme::core {

enum class ClockConfidence {
  unavailable,
  provisional,
  locked,
  degraded,
  invalid,
};

using AudioClockConfidence = ClockConfidence;
using MovieAudioClockConfidence = ClockConfidence;
using MonotonicTime = std::chrono::steady_clock::time_point;

struct AudioClockObservation {
  std::uint64_t logical_consumed_frames = 0;
  std::optional<std::uint64_t> output_latency_frames = std::nullopt;
  std::uint32_t sample_rate = 0;
  std::uint64_t playback_generation = 0;
  std::uint64_t host_audio_epoch = 0;
  std::uint64_t renderer_clock_epoch = 0;
  std::uint64_t route_generation = 0;

  // A known frame count is the normal observation. false means that the
  // renderer cannot prove consumption for this observation.
  bool consumption_known = true;

  // A clock can lock only when the mapper supplies fresh usable evidence.
  ClockConfidence correlation_confidence = ClockConfidence::unavailable;
  bool correlation_valid = false;
  bool correlation_locked = false;

  // The media PTS is an anchor for the logical frame position. If its frame
  // is omitted, the PTS is associated with logical_consumed_frames.
  std::optional<std::int64_t> media_pts_ms = std::nullopt;
  std::optional<std::uint64_t> media_pts_frame = std::nullopt;

  // These names make the anchor relationship explicit for callers that do
  // not use media_pts_frame.
  std::optional<std::int64_t> anchor_pts_ms = std::nullopt;
  std::optional<std::uint64_t> anchor_consumed_frames = std::nullopt;

  bool discontinuity = false;
  bool underrun = false;
};

struct MovieAudioClockSnapshot {
  ClockConfidence confidence = ClockConfidence::unavailable;
  ClockConfidence clock_confidence = ClockConfidence::unavailable;
  std::int64_t estimated_playout_pts_ms = 0;
  std::int64_t audio_playout_pts_ms = 0;
  std::int64_t playout_pts_ms = 0;
  std::uint64_t logical_consumed_frames = 0;
  std::optional<std::uint64_t> output_latency_frames = std::nullopt;
  std::uint64_t playback_generation = 0;
  std::uint64_t host_audio_epoch = 0;
  std::uint64_t renderer_clock_epoch = 0;
  std::uint64_t route_generation = 0;
  bool consumption_known = false;
  bool has_playout_pts = false;
};

class MovieAudioClock {
 public:
  [[nodiscard]] MovieAudioClockSnapshot observe(
      AudioClockObservation observation,
      MonotonicTime monotonic_now) noexcept;
  [[nodiscard]] MovieAudioClockSnapshot snapshot() const noexcept;
  void reset() noexcept;

 private:
  void set_confidence(ClockConfidence confidence) noexcept;
  void mark_invalid() noexcept;
  [[nodiscard]] bool increment_renderer_clock_epoch() noexcept;

  MovieAudioClockSnapshot snapshot_{};
  bool initialized_ = false;
  bool consumption_unknown_ = false;
  bool renderer_clock_epoch_locally_advanced_ = false;
  bool requires_relock_ = false;
  bool have_observation_time_ = false;
  MonotonicTime last_observation_time_{};

  std::uint64_t logical_consumed_frames_ = 0;
  std::uint64_t renderer_clock_epoch_ = 0;
  std::uint64_t playback_generation_ = 0;
  std::uint64_t host_audio_epoch_ = 0;
  std::uint64_t route_generation_ = 0;
  std::uint32_t sample_rate_ = 0;

  bool have_anchor_ = false;
  std::int64_t anchor_pts_ms_ = 0;
  std::uint64_t anchor_consumed_frames_ = 0;

  bool have_playout_pts_ = false;
  std::int64_t last_playout_pts_ms_ = 0;
  std::uint64_t pts_playback_generation_ = 0;
  std::uint64_t pts_renderer_clock_epoch_ = 0;
};

}  // namespace shareme::core
