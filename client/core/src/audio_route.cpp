#include "shareme/core/audio_route.hpp"

#include <limits>

namespace shareme::core {
namespace {

thread_local AudioRouteMonitor *active_monitor_callback = nullptr;

[[nodiscard]] bool same_candidate(const AudioRouteCandidate &left,
                                  const AudioRouteCandidate &right) noexcept {
  return left.event.event_sequence == right.event.event_sequence &&
      left.event.stable_device_id == right.event.stable_device_id;
}

[[nodiscard]] AudioRouteTransactionResult make_transaction_result(
    AudioRouteTransactionStatus status, std::uint64_t route_generation,
    std::optional<AudioRouteDeviceId> active_device_id, bool output_active,
    bool route_transition) noexcept {
  return AudioRouteTransactionResult{
      .status = status,
      .route_generation = route_generation,
      .active_device_id = active_device_id,
      .output_active = output_active,
      .route_transition = route_transition,
  };
}

[[nodiscard]] AudioRouteHandoffResult handoff_state(
    const AudioRouteHandoffInput &input,
    AudioRouteHandoffStatus status) {
  return AudioRouteHandoffResult{
      .status = status,
      .retained_block_frames = {},
      .retired_block_count = 0,
      .released_block_count = 0,
      .logical_consumed_frames = input.logical_consumed_frames,
      .renderer_clock_epoch = input.renderer_clock_epoch,
      .clock_confidence = input.clock_confidence,
      .discontinuity_reason = std::nullopt,
      .route_transition = true,
  };
}

[[nodiscard]] bool checked_sum(std::span<const std::uint64_t> values,
                               std::uint64_t &sum) noexcept {
  sum = 0;
  for (const auto value : values) {
    if (value > std::numeric_limits<std::uint64_t>::max() - sum)
      return false;
    sum += value;
  }
  return true;
}

}  // namespace

AudioRouteMonitor::~AudioRouteMonitor() { stop(); }

bool AudioRouteMonitor::start(Callback callback) {
  if (!callback)
    return false;

  std::lock_guard lock{mutex_};
  if (started_ || stopped_)
    return false;
  callback_ = std::move(callback);
  started_ = true;
  return true;
}

void AudioRouteMonitor::stop() noexcept {
  std::unique_lock lock{mutex_};
  stopped_ = true;
  started_ = false;
  callback_ = {};

  // A callback may close its own monitor. Waiting for that callback would
  // deadlock; it will decrement the in-flight count before returning.
  const std::size_t current_callback_count =
      active_monitor_callback == this ? 1U : 0U;
  callback_finished_.wait(lock, [this, current_callback_count] {
    return callbacks_in_flight_ <= current_callback_count;
  });
}

bool AudioRouteMonitor::notify(AudioRouteEvent event) {
  Callback callback;
  {
    std::lock_guard lock{mutex_};
    if (!started_ || stopped_ || !callback_)
      return false;
    if (has_last_event_sequence_ &&
        event.event_sequence <= last_event_sequence_)
      return false;

    last_event_sequence_ = event.event_sequence;
    has_last_event_sequence_ = true;
    ++callbacks_in_flight_;
    callback = callback_;
  }

  const auto previous_monitor = active_monitor_callback;
  active_monitor_callback = this;
  try {
    callback(event);
  } catch (...) {
    active_monitor_callback = previous_monitor;
    complete_callback();
    throw;
  }
  active_monitor_callback = previous_monitor;
  complete_callback();
  return true;
}

bool AudioRouteMonitor::accepting() const noexcept {
  std::lock_guard lock{mutex_};
  return started_ && !stopped_ && static_cast<bool>(callback_);
}

void AudioRouteMonitor::complete_callback() noexcept {
  std::lock_guard lock{mutex_};
  if (callbacks_in_flight_ != 0)
    --callbacks_in_flight_;
  if (callbacks_in_flight_ == 0)
    callback_finished_.notify_all();
}

AudioRouteController::AudioRouteController(AudioRouteControllerConfig config)
    : candidate_activator_{std::move(config.candidate_activator)},
      active_device_id_{config.initial_device_id},
      output_active_{config.initial_device_id.has_value()} {}

AudioRouteNotificationResult AudioRouteController::on_route_notification(
    AudioRouteEvent event) {
  if (!accepting_notifications_)
    return AudioRouteNotificationResult{
        .status = AudioRouteNotificationStatus::ignored_after_shutdown,
        .candidate = std::nullopt,
        .route_generation = route_generation_,
    };

  if (has_last_event_sequence_ &&
      event.event_sequence <= last_event_sequence_) {
    return AudioRouteNotificationResult{
        .status = AudioRouteNotificationStatus::stale,
        .candidate = pending_candidate_,
        .route_generation = route_generation_,
    };
  }

  last_event_sequence_ = event.event_sequence;
  has_last_event_sequence_ = true;

  const auto removes_active_output =
      event.change_kind == AudioRouteChangeKind::device_removed &&
      active_device_id_.has_value() &&
      event.stable_device_id == *active_device_id_;
  const auto needs_candidate = !active_device_id_.has_value() ||
      event.stable_device_id != *active_device_id_ || removes_active_output;

  if (!needs_candidate) {
    pending_candidate_.reset();
    route_transition_ = false;
    return AudioRouteNotificationResult{
        .status = AudioRouteNotificationStatus::coalesced,
        .candidate = std::nullopt,
        .route_generation = route_generation_,
    };
  }

  const AudioRouteCandidate candidate{.event = event};
  if (pending_candidate_.has_value() &&
      pending_candidate_->event.stable_device_id == event.stable_device_id &&
      pending_candidate_->event.change_kind !=
          AudioRouteChangeKind::device_removed &&
      event.change_kind != AudioRouteChangeKind::device_removed) {
    pending_candidate_ = candidate;
    route_transition_ = true;
    return AudioRouteNotificationResult{
        .status = AudioRouteNotificationStatus::coalesced,
        .candidate = pending_candidate_,
        .route_generation = route_generation_,
    };
  }

  pending_candidate_ = candidate;
  route_transition_ = true;
  return AudioRouteNotificationResult{
      .status = AudioRouteNotificationStatus::candidate_pending,
      .candidate = pending_candidate_,
      .route_generation = route_generation_,
  };
}

AudioRouteTransactionResult AudioRouteController::activate_candidate() {
  if (!accepting_notifications_)
    return transaction_result(
        AudioRouteTransactionStatus::ignored_after_shutdown, false);
  if (!pending_candidate_)
    return transaction_result(AudioRouteTransactionStatus::no_candidate, false);

  const auto candidate = *pending_candidate_;
  AudioRouteActivationResult result{
      .candidate = candidate,
      .status = AudioRouteActivationStatus::failed,
      .old_route_resumed = output_active_,
  };
  if (candidate_activator_) {
    try {
      result = candidate_activator_(candidate);
    } catch (...) {
      result = AudioRouteActivationResult{
          .candidate = candidate,
          .status = AudioRouteActivationStatus::failed,
          .old_route_resumed = output_active_,
      };
    }
  }
  return complete_candidate_activation(std::move(result));
}

AudioRouteTransactionResult AudioRouteController::complete_candidate_activation(
    AudioRouteActivationResult result) {
  if (!accepting_notifications_)
    return transaction_result(
        AudioRouteTransactionStatus::ignored_after_shutdown, false);
  if (!pending_candidate_)
    return transaction_result(AudioRouteTransactionStatus::no_candidate, false);

  if (!same_candidate(*pending_candidate_, result.candidate))
    return transaction_result(AudioRouteTransactionStatus::candidate_stale,
                              true);

  const auto candidate = *pending_candidate_;
  switch (result.status) {
    case AudioRouteActivationStatus::active:
      if (route_generation_ == std::numeric_limits<std::uint64_t>::max()) {
        pending_candidate_.reset();
        output_active_ = result.old_route_resumed && active_device_id_.has_value();
        if (!output_active_)
          active_device_id_.reset();
        return transaction_result(AudioRouteTransactionStatus::activation_failed,
                                  true);
      }
      pending_candidate_.reset();
      active_device_id_ = candidate.event.stable_device_id;
      output_active_ = true;
      route_transition_ = false;
      ++route_generation_;
      return transaction_result(AudioRouteTransactionStatus::committed, true);

    case AudioRouteActivationStatus::failed:
      pending_candidate_.reset();
      output_active_ = result.old_route_resumed && active_device_id_.has_value();
      if (!output_active_)
        active_device_id_.reset();
      route_transition_ = false;
      return transaction_result(AudioRouteTransactionStatus::activation_failed,
                                true);

    case AudioRouteActivationStatus::lost:
      pending_candidate_.reset();
      output_active_ = result.old_route_resumed && active_device_id_.has_value();
      if (!output_active_)
        active_device_id_.reset();
      route_transition_ = false;
      return transaction_result(AudioRouteTransactionStatus::candidate_lost,
                                true);

    case AudioRouteActivationStatus::no_active_output:
      pending_candidate_.reset();
      output_active_ = false;
      active_device_id_.reset();
      route_transition_ = false;
      return transaction_result(AudioRouteTransactionStatus::no_active_output,
                                true);
  }

  pending_candidate_.reset();
  output_active_ = false;
  active_device_id_.reset();
  route_transition_ = false;
  return transaction_result(AudioRouteTransactionStatus::activation_failed,
                            true);
}

void AudioRouteController::shutdown() noexcept {
  accepting_notifications_ = false;
  pending_candidate_.reset();
  route_transition_ = false;
  candidate_activator_ = {};
}

std::optional<AudioRouteCandidate> AudioRouteController::pending_candidate()
    const noexcept {
  return pending_candidate_;
}

AudioRouteControllerSnapshot AudioRouteController::snapshot() const noexcept {
  return AudioRouteControllerSnapshot{
      .route_generation = route_generation_,
      .last_event_sequence = last_event_sequence_,
      .active_device_id = active_device_id_,
      .pending_candidate = pending_candidate_,
      .output_active = output_active_,
      .route_transition = route_transition_,
      .accepting_notifications = accepting_notifications_,
  };
}

AudioRouteTransactionResult AudioRouteController::transaction_result(
    AudioRouteTransactionStatus status, bool route_transition) const noexcept {
  return make_transaction_result(status, route_generation_, active_device_id_,
                                 output_active_, route_transition);
}

AudioRouteHandoffResult plan_audio_route_handoff(
    AudioRouteHandoffInput input, FinalDeviceSnapshot final_snapshot) {
  if ((input.expected_device_instance_id.has_value() &&
       final_snapshot.device_instance_id != *input.expected_device_instance_id) ||
      (input.minimum_snapshot_sequence.has_value() &&
       final_snapshot.snapshot_sequence <= *input.minimum_snapshot_sequence)) {
    return handoff_state(input, AudioRouteHandoffStatus::stale_snapshot);
  }

  if (!is_valid_final_device_snapshot(final_snapshot) || final_snapshot.active)
    return handoff_state(input, AudioRouteHandoffStatus::invalid_snapshot);

  std::uint64_t total_in_flight_frames = 0;
  if (!checked_sum(input.in_flight_block_frames, total_in_flight_frames) ||
      final_snapshot.device_queue_frames > total_in_flight_frames) {
    return handoff_state(input, AudioRouteHandoffStatus::invalid_snapshot);
  }

  if (!final_snapshot.exact_consumption) {
    auto result = handoff_state(input, AudioRouteHandoffStatus::unknown_consumption);
    result.released_block_count = input.in_flight_block_frames.size();
    result.clock_confidence = ClockConfidence::invalid;
    if (result.renderer_clock_epoch != std::numeric_limits<std::uint64_t>::max())
      ++result.renderer_clock_epoch;
    result.discontinuity_reason =
        PlaybackCategory::route_handoff_unknown_consumption;
    return result;
  }

  if (final_snapshot.device_consumed_frames_total <
          input.last_device_consumed_frames ||
      final_snapshot.device_consumed_frames_total -
              input.last_device_consumed_frames !=
          total_in_flight_frames - final_snapshot.device_queue_frames) {
    return handoff_state(input, AudioRouteHandoffStatus::invalid_snapshot);
  }

  const auto consumed_in_flight_frames =
      total_in_flight_frames - final_snapshot.device_queue_frames;
  if (input.logical_consumed_frames >
      std::numeric_limits<std::uint64_t>::max() - consumed_in_flight_frames) {
    return handoff_state(input, AudioRouteHandoffStatus::invalid_snapshot);
  }

  auto result = handoff_state(input, AudioRouteHandoffStatus::exact);
  result.logical_consumed_frames =
      input.logical_consumed_frames + consumed_in_flight_frames;

  std::uint64_t remaining_consumed = consumed_in_flight_frames;
  for (const auto block_frames : input.in_flight_block_frames) {
    if (remaining_consumed >= block_frames) {
      remaining_consumed -= block_frames;
      ++result.retired_block_count;
      continue;
    }

    if (remaining_consumed != 0) {
      result.retained_block_frames.push_back(block_frames - remaining_consumed);
      remaining_consumed = 0;
    } else {
      result.retained_block_frames.push_back(block_frames);
    }
  }

  if (remaining_consumed != 0)
    return handoff_state(input, AudioRouteHandoffStatus::invalid_snapshot);
  return result;
}

}  // namespace shareme::core
