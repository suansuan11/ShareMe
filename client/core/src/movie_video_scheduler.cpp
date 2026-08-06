#include "shareme/core/movie_video_scheduler.hpp"

#include <limits>
#include <utility>

namespace shareme::core {
namespace {

constexpr std::int64_t kEarlyHoldEnterMs = 50;
constexpr std::int64_t kEarlyHoldExitMs = 25;
constexpr std::int64_t kLateDropEnterMs = -50;
constexpr std::int64_t kLateDropExitMs = -25;
constexpr std::int64_t kHardCandidateMs = -300;
constexpr std::int64_t kHardCandidateCancelMs = -250;
constexpr std::int64_t kMaximumEarlyHoldMs = 250;
constexpr std::size_t kMaximumHeldTokens = 3;
constexpr std::size_t kCandidateObservations = 4;
constexpr std::int64_t kCandidateWindowMs = 750;

[[nodiscard]] std::optional<std::int64_t> checked_delta(
    std::int64_t video_pts_ms, std::int64_t audio_pts_ms) noexcept {
  if ((audio_pts_ms < 0 && video_pts_ms >
           std::numeric_limits<std::int64_t>::max() + audio_pts_ms) ||
      (audio_pts_ms > 0 && video_pts_ms <
           std::numeric_limits<std::int64_t>::min() + audio_pts_ms))
    return std::nullopt;
  return video_pts_ms - audio_pts_ms;
}

[[nodiscard]] bool has_event(const VideoSchedulerUpdate &update,
                             VideoSchedulerEvent expected) noexcept {
  for (const auto event : update.events) {
    if (event == expected)
      return true;
  }
  return false;
}

} // namespace

MovieVideoPlayoutScheduler::MovieVideoPlayoutScheduler(
    MovieVideoPlayoutSchedulerConfig config) noexcept
    : config_(config) {}

VideoSchedulerUpdate MovieVideoPlayoutScheduler::submit(VideoFrameTiming timing) {
  VideoSchedulerUpdate update;
  if (!valid_locked_clock() || timing.playback_generation != playback_generation_ ||
      hold_limit_blocked_) {
    snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
    snapshot_.applied_action = VideoAppliedAction::pass_through;
    update.disposition = VideoFrameDisposition::pass_through;
    return update;
  }

  const auto delta = delta_ms(timing.media_pts_ms);
  if (!delta) {
    snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
    snapshot_.applied_action = VideoAppliedAction::pass_through;
    update.disposition = VideoFrameDisposition::pass_through;
    return update;
  }

  const auto disposition = policy_disposition(*delta, update);
  if (!config_.apply_policy) {
    update.disposition = VideoFrameDisposition::pass_through;
    snapshot_.applied_action = VideoAppliedAction::pass_through;
    return update;
  }

  update.disposition = disposition;
  if (disposition == VideoFrameDisposition::hold) {
    const auto held_span = held_.empty()
                               ? std::optional<std::int64_t>{0}
                               : checked_delta(
                                     timing.media_pts_ms,
                                     held_.front().timing.media_pts_ms);
    if (held_.size() >= kMaximumHeldTokens ||
        (!held_.empty() &&
         (!held_span || *held_span >= kMaximumEarlyHoldMs))) {
      release_held(update, VideoFrameDisposition::pass_through,
                   VideoTokenReleaseReason::early_hold_limit);
      hold_limit_blocked_ = true;
      snapshot_.clock_blocked = true;
      snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
      snapshot_.applied_action = VideoAppliedAction::pass_through;
      update.events.push_back(VideoSchedulerEvent::early_hold_limit);
      update.events.push_back(VideoSchedulerEvent::clock_blocked);
      update.disposition = VideoFrameDisposition::pass_through;
      return update;
    }
    held_.push_back(HeldToken{.timing = timing});
    snapshot_.held_token_count = held_.size();
    snapshot_.applied_action = VideoAppliedAction::none;
  } else if (disposition == VideoFrameDisposition::drop) {
    update.released.push_back(
        {.token = timing.token,
         .disposition = VideoFrameDisposition::drop,
         .reason = VideoTokenReleaseReason::late_drop});
    snapshot_.applied_action = VideoAppliedAction::late_drop;
  } else {
    snapshot_.applied_action = VideoAppliedAction::present;
  }
  return update;
}

VideoSchedulerUpdate MovieVideoPlayoutScheduler::advance(VideoClockInput input) {
  VideoSchedulerUpdate update;
  const auto generation_changed =
      have_clock_ && input.playback_generation != playback_generation_;
  const auto route_changed =
      have_clock_ && input.route_generation != route_generation_;
  const auto sequence_invalid =
      have_clock_ && input.observation_sequence != 0 &&
      input.observation_sequence <= last_observation_sequence_;

  if (generation_changed) {
    reset_candidate(update, true);
    release_held(update, VideoFrameDisposition::pass_through,
                 VideoTokenReleaseReason::generation_reset);
    reset_policy();
    update.events.push_back(VideoSchedulerEvent::hard_resync_generation_changed);
  }
  if (route_changed || input.route_transition) {
    reset_candidate(update, true);
    release_held(update, VideoFrameDisposition::pass_through,
                 VideoTokenReleaseReason::route_transition);
    reset_policy();
    update.events.push_back(VideoSchedulerEvent::route_transition);
  }
  if (sequence_invalid) {
    release_held(update, VideoFrameDisposition::pass_through,
                 VideoTokenReleaseReason::sequence_invalid);
    reset_candidate(update, true);
    snapshot_.clock_blocked = true;
    snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
    update.events.push_back(VideoSchedulerEvent::clock_blocked);
  }

  const auto confidence_lost =
      input.clock_confidence != ClockConfidence::locked || !input.playing;
  snapshot_.clock_confidence = input.clock_confidence;
  audio_playout_pts_ms_ = input.audio_playout_pts_ms;
  playback_generation_ = input.playback_generation;
  route_generation_ = input.route_generation;
  have_clock_ = true;
  if (input.observation_sequence != 0)
    last_observation_sequence_ = input.observation_sequence;

  if (confidence_lost || input.discontinuity) {
    if (!held_.empty())
      release_held(update, VideoFrameDisposition::pass_through,
                   VideoTokenReleaseReason::clock_blocked);
    reset_candidate(update, true);
    early_hold_active_ = false;
    late_drop_active_ = false;
    hold_limit_blocked_ = false;
    snapshot_.clock_blocked = true;
    snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
    snapshot_.applied_action = VideoAppliedAction::pass_through;
    if (!has_event(update, VideoSchedulerEvent::clock_blocked))
      update.events.push_back(VideoSchedulerEvent::clock_blocked);
    snapshot_.held_token_count = 0;
    return update;
  }

  snapshot_.clock_blocked = hold_limit_blocked_ || sequence_invalid;
  if (!hold_limit_blocked_)
    snapshot_.suggested_action = VideoSuggestedAction::none;
  if (sequence_invalid) {
    snapshot_.suggested_action = VideoSuggestedAction::clock_blocked;
    snapshot_.applied_action = VideoAppliedAction::pass_through;
  }

  if (!hold_limit_blocked_ && !held_.empty()) {
    std::vector<HeldToken> remaining;
    remaining.reserve(held_.size());
    for (const auto &held : held_) {
      const auto delta = delta_ms(held.timing.media_pts_ms);
      if (!delta || *delta > kEarlyHoldExitMs) {
        remaining.push_back(held);
        continue;
      }
      const auto disposition = *delta <= kLateDropEnterMs
                                   ? VideoFrameDisposition::drop
                                   : VideoFrameDisposition::present;
      update.released.push_back(
          {.token = held.timing.token,
           .disposition = disposition,
           .reason = disposition == VideoFrameDisposition::drop
                         ? VideoTokenReleaseReason::late_drop
                         : VideoTokenReleaseReason::none});
      snapshot_.applied_action = disposition == VideoFrameDisposition::drop
                                     ? VideoAppliedAction::late_drop
                                     : VideoAppliedAction::present;
    }
    held_ = std::move(remaining);
    snapshot_.held_token_count = held_.size();
    if (held_.empty())
      early_hold_active_ = false;
  }

  if (!hold_limit_blocked_ && !generation_changed && !route_changed &&
      !input.route_transition && !sequence_invalid)
    update_candidate(input, update);
  return update;
}

VideoSchedulerUpdate MovieVideoPlayoutScheduler::shutdown() noexcept {
  VideoSchedulerUpdate update;
  release_held(update, VideoFrameDisposition::pass_through,
               VideoTokenReleaseReason::shutdown);
  reset_candidate(update, false);
  reset_policy();
  have_clock_ = false;
  snapshot_ = {};
  return update;
}

VideoSchedulerSnapshot MovieVideoPlayoutScheduler::snapshot() const noexcept {
  return snapshot_;
}

bool MovieVideoPlayoutScheduler::valid_locked_clock() const noexcept {
  return have_clock_ && snapshot_.clock_confidence == ClockConfidence::locked &&
      !snapshot_.clock_blocked;
}

std::optional<std::int64_t>
MovieVideoPlayoutScheduler::delta_ms(std::int64_t video_pts_ms) const noexcept {
  return checked_delta(video_pts_ms, audio_playout_pts_ms_);
}

VideoFrameDisposition MovieVideoPlayoutScheduler::policy_disposition(
    std::int64_t delta, VideoSchedulerUpdate &) noexcept {
  if (early_hold_active_) {
    if (delta <= kEarlyHoldExitMs) {
      early_hold_active_ = false;
    } else {
      snapshot_.suggested_action = VideoSuggestedAction::early_hold;
      return VideoFrameDisposition::hold;
    }
  }
  if (late_drop_active_) {
    if (delta >= kLateDropExitMs) {
      late_drop_active_ = false;
    } else {
      snapshot_.suggested_action = VideoSuggestedAction::late_drop;
      return VideoFrameDisposition::drop;
    }
  }
  if (delta >= kEarlyHoldEnterMs) {
    early_hold_active_ = true;
    snapshot_.suggested_action = VideoSuggestedAction::early_hold;
    return VideoFrameDisposition::hold;
  }
  if (delta <= kLateDropEnterMs) {
    late_drop_active_ = true;
    snapshot_.suggested_action = VideoSuggestedAction::late_drop;
    return VideoFrameDisposition::drop;
  }
  snapshot_.suggested_action = VideoSuggestedAction::none;
  return VideoFrameDisposition::present;
}

void MovieVideoPlayoutScheduler::update_candidate(
    const VideoClockInput &input, VideoSchedulerUpdate &update) noexcept {
  if (!input.observed_video_pts_ms) {
    reset_candidate(update, true);
    return;
  }
  const auto observed_delta = delta_ms(*input.observed_video_pts_ms);
  if (!observed_delta || *observed_delta > kHardCandidateCancelMs) {
    reset_candidate(update, true);
    return;
  }
  if (*observed_delta > kHardCandidateMs)
    return;
  if (!candidate_active_) {
    candidate_active_ = true;
    candidate_start_time_ms_ = input.observation_time_ms;
    candidate_last_time_ms_ = input.observation_time_ms;
    candidate_last_sequence_ = input.observation_sequence;
    snapshot_.candidate_observation_count = 1;
    update.events.push_back(VideoSchedulerEvent::candidate_started);
  } else if (input.observation_sequence <= candidate_last_sequence_ ||
             input.observation_time_ms < candidate_last_time_ms_) {
    reset_candidate(update, true);
    return;
  } else {
    candidate_last_sequence_ = input.observation_sequence;
    candidate_last_time_ms_ = input.observation_time_ms;
    ++snapshot_.candidate_observation_count;
  }
  const auto candidate_window =
      checked_delta(candidate_last_time_ms_, candidate_start_time_ms_);
  snapshot_.hard_resync_candidate =
      snapshot_.candidate_observation_count >= kCandidateObservations &&
      candidate_window && *candidate_window >= kCandidateWindowMs;
  snapshot_.suggested_action = VideoSuggestedAction::hard_resync_candidate;
}

void MovieVideoPlayoutScheduler::reset_candidate(
    VideoSchedulerUpdate &update, bool emit_cancellation) noexcept {
  if (candidate_active_ && emit_cancellation)
    update.events.push_back(VideoSchedulerEvent::candidate_cancelled);
  candidate_active_ = false;
  snapshot_.hard_resync_candidate = false;
  snapshot_.candidate_observation_count = 0;
  if (snapshot_.suggested_action == VideoSuggestedAction::hard_resync_candidate)
    snapshot_.suggested_action = VideoSuggestedAction::none;
}

void MovieVideoPlayoutScheduler::release_held(
    VideoSchedulerUpdate &update, VideoFrameDisposition disposition,
    VideoTokenReleaseReason reason) noexcept {
  for (const auto &held : held_)
    update.released.push_back(
        {.token = held.timing.token, .disposition = disposition, .reason = reason});
  held_.clear();
  snapshot_.held_token_count = 0;
}

void MovieVideoPlayoutScheduler::reset_policy() noexcept {
  early_hold_active_ = false;
  late_drop_active_ = false;
  hold_limit_blocked_ = false;
  snapshot_.clock_blocked = false;
  snapshot_.suggested_action = VideoSuggestedAction::none;
  snapshot_.applied_action = VideoAppliedAction::none;
  snapshot_.held_token_count = held_.size();
}

} // namespace shareme::core
