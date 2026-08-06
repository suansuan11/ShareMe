#include "shareme/core/audio_route.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

using namespace shareme::core;

AudioRouteEvent route_event(
    std::uint64_t sequence, AudioRouteDeviceId device,
    AudioRouteChangeKind change_kind =
        AudioRouteChangeKind::default_output_changed,
    AudioRouteDefaultRole default_role = AudioRouteDefaultRole::default_output) {
  return AudioRouteEvent{
      .event_sequence = sequence,
      .stable_device_id = device,
      .change_kind = change_kind,
      .default_role = default_role,
      .observed_at = std::chrono::steady_clock::time_point{
          std::chrono::milliseconds{static_cast<std::int64_t>(sequence)}},
  };
}

AudioRouteActivationResult activation(
    AudioRouteCandidate candidate, AudioRouteActivationStatus status,
    bool old_route_resumed = false) {
  return AudioRouteActivationResult{
      .candidate = std::move(candidate),
      .status = status,
      .old_route_resumed = old_route_resumed,
  };
}

FinalDeviceSnapshot final_snapshot(AudioRouteDeviceId device,
                                   std::uint64_t sequence,
                                   std::uint64_t accepted_frames,
                                   std::uint64_t consumed_frames,
                                   std::uint64_t queue_frames,
                                   bool exact_consumption) {
  return FinalDeviceSnapshot{
      .device_instance_id = device,
      .snapshot_sequence = sequence,
      .accepted_frames_total = accepted_frames,
      .device_consumed_frames_total = consumed_frames,
      .device_queue_frames = queue_frames,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = exact_consumption,
  };
}

void monitor_rejects_stale_and_shutdown_notifications() {
  AudioRouteMonitor monitor;
  std::uint64_t callback_count = 0;
  REQUIRE(monitor.start([&](AudioRouteEvent) { ++callback_count; }));
  REQUIRE(monitor.notify(route_event(1, 10)));
  REQUIRE(!monitor.notify(route_event(1, 10)));
  REQUIRE(callback_count == 1);

  monitor.stop();
  REQUIRE(!monitor.notify(route_event(2, 11)));
  REQUIRE(!monitor.start([](AudioRouteEvent) {}));

  const auto start = std::chrono::steady_clock::now();
  for (std::uint64_t sequence = 2; sequence < 10'002; ++sequence)
    REQUIRE(!monitor.notify(route_event(sequence, 11)));
  REQUIRE(std::chrono::steady_clock::now() - start <
          std::chrono::milliseconds{250});
  REQUIRE(callback_count == 1);
}

void notifications_coalesce_without_generation_change_and_reject_stale_events() {
  AudioRouteController controller{AudioRouteControllerConfig{
      .initial_device_id = AudioRouteDeviceId{10},
      .candidate_activator = {},
  }};

  const auto first = controller.on_route_notification(route_event(1, 20));
  REQUIRE(first.status == AudioRouteNotificationStatus::candidate_pending);
  REQUIRE(first.route_generation == 0);
  REQUIRE(first.candidate.has_value());
  REQUIRE(first.candidate->event.event_sequence == 1);

  const auto duplicate = controller.on_route_notification(route_event(2, 20));
  REQUIRE(duplicate.status == AudioRouteNotificationStatus::coalesced);
  REQUIRE(duplicate.route_generation == 0);
  REQUIRE(duplicate.candidate.has_value());
  REQUIRE(duplicate.candidate->event.event_sequence == 2);

  const auto stale = controller.on_route_notification(route_event(1, 30));
  REQUIRE(stale.status == AudioRouteNotificationStatus::stale);
  REQUIRE(stale.route_generation == 0);
  REQUIRE(controller.pending_candidate().has_value());
  REQUIRE(controller.pending_candidate()->event.stable_device_id == 20);
}

void stale_candidates_and_candidate_loss_never_commit_a_generation() {
  AudioRouteController controller{AudioRouteControllerConfig{
      .initial_device_id = AudioRouteDeviceId{10},
      .candidate_activator = {},
  }};

  const auto first = controller.on_route_notification(route_event(1, 20));
  REQUIRE(first.candidate.has_value());
  const auto stale_candidate = *first.candidate;

  const auto replacement = controller.on_route_notification(route_event(2, 30));
  REQUIRE(replacement.candidate.has_value());
  const auto current_candidate = *replacement.candidate;

  const auto stale_result = controller.complete_candidate_activation(
      activation(stale_candidate, AudioRouteActivationStatus::active));
  REQUIRE(stale_result.status == AudioRouteTransactionStatus::candidate_stale);
  REQUIRE(stale_result.route_generation == 0);
  REQUIRE(stale_result.output_active);
  REQUIRE(stale_result.active_device_id == AudioRouteDeviceId{10});

  const auto failed = controller.complete_candidate_activation(activation(
      current_candidate, AudioRouteActivationStatus::failed, true));
  REQUIRE(failed.status == AudioRouteTransactionStatus::activation_failed);
  REQUIRE(failed.route_generation == 0);
  REQUIRE(failed.output_active);
  REQUIRE(failed.active_device_id == AudioRouteDeviceId{10});

  const auto loss_event = controller.on_route_notification(route_event(3, 30));
  REQUIRE(loss_event.status == AudioRouteNotificationStatus::candidate_pending);
  REQUIRE(loss_event.candidate.has_value());
  const auto lost = controller.complete_candidate_activation(activation(
      *loss_event.candidate, AudioRouteActivationStatus::lost, true));
  REQUIRE(lost.status == AudioRouteTransactionStatus::candidate_lost);
  REQUIRE(lost.route_generation == 0);
  REQUIRE(lost.output_active);
  REQUIRE(lost.active_device_id == AudioRouteDeviceId{10});

  const auto no_output_event = controller.on_route_notification(route_event(
      4, 30, AudioRouteChangeKind::device_removed,
      AudioRouteDefaultRole::none));
  REQUIRE(no_output_event.status ==
          AudioRouteNotificationStatus::candidate_pending);
  REQUIRE(no_output_event.candidate.has_value());
  const auto no_output = controller.complete_candidate_activation(activation(
      *no_output_event.candidate,
      AudioRouteActivationStatus::no_active_output));
  REQUIRE(no_output.status == AudioRouteTransactionStatus::no_active_output);
  REQUIRE(no_output.route_generation == 0);
  REQUIRE(!no_output.output_active);
  REQUIRE(!no_output.active_device_id.has_value());
}

void shutdown_rejects_route_events_and_activation_results() {
  AudioRouteController controller;
  controller.shutdown();

  const auto notification = controller.on_route_notification(route_event(1, 2));
  REQUIRE(notification.status == AudioRouteNotificationStatus::ignored_after_shutdown);
  REQUIRE(notification.route_generation == 0);

  const auto transaction = controller.activate_candidate();
  REQUIRE(transaction.status == AudioRouteTransactionStatus::ignored_after_shutdown);
  REQUIRE(transaction.route_generation == 0);
}

void repeated_successful_routes_commit_exactly_once_per_candidate() {
  std::vector<AudioRouteDeviceId> activated;
  AudioRouteController controller{AudioRouteControllerConfig{
      .initial_device_id = AudioRouteDeviceId{1},
      .candidate_activator = [&](const AudioRouteCandidate &candidate) {
        activated.push_back(candidate.event.stable_device_id);
        return activation(candidate, AudioRouteActivationStatus::active);
      },
  }};

  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    const auto notification = controller.on_route_notification(
        route_event(sequence, AudioRouteDeviceId{sequence + 1}));
    REQUIRE(notification.status == AudioRouteNotificationStatus::candidate_pending);
    REQUIRE(notification.route_generation == sequence - 1);

    const auto committed = controller.activate_candidate();
    REQUIRE(committed.status == AudioRouteTransactionStatus::committed);
    REQUIRE(committed.route_generation == sequence);
    REQUIRE(committed.output_active);

    const auto repeated = controller.activate_candidate();
    REQUIRE(repeated.status == AudioRouteTransactionStatus::no_candidate);
    REQUIRE(repeated.route_generation == sequence);
  }

  REQUIRE(activated == std::vector<AudioRouteDeviceId>({2, 3, 4}));
  REQUIRE(controller.snapshot().route_generation == 3);
  REQUIRE(controller.snapshot().active_device_id == AudioRouteDeviceId{4});
}

void exact_handoff_retires_consumed_blocks_and_keeps_the_suffix() {
  const std::vector<std::uint64_t> in_flight{4, 6, 8};
  const auto result = plan_audio_route_handoff(
      AudioRouteHandoffInput{
          .expected_device_instance_id = AudioRouteDeviceId{41},
          .minimum_snapshot_sequence = 3,
          .last_device_consumed_frames = 0,
          .logical_consumed_frames = 0,
          .renderer_clock_epoch = 5,
          .clock_confidence = ClockConfidence::locked,
          .in_flight_block_frames = in_flight,
      },
      final_snapshot(41, 4, 18, 10, 8, true));

  REQUIRE(result.status == AudioRouteHandoffStatus::exact);
  REQUIRE(result.retained_block_frames == std::vector<std::uint64_t>({8}));
  REQUIRE(result.retired_block_count == 2);
  REQUIRE(result.released_block_count == 0);
  REQUIRE(result.logical_consumed_frames == 10);
  REQUIRE(result.renderer_clock_epoch == 5);
  REQUIRE(result.clock_confidence == ClockConfidence::locked);
  REQUIRE(result.route_transition);
}

void unknown_consumption_stops_at_last_provable_value_and_invalidates_clock() {
  const std::vector<std::uint64_t> in_flight{10};
  const auto result = plan_audio_route_handoff(
      AudioRouteHandoffInput{
          .expected_device_instance_id = AudioRouteDeviceId{51},
          .minimum_snapshot_sequence = 1,
          .last_device_consumed_frames = 4,
          .logical_consumed_frames = 4,
          .renderer_clock_epoch = 7,
          .clock_confidence = ClockConfidence::locked,
          .in_flight_block_frames = in_flight,
      },
      final_snapshot(51, 2, 10, 8, 2, false));

  REQUIRE(result.status == AudioRouteHandoffStatus::unknown_consumption);
  REQUIRE(result.retained_block_frames.empty());
  REQUIRE(result.released_block_count == 1);
  REQUIRE(result.logical_consumed_frames == 4);
  REQUIRE(result.renderer_clock_epoch == 8);
  REQUIRE(result.clock_confidence == ClockConfidence::invalid);
  REQUIRE(result.discontinuity_reason ==
          PlaybackCategory::route_handoff_unknown_consumption);
}

void stale_final_snapshots_are_rejected_before_handoff() {
  const std::vector<std::uint64_t> in_flight{10};
  const auto result = plan_audio_route_handoff(
      AudioRouteHandoffInput{
          .expected_device_instance_id = AudioRouteDeviceId{61},
          .minimum_snapshot_sequence = 4,
          .last_device_consumed_frames = 0,
          .logical_consumed_frames = 0,
          .renderer_clock_epoch = 0,
          .clock_confidence = ClockConfidence::provisional,
          .in_flight_block_frames = in_flight,
      },
      final_snapshot(62, 5, 10, 0, 10, true));

  REQUIRE(result.status == AudioRouteHandoffStatus::stale_snapshot);
  REQUIRE(result.retained_block_frames.empty());
  REQUIRE(result.logical_consumed_frames == 0);
  REQUIRE(result.renderer_clock_epoch == 0);
  REQUIRE(result.clock_confidence == ClockConfidence::provisional);
}

void route_transition_video_policy_has_distinct_bounded_pass_through() {
  const auto waiting = evaluate_audio_route_video_policy(
      AudioRouteVideoPolicyInput{
          .held_frame_count = 2,
          .held_duration = std::chrono::milliseconds{249},
          .stale_pre_switch_clock = true,
          .new_clock_locked = false,
          .correction_requested = true,
      });
  REQUIRE(waiting.action == AudioRouteVideoAction::hold);
  REQUIRE(waiting.route_transition);
  REQUIRE(waiting.clock_blocked);
  REQUIRE(!waiting.correction_allowed);
  REQUIRE(waiting.released_frame_count == 0);

  const auto frame_limit = evaluate_audio_route_video_policy(
      AudioRouteVideoPolicyInput{
          .held_frame_count = 3,
          .held_duration = std::chrono::milliseconds{1},
          .stale_pre_switch_clock = true,
          .new_clock_locked = false,
          .correction_requested = true,
      });
  REQUIRE(frame_limit.action == AudioRouteVideoAction::pass_through);
  REQUIRE(frame_limit.route_transition);
  REQUIRE(frame_limit.clock_blocked);
  REQUIRE(frame_limit.released_frame_count == 3);

  const auto time_limit = evaluate_audio_route_video_policy(
      AudioRouteVideoPolicyInput{
          .held_frame_count = 1,
          .held_duration = std::chrono::milliseconds{250},
          .stale_pre_switch_clock = false,
          .new_clock_locked = true,
          .correction_requested = true,
      });
  REQUIRE(time_limit.action == AudioRouteVideoAction::pass_through);
  REQUIRE(time_limit.route_transition);
  REQUIRE(time_limit.clock_blocked);
  REQUIRE(!time_limit.correction_allowed);
  REQUIRE(time_limit.released_frame_count == 1);
}

}  // namespace

int main() {
  monitor_rejects_stale_and_shutdown_notifications();
  notifications_coalesce_without_generation_change_and_reject_stale_events();
  stale_candidates_and_candidate_loss_never_commit_a_generation();
  shutdown_rejects_route_events_and_activation_results();
  repeated_successful_routes_commit_exactly_once_per_candidate();
  exact_handoff_retires_consumed_blocks_and_keeps_the_suffix();
  unknown_consumption_stops_at_last_provable_value_and_invalidates_clock();
  stale_final_snapshots_are_rejected_before_handoff();
  route_transition_video_policy_has_distinct_bounded_pass_through();
  return EXIT_SUCCESS;
}
