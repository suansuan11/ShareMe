#include "shareme/core/movie_video_scheduler.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::core::VideoClockInput locked_clock(
    std::int64_t audio_pts_ms, std::uint64_t sequence,
    std::uint64_t generation = 1, std::int64_t observation_time_ms = 0,
    std::optional<std::int64_t> observed_video_pts_ms = std::nullopt) {
  return {.clock_confidence = shareme::core::ClockConfidence::locked,
          .audio_playout_pts_ms = audio_pts_ms,
          .playback_generation = generation,
          .route_generation = 1,
          .playing = true,
          .observation_sequence = sequence,
          .observation_time_ms = observation_time_ms,
          .observed_video_pts_ms = observed_video_pts_ms};
}

bool contains_event(const shareme::core::VideoSchedulerUpdate &update,
                    shareme::core::VideoSchedulerEvent expected) {
  return std::find(update.events.begin(), update.events.end(), expected) !=
      update.events.end();
}

const shareme::core::VideoTokenRelease *find_release(
    const shareme::core::VideoSchedulerUpdate &update, std::uint64_t token) {
  const auto found = std::find_if(
      update.released.begin(), update.released.end(),
      [token](const auto &release) { return release.token == token; });
  return found == update.released.end() ? nullptr : &*found;
}

void unavailable_clock_is_pass_through() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  const auto update = scheduler.advance({
      .clock_confidence = ClockConfidence::provisional,
      .audio_playout_pts_ms = 1'000,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 1,
      .observation_time_ms = 0});
  REQUIRE(contains_event(update, VideoSchedulerEvent::clock_blocked));
  const auto frame = scheduler.submit(
      {.token = 1, .media_pts_ms = 1'200, .playback_generation = 1});
  REQUIRE(frame.disposition == VideoFrameDisposition::pass_through);
  REQUIRE(scheduler.snapshot().suggested_action ==
          VideoSuggestedAction::clock_blocked);
}

void every_non_locked_confidence_is_pass_through() {
  using namespace shareme::core;
  const ClockConfidence confidences[] = {
      ClockConfidence::unavailable, ClockConfidence::provisional,
      ClockConfidence::degraded, ClockConfidence::invalid};
  for (const auto confidence : confidences) {
    MovieVideoPlayoutScheduler scheduler{
        MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
    static_cast<void>(scheduler.advance({
        .clock_confidence = confidence,
        .audio_playout_pts_ms = 0,
        .playback_generation = 1,
        .route_generation = 1,
        .playing = true,
        .observation_sequence = 1}));
    REQUIRE(scheduler.submit({.token = 100,
                              .media_pts_ms = 100,
                              .playback_generation = 1})
                .disposition == VideoFrameDisposition::pass_through);
  }
}

void hysteresis_uses_frozen_thresholds() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(scheduler.advance(locked_clock(1'000, 1)));

  const auto early = scheduler.submit(
      {.token = 1, .media_pts_ms = 1'050, .playback_generation = 1});
  REQUIRE(early.disposition == VideoFrameDisposition::hold);
  const auto still_early = scheduler.submit(
      {.token = 2, .media_pts_ms = 1'049, .playback_generation = 1});
  REQUIRE(still_early.disposition == VideoFrameDisposition::hold);

  const auto early_exit = scheduler.advance(locked_clock(1'025, 2));
  REQUIRE(find_release(early_exit, 1) != nullptr);
  REQUIRE(find_release(early_exit, 1)->disposition ==
          VideoFrameDisposition::present);
  REQUIRE(find_release(early_exit, 2) != nullptr);
  REQUIRE(find_release(early_exit, 2)->disposition ==
          VideoFrameDisposition::present);

  static_cast<void>(scheduler.advance(locked_clock(2'000, 3)));
  const auto late = scheduler.submit(
      {.token = 3, .media_pts_ms = 1'950, .playback_generation = 1});
  REQUIRE(late.disposition == VideoFrameDisposition::drop);
  REQUIRE(late.released.size() == 1);
  REQUIRE(late.released.front().token == 3);
  REQUIRE(late.released.front().reason ==
          VideoTokenReleaseReason::late_drop);
  const auto hysteresis_late = scheduler.submit(
      {.token = 4, .media_pts_ms = 1'970, .playback_generation = 1});
  REQUIRE(hysteresis_late.disposition == VideoFrameDisposition::drop);
  const auto late_exit = scheduler.submit(
      {.token = 5, .media_pts_ms = 1'980, .playback_generation = 1});
  REQUIRE(late_exit.disposition == VideoFrameDisposition::present);
}

void early_hold_is_bounded_and_blocks_the_clock() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(scheduler.advance(locked_clock(0, 1)));
  REQUIRE(scheduler.submit({.token = 1, .media_pts_ms = 50,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  REQUIRE(scheduler.submit({.token = 2, .media_pts_ms = 100,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  REQUIRE(scheduler.submit({.token = 3, .media_pts_ms = 200,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);

  const auto bound = scheduler.submit({.token = 4,
                                       .media_pts_ms = 300,
                                       .playback_generation = 1});
  REQUIRE(bound.disposition == VideoFrameDisposition::pass_through);
  REQUIRE(bound.released.size() == 3);
  REQUIRE(bound.released.front().reason ==
          VideoTokenReleaseReason::early_hold_limit);
  REQUIRE(scheduler.snapshot().clock_blocked);
  REQUIRE(scheduler.snapshot().held_token_count == 0);
  REQUIRE(contains_event(bound, VideoSchedulerEvent::clock_blocked));
  REQUIRE(contains_event(bound, VideoSchedulerEvent::early_hold_limit));
  const auto still_blocked = scheduler.advance(locked_clock(0, 2));
  REQUIRE(!contains_event(still_blocked,
                          VideoSchedulerEvent::candidate_started));
  REQUIRE(scheduler.snapshot().clock_blocked);
  REQUIRE(!scheduler.snapshot().hard_resync_candidate);
  REQUIRE(scheduler.submit({.token = 5,
                            .media_pts_ms = 350,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::pass_through);

  static_cast<void>(scheduler.advance(locked_clock(0, 3, 2)));
  REQUIRE(!scheduler.snapshot().clock_blocked);

  MovieVideoPlayoutScheduler span_scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(span_scheduler.advance(locked_clock(0, 1)));
  REQUIRE(span_scheduler.submit({.token = 6, .media_pts_ms = 50,
                                  .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  const auto span_bound = span_scheduler.submit(
      {.token = 7, .media_pts_ms = 300, .playback_generation = 1});
  REQUIRE(span_bound.disposition == VideoFrameDisposition::pass_through);
  REQUIRE(span_bound.released.size() == 1);
  REQUIRE(span_bound.released.front().token == 6);
}

void hard_resync_candidate_requires_ordered_periodic_observations() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  const auto first = scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700));
  REQUIRE(contains_event(first, VideoSchedulerEvent::candidate_started));
  static_cast<void>(scheduler.advance(locked_clock(1'250, 2, 1, 250, 950)));
  static_cast<void>(scheduler.advance(locked_clock(1'500, 3, 1, 500, 1'200)));
  const auto qualified = scheduler.advance(
      locked_clock(1'750, 4, 1, 750, 1'450));
  REQUIRE(scheduler.snapshot().hard_resync_candidate);
  REQUIRE(scheduler.snapshot().candidate_observation_count == 4);
  REQUIRE(!contains_event(qualified, VideoSchedulerEvent::hard_resync_applied));

  const auto cancelled = scheduler.advance(
      locked_clock(2'000, 5, 1, 1'000, 1'751));
  REQUIRE(contains_event(cancelled, VideoSchedulerEvent::candidate_cancelled));
  REQUIRE(!scheduler.snapshot().hard_resync_candidate);
}

void candidate_resets_on_scope_and_sequence_changes() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler generation_scheduler;
  static_cast<void>(generation_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  REQUIRE(generation_scheduler.snapshot().candidate_observation_count == 1);
  const auto generation_reset = generation_scheduler.advance(
      locked_clock(1'000, 2, 2, 250, 700));
  REQUIRE(contains_event(
      generation_reset, VideoSchedulerEvent::hard_resync_generation_changed));
  REQUIRE(!generation_scheduler.snapshot().hard_resync_candidate);
  REQUIRE(generation_scheduler.snapshot().candidate_observation_count == 0);
  REQUIRE(!contains_event(generation_reset,
                          VideoSchedulerEvent::candidate_started));

  MovieVideoPlayoutScheduler sequence_scheduler;
  static_cast<void>(sequence_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  const auto sequence_reset = sequence_scheduler.advance(
      locked_clock(1'000, 1, 1, 250, 700));
  REQUIRE(contains_event(sequence_reset, VideoSchedulerEvent::clock_blocked));
  REQUIRE(!sequence_scheduler.snapshot().hard_resync_candidate);
  REQUIRE(sequence_scheduler.snapshot().suggested_action ==
          VideoSuggestedAction::clock_blocked);
  REQUIRE(sequence_scheduler.snapshot().applied_action ==
          VideoAppliedAction::pass_through);

  MovieVideoPlayoutScheduler sequence_release_scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(sequence_release_scheduler.advance(locked_clock(0, 1)));
  REQUIRE(sequence_release_scheduler.submit({.token = 31, .media_pts_ms = 50,
                                             .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  const auto sequence_release =
      sequence_release_scheduler.advance(locked_clock(0, 1));
  REQUIRE(sequence_release.released.size() == 1);
  REQUIRE(sequence_release.released.front().reason ==
          VideoTokenReleaseReason::sequence_invalid);

  MovieVideoPlayoutScheduler clock_release_scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(clock_release_scheduler.advance(locked_clock(0, 1)));
  REQUIRE(clock_release_scheduler.submit({.token = 32, .media_pts_ms = 50,
                                          .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  const auto clock_release = clock_release_scheduler.advance({
      .clock_confidence = ClockConfidence::provisional,
      .audio_playout_pts_ms = 0,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 2});
  REQUIRE(clock_release.released.size() == 1);
  REQUIRE(clock_release.released.front().reason ==
          VideoTokenReleaseReason::clock_blocked);

  MovieVideoPlayoutScheduler pause_scheduler;
  static_cast<void>(pause_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  auto paused = locked_clock(1'000, 2, 1, 250, 700);
  paused.playing = false;
  static_cast<void>(pause_scheduler.advance(paused));
  REQUIRE(!pause_scheduler.snapshot().hard_resync_candidate);

  MovieVideoPlayoutScheduler discontinuity_scheduler;
  static_cast<void>(discontinuity_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  auto discontinuity = locked_clock(1'000, 2, 1, 250, 700);
  discontinuity.discontinuity = true;
  static_cast<void>(discontinuity_scheduler.advance(discontinuity));
  REQUIRE(!discontinuity_scheduler.snapshot().hard_resync_candidate);
  REQUIRE(discontinuity_scheduler.snapshot().candidate_observation_count == 0);

  MovieVideoPlayoutScheduler route_scheduler;
  static_cast<void>(route_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  auto route = locked_clock(1'000, 2, 1, 250, 700);
  route.route_generation = 2;
  route.route_transition = true;
  static_cast<void>(route_scheduler.advance(route));
  REQUIRE(!route_scheduler.snapshot().hard_resync_candidate);
  REQUIRE(route_scheduler.snapshot().candidate_observation_count == 0);

  MovieVideoPlayoutScheduler same_route_scheduler;
  static_cast<void>(same_route_scheduler.advance(
      locked_clock(1'000, 1, 1, 0, 700)));
  auto same_route = locked_clock(1'000, 2, 1, 250, 700);
  same_route.route_transition = true;
  const auto same_route_update = same_route_scheduler.advance(same_route);
  REQUIRE(!same_route_scheduler.snapshot().hard_resync_candidate);
  REQUIRE(same_route_scheduler.snapshot().candidate_observation_count == 0);
  REQUIRE(!contains_event(same_route_update,
                          VideoSchedulerEvent::candidate_started));
}

void shutdown_releases_held_tokens() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(scheduler.advance(locked_clock(0, 1)));
  REQUIRE(scheduler.submit({.token = 42, .media_pts_ms = 50,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  const auto update = scheduler.shutdown();
  REQUIRE(update.released.size() == 1);
  REQUIRE(update.released.front().token == 42);
  REQUIRE(update.released.front().reason == VideoTokenReleaseReason::shutdown);
}

void generation_and_route_resets_release_held_tokens() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler{
      MovieVideoPlayoutSchedulerConfig{.apply_policy = true}};
  static_cast<void>(scheduler.advance(locked_clock(0, 1)));
  REQUIRE(scheduler.submit({.token = 10, .media_pts_ms = 50,
                            .playback_generation = 1})
              .disposition == VideoFrameDisposition::hold);
  auto generation_change = locked_clock(0, 2, 2);
  const auto generation_update = scheduler.advance(generation_change);
  REQUIRE(find_release(generation_update, 10) != nullptr);
  REQUIRE(find_release(generation_update, 10)->disposition ==
          VideoFrameDisposition::pass_through);
  REQUIRE(find_release(generation_update, 10)->reason ==
          VideoTokenReleaseReason::generation_reset);
  REQUIRE(contains_event(generation_update,
                         VideoSchedulerEvent::hard_resync_generation_changed));

  static_cast<void>(scheduler.advance(locked_clock(0, 3, 2)));
  REQUIRE(scheduler.submit({.token = 11, .media_pts_ms = 50,
                            .playback_generation = 2})
              .disposition == VideoFrameDisposition::hold);
  auto route_change = locked_clock(0, 4, 2);
  route_change.route_generation = 2;
  route_change.route_transition = true;
  const auto route_update = scheduler.advance(route_change);
  REQUIRE(find_release(route_update, 11) != nullptr);
  REQUIRE(find_release(route_update, 11)->reason ==
          VideoTokenReleaseReason::route_transition);
  REQUIRE(contains_event(route_update, VideoSchedulerEvent::route_transition));
}

void observational_mode_never_applies_policy() {
  using namespace shareme::core;
  MovieVideoPlayoutScheduler scheduler;
  static_cast<void>(scheduler.advance(locked_clock(0, 1)));
  const auto early = scheduler.submit(
      {.token = 21, .media_pts_ms = 100, .playback_generation = 1});
  REQUIRE(early.disposition == VideoFrameDisposition::pass_through);
  REQUIRE(scheduler.snapshot().suggested_action ==
          VideoSuggestedAction::early_hold);
  REQUIRE(scheduler.snapshot().applied_action == VideoAppliedAction::pass_through);
}

void observational_configuration_is_explicit() {
  const auto config =
      shareme::core::MovieVideoPlayoutSchedulerConfig::observational();
  REQUIRE(!config.apply_policy);
}

} // namespace

int main() {
  unavailable_clock_is_pass_through();
  every_non_locked_confidence_is_pass_through();
  hysteresis_uses_frozen_thresholds();
  early_hold_is_bounded_and_blocks_the_clock();
  hard_resync_candidate_requires_ordered_periodic_observations();
  candidate_resets_on_scope_and_sequence_changes();
  generation_and_route_resets_release_held_tokens();
  shutdown_releases_held_tokens();
  observational_mode_never_applies_policy();
  observational_configuration_is_explicit();
  return EXIT_SUCCESS;
}
