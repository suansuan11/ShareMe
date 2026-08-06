#pragma once

#include "shareme/core/audio_output_contract.hpp"
#include "shareme/core/movie_audio_pts_mapper.hpp"

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

struct AudioClockAnchor {
  std::uint64_t control_sequence = 0;
  std::uint64_t host_source_sequence = 0;
  std::uint64_t playback_generation = 0;
  std::uint64_t audio_epoch = 0;
  std::int64_t media_pts_ms = 0;
  std::uint64_t consumed_frames = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
};

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

  // One PTS anchor representation avoids conflicting media/anchor aliases.
  std::optional<AudioClockAnchor> anchor = std::nullopt;

  // Only a mapper-produced result with approved provenance can establish a
  // lock. An absent result is valid for ordinary observations after a lock.
  std::optional<CorrelationResult> correlation = std::nullopt;

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
  void clear_playback_anchor() noexcept;
  void reset() noexcept;

 private:
  struct CorrelationSequence {
    std::uint64_t source_sequence = 0;
    std::uint64_t decoded_sequence = 0;
  };

  void set_confidence(ClockConfidence confidence) noexcept;
  void begin_relock() noexcept;
  void mark_invalid() noexcept;
  [[nodiscard]] bool increment_renderer_clock_epoch() noexcept;

  MovieAudioClockSnapshot snapshot_{};
  bool initialized_ = false;
  bool consumption_unknown_ = false;
  bool renderer_clock_epoch_locally_advanced_ = false;
  bool renderer_clock_epoch_overflowed_ = false;
  bool requires_relock_ = false;
  bool relock_correlation_ready_ = false;
  bool have_observation_time_ = false;
  MonotonicTime last_observation_time_{};

  std::uint64_t logical_consumed_frames_ = 0;
  std::uint64_t renderer_clock_epoch_ = 0;
  std::uint64_t playback_generation_ = 0;
  std::uint64_t host_audio_epoch_ = 0;
  std::uint64_t route_generation_ = 0;
  std::uint32_t sample_rate_ = 0;

  std::optional<AudioClockAnchor> anchor_ = std::nullopt;
  std::optional<CorrelationResult> last_correlation_ = std::nullopt;
  std::optional<CorrelationSequence> relock_floor_ = std::nullopt;

  bool have_playout_pts_ = false;
  std::int64_t last_playout_pts_ms_ = 0;
  std::uint64_t pts_playback_generation_ = 0;
  std::uint64_t pts_host_audio_epoch_ = 0;
  std::uint64_t pts_renderer_clock_epoch_ = 0;
};

}  // namespace shareme::core
