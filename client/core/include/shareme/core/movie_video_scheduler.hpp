#pragma once

#include "shareme/core/movie_audio_clock.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace shareme::core {

enum class VideoFrameDisposition {
  pass_through,
  hold,
  present,
  drop,
};

enum class VideoTokenReleaseReason {
  none,
  early_hold_limit,
  late_drop,
  clock_blocked,
  generation_reset,
  route_transition,
  sequence_invalid,
  shutdown,
};

enum class VideoSchedulerEvent {
  clock_blocked,
  candidate_started,
  candidate_cancelled,
  cooldown_started,
  hard_resync_generation_changed,
  hard_resync_applied,
  hard_resync_exited,
  route_transition,
  early_hold_limit,
};

enum class VideoSuggestedAction {
  none,
  early_hold,
  late_drop,
  hard_resync_candidate,
  clock_blocked,
};

enum class VideoAppliedAction {
  none,
  pass_through,
  present,
  late_drop,
};

struct VideoFrameTiming {
  std::uint64_t token{};
  std::int64_t media_pts_ms{};
  std::uint64_t playback_generation{};
};

struct VideoClockInput {
  ClockConfidence clock_confidence{ClockConfidence::unavailable};
  std::int64_t audio_playout_pts_ms{};
  std::uint64_t playback_generation{};
  std::uint64_t route_generation{};
  bool playing{};
  std::uint64_t observation_sequence{};
  std::int64_t observation_time_ms{};
  std::optional<std::int64_t> observed_video_pts_ms;
  bool discontinuity{};
  bool route_transition{};
};

struct VideoTokenRelease {
  std::uint64_t token{};
  VideoFrameDisposition disposition{VideoFrameDisposition::pass_through};
  VideoTokenReleaseReason reason{VideoTokenReleaseReason::none};
};

struct VideoSchedulerUpdate {
  VideoFrameDisposition disposition{VideoFrameDisposition::pass_through};
  std::vector<VideoTokenRelease> released;
  std::vector<VideoSchedulerEvent> events;
};

struct VideoSchedulerSnapshot {
  ClockConfidence clock_confidence{ClockConfidence::unavailable};
  VideoSuggestedAction suggested_action{VideoSuggestedAction::none};
  VideoAppliedAction applied_action{VideoAppliedAction::none};
  std::size_t held_token_count{};
  bool clock_blocked{};
  bool hard_resync_candidate{};
  std::size_t candidate_observation_count{};
};

struct MovieVideoPlayoutSchedulerConfig {
  bool apply_policy{false};
};

class MovieVideoPlayoutScheduler final {
public:
  explicit MovieVideoPlayoutScheduler(
      MovieVideoPlayoutSchedulerConfig config = {}) noexcept;

  [[nodiscard]] VideoSchedulerUpdate submit(VideoFrameTiming timing);
  [[nodiscard]] VideoSchedulerUpdate advance(VideoClockInput input);
  [[nodiscard]] VideoSchedulerUpdate shutdown() noexcept;
  [[nodiscard]] VideoSchedulerSnapshot snapshot() const noexcept;

private:
  struct HeldToken {
    VideoFrameTiming timing;
  };

  [[nodiscard]] bool valid_locked_clock() const noexcept;
  [[nodiscard]] std::optional<std::int64_t>
  delta_ms(std::int64_t video_pts_ms) const noexcept;
  [[nodiscard]] VideoFrameDisposition policy_disposition(
      std::int64_t delta, VideoSchedulerUpdate &update) noexcept;
  void update_candidate(const VideoClockInput &input,
                        VideoSchedulerUpdate &update) noexcept;
  void reset_candidate(VideoSchedulerUpdate &update,
                       bool emit_cancellation) noexcept;
  void release_held(VideoSchedulerUpdate &update,
                    VideoFrameDisposition disposition,
                    VideoTokenReleaseReason reason) noexcept;
  void reset_policy() noexcept;

  const MovieVideoPlayoutSchedulerConfig config_;
  VideoSchedulerSnapshot snapshot_{};
  std::vector<HeldToken> held_;
  std::uint64_t playback_generation_{};
  std::uint64_t route_generation_{};
  std::int64_t audio_playout_pts_ms_{};
  std::uint64_t last_observation_sequence_{};
  bool have_clock_{false};
  bool early_hold_active_{false};
  bool late_drop_active_{false};
  bool hold_limit_blocked_{false};
  bool candidate_active_{false};
  std::int64_t candidate_start_time_ms_{};
  std::int64_t candidate_last_time_ms_{};
  std::uint64_t candidate_last_sequence_{};
};

} // namespace shareme::core
