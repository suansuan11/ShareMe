#pragma once

#include "shareme/core/audio_output_contract.hpp"
#include "shareme/core/movie_audio_clock.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace shareme::core {

// Device identifiers are process-local route identity values. They are never
// serialized or copied into diagnostics.
using AudioRouteDeviceId = std::uint64_t;
using AudioRouteEventSequence = std::uint64_t;
using AudioRouteObservedAt = std::chrono::steady_clock::time_point;

enum class AudioRouteChangeKind : std::uint8_t {
  default_output_changed,
  device_added,
  device_removed,
  device_list_changed,
};

enum class AudioRouteDefaultRole : std::uint8_t {
  none,
  default_output,
};

struct AudioRouteEvent {
  AudioRouteEventSequence event_sequence{};
  AudioRouteDeviceId stable_device_id{};
  AudioRouteChangeKind change_kind{
      AudioRouteChangeKind::default_output_changed};
  AudioRouteDefaultRole default_role{AudioRouteDefaultRole::none};
  AudioRouteObservedAt observed_at{};
};

class AudioRouteMonitor final {
 public:
  using Callback = std::function<void(AudioRouteEvent)>;

  AudioRouteMonitor() = default;
  ~AudioRouteMonitor();

  AudioRouteMonitor(const AudioRouteMonitor &) = delete;
  AudioRouteMonitor &operator=(const AudioRouteMonitor &) = delete;
  AudioRouteMonitor(AudioRouteMonitor &&) = delete;
  AudioRouteMonitor &operator=(AudioRouteMonitor &&) = delete;

  // A monitor has one start/stop lifetime. A stopped monitor cannot be
  // restarted, which keeps late native notifications fail-closed.
  [[nodiscard]] bool start(Callback callback);
  void stop() noexcept;

  // Adapters use notify to feed value events. Delivery is synchronous and
  // bounded by the callback; stale and post-stop events are ignored.
  [[nodiscard]] bool notify(AudioRouteEvent event);
  [[nodiscard]] bool accepting() const noexcept;

 private:
  void complete_callback() noexcept;

  mutable std::mutex mutex_;
  std::condition_variable callback_finished_;
  Callback callback_;
  AudioRouteEventSequence last_event_sequence_{};
  std::size_t callbacks_in_flight_{};
  bool has_last_event_sequence_{false};
  bool started_{false};
  bool stopped_{false};
};

struct AudioRouteCandidate {
  AudioRouteEvent event{};
};

enum class AudioRouteNotificationStatus : std::uint8_t {
  candidate_pending,
  coalesced,
  stale,
  ignored_after_shutdown,
};

struct AudioRouteNotificationResult {
  AudioRouteNotificationStatus status{
      AudioRouteNotificationStatus::ignored_after_shutdown};
  std::optional<AudioRouteCandidate> candidate{};
  std::uint64_t route_generation{};
};

enum class AudioRouteActivationStatus : std::uint8_t {
  active,
  failed,
  lost,
  no_active_output,
};

struct AudioRouteActivationResult {
  AudioRouteCandidate candidate{};
  AudioRouteActivationStatus status{AudioRouteActivationStatus::failed};
  bool old_route_resumed{false};
};

using AudioRouteCandidateActivator =
    std::function<AudioRouteActivationResult(const AudioRouteCandidate &)>;

struct AudioRouteControllerConfig {
  std::optional<AudioRouteDeviceId> initial_device_id{};
  AudioRouteCandidateActivator candidate_activator{};
};

enum class AudioRouteTransactionStatus : std::uint8_t {
  committed,
  no_candidate,
  candidate_stale,
  activation_failed,
  candidate_lost,
  no_active_output,
  ignored_after_shutdown,
};

struct AudioRouteTransactionResult {
  AudioRouteTransactionStatus status{
      AudioRouteTransactionStatus::no_candidate};
  std::uint64_t route_generation{};
  std::optional<AudioRouteDeviceId> active_device_id{};
  bool output_active{false};
  bool route_transition{false};
};

struct AudioRouteControllerSnapshot {
  std::uint64_t route_generation{};
  AudioRouteEventSequence last_event_sequence{};
  std::optional<AudioRouteDeviceId> active_device_id{};
  std::optional<AudioRouteCandidate> pending_candidate{};
  bool output_active{false};
  bool route_transition{false};
  bool accepting_notifications{true};
};

class AudioRouteController final {
 public:
  explicit AudioRouteController(AudioRouteControllerConfig config = {});

  [[nodiscard]] AudioRouteNotificationResult on_route_notification(
      AudioRouteEvent event);

  // The no-argument operation invokes the value-only activator supplied in
  // AudioRouteControllerConfig. Platform/device activation remains outside
  // this portable policy.
  [[nodiscard]] AudioRouteTransactionResult activate_candidate();

  // Tests and later renderer integrations may submit an activation result
  // directly. The event sequence and stable device identity are checked
  // before any generation commit.
  [[nodiscard]] AudioRouteTransactionResult complete_candidate_activation(
      AudioRouteActivationResult result);

  void shutdown() noexcept;

  [[nodiscard]] std::optional<AudioRouteCandidate> pending_candidate()
      const noexcept;
  [[nodiscard]] AudioRouteControllerSnapshot snapshot() const noexcept;

 private:
  [[nodiscard]] AudioRouteTransactionResult transaction_result(
      AudioRouteTransactionStatus status, bool route_transition) const noexcept;

  AudioRouteCandidateActivator candidate_activator_;
  std::optional<AudioRouteDeviceId> active_device_id_;
  std::optional<AudioRouteCandidate> pending_candidate_;
  AudioRouteEventSequence last_event_sequence_{};
  std::uint64_t route_generation_{};
  bool has_last_event_sequence_{false};
  bool output_active_{false};
  bool route_transition_{false};
  bool accepting_notifications_{true};
};

enum class AudioRouteHandoffStatus : std::uint8_t {
  exact,
  unknown_consumption,
  stale_snapshot,
  invalid_snapshot,
};

struct AudioRouteHandoffInput {
  std::optional<AudioRouteDeviceId> expected_device_instance_id{};
  std::optional<std::uint64_t> minimum_snapshot_sequence{};
  std::uint64_t last_device_consumed_frames{};
  std::uint64_t logical_consumed_frames{};
  std::uint64_t renderer_clock_epoch{};
  ClockConfidence clock_confidence{ClockConfidence::unavailable};
  std::span<const std::uint64_t> in_flight_block_frames{};
};

struct AudioRouteHandoffResult {
  AudioRouteHandoffStatus status{AudioRouteHandoffStatus::invalid_snapshot};
  std::vector<std::uint64_t> retained_block_frames;
  std::uint64_t retired_block_count{};
  std::uint64_t released_block_count{};
  std::uint64_t logical_consumed_frames{};
  std::uint64_t renderer_clock_epoch{};
  ClockConfidence clock_confidence{ClockConfidence::unavailable};
  std::optional<PlaybackCategory> discontinuity_reason{};
  bool route_transition{true};
};

[[nodiscard]] AudioRouteHandoffResult plan_audio_route_handoff(
    AudioRouteHandoffInput input, FinalDeviceSnapshot final_snapshot);

inline constexpr std::size_t kAudioRouteMaximumHeldFrames = 3;
inline constexpr std::chrono::milliseconds kAudioRouteMaximumHoldDuration{
    250};

enum class AudioRouteVideoAction : std::uint8_t {
  hold,
  pass_through,
};

struct AudioRouteVideoPolicyInput {
  std::size_t held_frame_count{};
  std::chrono::milliseconds held_duration{};
  bool stale_pre_switch_clock{false};
  bool new_clock_locked{false};
  bool correction_requested{false};
};

struct AudioRouteVideoPolicyResult {
  AudioRouteVideoAction action{AudioRouteVideoAction::pass_through};
  std::size_t released_frame_count{};
  bool route_transition{true};
  bool clock_blocked{true};
  bool correction_allowed{false};
};

[[nodiscard]] constexpr AudioRouteVideoPolicyResult
evaluate_audio_route_video_policy(AudioRouteVideoPolicyInput input) noexcept {
  const bool bound_reached =
      input.held_frame_count >= kAudioRouteMaximumHeldFrames ||
      input.held_duration >= kAudioRouteMaximumHoldDuration;
  return AudioRouteVideoPolicyResult{
      .action = bound_reached ? AudioRouteVideoAction::pass_through
                              : AudioRouteVideoAction::hold,
      .released_frame_count = bound_reached ? input.held_frame_count : 0,
      .route_transition = true,
      .clock_blocked = input.stale_pre_switch_clock || !input.new_clock_locked ||
          bound_reached,
      .correction_allowed = input.correction_requested &&
          input.new_clock_locked && !input.stale_pre_switch_clock &&
          !bound_reached,
  };
}

}  // namespace shareme::core
