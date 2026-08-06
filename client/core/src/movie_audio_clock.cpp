#include "shareme/core/movie_audio_clock.hpp"

#include <limits>

namespace shareme::core {
namespace {

constexpr std::int64_t kMaximumCorrelationResidualMs = 80;

[[nodiscard]] std::uint64_t negative_magnitude(std::int64_t value) noexcept {
  return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

[[nodiscard]] bool checked_add_unsigned(
    std::int64_t value, std::uint64_t amount,
    std::int64_t& result) noexcept {
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  if (value >= 0) {
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    if (amount > static_cast<std::uint64_t>(max) - unsigned_value) {
      return false;
    }
    result = static_cast<std::int64_t>(unsigned_value + amount);
    return true;
  }

  const auto magnitude = negative_magnitude(value);
  if (amount <= magnitude) {
    const auto remaining = magnitude - amount;
    if (remaining == static_cast<std::uint64_t>(max) + 1U) {
      result = std::numeric_limits<std::int64_t>::min();
    } else {
      result = -static_cast<std::int64_t>(remaining);
    }
    return true;
  }

  const auto positive_result = amount - magnitude;
  if (positive_result > static_cast<std::uint64_t>(max)) {
    return false;
  }
  result = static_cast<std::int64_t>(positive_result);
  return true;
}

[[nodiscard]] bool checked_subtract_unsigned(
    std::int64_t value, std::uint64_t amount,
    std::int64_t& result) noexcept {
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  if (value >= 0) {
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    const auto lower_bound_distance =
        unsigned_value + static_cast<std::uint64_t>(max) + 1U;
    if (amount > lower_bound_distance) {
      return false;
    }
    if (amount <= static_cast<std::uint64_t>(max)) {
      result = value - static_cast<std::int64_t>(amount);
    } else {
      const auto negative_result = amount - unsigned_value;
      if (negative_result == static_cast<std::uint64_t>(max) + 1U) {
        result = std::numeric_limits<std::int64_t>::min();
      } else {
        result = -static_cast<std::int64_t>(negative_result);
      }
    }
    return true;
  }

  const auto magnitude = negative_magnitude(value);
  const auto lower_bound_magnitude = static_cast<std::uint64_t>(max) + 1U;
  if (amount > lower_bound_magnitude - magnitude) {
    return false;
  }
  const auto result_magnitude = magnitude + amount;
  if (result_magnitude == lower_bound_magnitude) {
    result = std::numeric_limits<std::int64_t>::min();
  } else {
    result = -static_cast<std::int64_t>(result_magnitude);
  }
  return true;
}

[[nodiscard]] std::optional<std::int64_t> checked_frame_duration_ms(
    std::uint64_t frames, std::uint32_t sample_rate) noexcept {
  const auto duration_ms = audio_duration_ms(frames, sample_rate);
  if (!duration_ms.has_value() ||
      *duration_ms >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*duration_ms);
}

[[nodiscard]] std::uint64_t magnitude(std::int64_t value) noexcept {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }
  return negative_magnitude(value);
}

struct CorrelationCheck {
  bool supplied = false;
  bool valid = false;
};

[[nodiscard]] CorrelationCheck validate_correlation(
    const CorrelationResult& correlation, const AudioClockAnchor& anchor,
    std::uint64_t playback_generation, std::uint64_t audio_epoch,
    std::uint32_t sample_rate, bool requires_relock,
    const std::optional<std::uint64_t>& relock_floor_source,
    const std::optional<std::uint64_t>& relock_floor_decoded) noexcept {
  CorrelationCheck check{true, false};
  if (!correlation.valid ||
      correlation.reason != CorrelationRejectionReason::none ||
      !is_approved_correlation_provenance(correlation.provenance) ||
      !correlation.anchor_control_sequence.has_value() ||
      !correlation.source_sequence.has_value() ||
      !correlation.decoded_sequence.has_value() ||
      !correlation.playback_generation.has_value() ||
      !correlation.audio_epoch.has_value() ||
      !correlation.sample_rate.has_value() ||
      !correlation.channel_count.has_value()) {
    return check;
  }
  if (*correlation.anchor_control_sequence != anchor.control_sequence ||
      *correlation.source_sequence != anchor.host_source_sequence ||
      *correlation.playback_generation != playback_generation ||
      *correlation.audio_epoch != audio_epoch ||
      *correlation.sample_rate != sample_rate ||
      *correlation.sample_rate != anchor.sample_rate ||
      *correlation.channel_count != anchor.channel_count ||
      anchor.channel_count == 0 ||
      magnitude(correlation.residual_ms) >
          static_cast<std::uint64_t>(kMaximumCorrelationResidualMs)) {
    return check;
  }
  if (requires_relock &&
      (relock_floor_source.has_value() != relock_floor_decoded.has_value() ||
       (relock_floor_source.has_value() &&
        (*correlation.source_sequence <= *relock_floor_source ||
         *correlation.decoded_sequence <= *relock_floor_decoded)))) {
      return check;
  }
  check.valid = true;
  return check;
}

}  // namespace

MovieAudioClockSnapshot MovieAudioClock::observe(
    AudioClockObservation observation,
    MonotonicTime monotonic_now) noexcept {
  if (have_observation_time_ && monotonic_now < last_observation_time_) {
    mark_invalid();
    return snapshot_;
  }

  const auto previous = *this;
  last_observation_time_ = monotonic_now;
  have_observation_time_ = true;

  if (observation.sample_rate == 0) {
    mark_invalid();
    return snapshot_;
  }
  if (renderer_clock_epoch_overflowed_) {
    consumption_unknown_ = true;
    requires_relock_ = true;
    snapshot_.output_latency_frames = observation.output_latency_frames;
    snapshot_.logical_consumed_frames = logical_consumed_frames_;
    snapshot_.playback_generation = playback_generation_;
    snapshot_.host_audio_epoch = host_audio_epoch_;
    snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
    snapshot_.route_generation = route_generation_;
    snapshot_.consumption_known = false;
    set_confidence(ClockConfidence::invalid);
    return snapshot_;
  }

  const auto reject = [&]() noexcept {
    *this = previous;
    last_observation_time_ = monotonic_now;
    have_observation_time_ = true;
    mark_invalid();
    return snapshot_;
  };

  const bool was_unknown = consumption_unknown_;
  bool media_scope_changed = false;

  if (!initialized_) {
    initialized_ = true;
    playback_generation_ = observation.playback_generation;
    host_audio_epoch_ = observation.host_audio_epoch;
    route_generation_ = observation.route_generation;
    renderer_clock_epoch_ = observation.renderer_clock_epoch;
    logical_consumed_frames_ = 0;
    sample_rate_ = observation.sample_rate;
  } else {
    if (observation.playback_generation < playback_generation_ ||
        observation.host_audio_epoch < host_audio_epoch_ ||
        observation.route_generation < route_generation_) {
      return reject();
    }
    if (observation.renderer_clock_epoch < renderer_clock_epoch_ &&
        !renderer_clock_epoch_locally_advanced_) {
      return reject();
    }
    if (observation.renderer_clock_epoch > renderer_clock_epoch_) {
      renderer_clock_epoch_ = observation.renderer_clock_epoch;
      renderer_clock_epoch_locally_advanced_ = false;
      begin_relock();
    } else if (observation.renderer_clock_epoch == renderer_clock_epoch_) {
      renderer_clock_epoch_locally_advanced_ = false;
    }

    if (observation.playback_generation > playback_generation_) {
      playback_generation_ = observation.playback_generation;
      media_scope_changed = true;
    }
    if (observation.host_audio_epoch > host_audio_epoch_) {
      host_audio_epoch_ = observation.host_audio_epoch;
      media_scope_changed = true;
    }
    if (observation.route_generation > route_generation_) {
      route_generation_ = observation.route_generation;
      begin_relock();
    }
    if (observation.sample_rate != sample_rate_) {
      return reject();
    }
    if (media_scope_changed) {
      anchor_.reset();
      last_correlation_.reset();
      relock_floor_.reset();
      relock_correlation_ready_ = false;
      requires_relock_ = true;
      have_playout_pts_ = false;
    }
  }

  if (!observation.consumption_known) {
    if (!consumption_unknown_) {
      begin_relock();
      if (!increment_renderer_clock_epoch()) {
        consumption_unknown_ = true;
        renderer_clock_epoch_overflowed_ = true;
      } else {
        consumption_unknown_ = true;
      }
    }
    snapshot_.output_latency_frames = observation.output_latency_frames;
    snapshot_.logical_consumed_frames = logical_consumed_frames_;
    snapshot_.playback_generation = playback_generation_;
    snapshot_.host_audio_epoch = host_audio_epoch_;
    snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
    snapshot_.route_generation = route_generation_;
    snapshot_.consumption_known = false;
    snapshot_.has_playout_pts = have_playout_pts_;
    if (have_playout_pts_) {
      snapshot_.estimated_playout_pts_ms = last_playout_pts_ms_;
      snapshot_.audio_playout_pts_ms = last_playout_pts_ms_;
      snapshot_.playout_pts_ms = last_playout_pts_ms_;
    }
    set_confidence(ClockConfidence::invalid);
    return snapshot_;
  }

  if (renderer_clock_epoch_overflowed_) {
    consumption_unknown_ = true;
    requires_relock_ = true;
    snapshot_.output_latency_frames = observation.output_latency_frames;
    snapshot_.logical_consumed_frames = logical_consumed_frames_;
    snapshot_.playback_generation = playback_generation_;
    snapshot_.host_audio_epoch = host_audio_epoch_;
    snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
    snapshot_.route_generation = route_generation_;
    snapshot_.consumption_known = false;
    set_confidence(ClockConfidence::invalid);
    return snapshot_;
  }

  if (observation.logical_consumed_frames < logical_consumed_frames_) {
    return reject();
  }
  logical_consumed_frames_ = observation.logical_consumed_frames;

  if (observation.anchor.has_value()) {
    const auto& candidate_anchor = *observation.anchor;
    if (candidate_anchor.playback_generation != playback_generation_ ||
        candidate_anchor.audio_epoch != host_audio_epoch_ ||
        candidate_anchor.sample_rate != sample_rate_ ||
        candidate_anchor.channel_count == 0 ||
        candidate_anchor.consumed_frames > logical_consumed_frames_) {
      return reject();
    }
    if (anchor_.has_value() && !media_scope_changed &&
        (candidate_anchor.control_sequence <= anchor_->control_sequence ||
         candidate_anchor.host_source_sequence <
             anchor_->host_source_sequence)) {
      return reject();
    }
    if (anchor_.has_value() && !media_scope_changed &&
        candidate_anchor.control_sequence != anchor_->control_sequence) {
      begin_relock();
    }
    anchor_ = candidate_anchor;
  }

  std::optional<std::int64_t> calculated_pts_ms;
  if (anchor_.has_value()) {
    if (logical_consumed_frames_ < anchor_->consumed_frames) {
      return reject();
    }
    const auto elapsed_frames =
        logical_consumed_frames_ - anchor_->consumed_frames;
    const auto elapsed_ms =
        checked_frame_duration_ms(elapsed_frames, sample_rate_);
    if (!elapsed_ms.has_value()) {
      return reject();
    }

    auto pts_ms = std::int64_t{0};
    if (!checked_add_unsigned(anchor_->media_pts_ms,
                              static_cast<std::uint64_t>(*elapsed_ms),
                              pts_ms)) {
      return reject();
    }
    if (observation.output_latency_frames.has_value()) {
      const auto latency_ms = checked_frame_duration_ms(
          *observation.output_latency_frames, sample_rate_);
      if (!latency_ms.has_value() ||
          !checked_subtract_unsigned(pts_ms,
                                     static_cast<std::uint64_t>(*latency_ms),
                                     pts_ms)) {
        return reject();
      }
    }
    calculated_pts_ms = pts_ms;
  }

  if (calculated_pts_ms.has_value()) {
    const bool new_pts_epoch =
        !have_playout_pts_ ||
        pts_playback_generation_ != playback_generation_ ||
        pts_host_audio_epoch_ != host_audio_epoch_ ||
        pts_renderer_clock_epoch_ != renderer_clock_epoch_;
    if (!new_pts_epoch && *calculated_pts_ms < last_playout_pts_ms_) {
      return reject();
    }
    last_playout_pts_ms_ = *calculated_pts_ms;
    pts_playback_generation_ = playback_generation_;
    pts_host_audio_epoch_ = host_audio_epoch_;
    pts_renderer_clock_epoch_ = renderer_clock_epoch_;
    have_playout_pts_ = true;
  }

  CorrelationCheck correlation_check;
  if (observation.correlation.has_value()) {
    if (!anchor_.has_value()) {
      correlation_check.supplied = true;
    } else {
      correlation_check = validate_correlation(
          *observation.correlation, *anchor_, playback_generation_,
          host_audio_epoch_, sample_rate_, requires_relock_,
          relock_floor_.has_value()
              ? std::optional<std::uint64_t>{
                    relock_floor_->source_sequence}
              : std::nullopt,
          relock_floor_.has_value()
              ? std::optional<std::uint64_t>{
                    relock_floor_->decoded_sequence}
              : std::nullopt);
    }
    if (was_unknown && !correlation_check.valid) {
      return reject();
    }
    if (correlation_check.valid) {
      last_correlation_ = observation.correlation;
      if (requires_relock_) {
        relock_correlation_ready_ = true;
      }
    }
  }

  consumption_unknown_ = false;
  snapshot_.logical_consumed_frames = logical_consumed_frames_;
  snapshot_.output_latency_frames = observation.output_latency_frames;
  snapshot_.playback_generation = playback_generation_;
  snapshot_.host_audio_epoch = host_audio_epoch_;
  snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
  snapshot_.route_generation = route_generation_;
  snapshot_.consumption_known = true;
  snapshot_.has_playout_pts = have_playout_pts_;
  if (have_playout_pts_) {
    snapshot_.estimated_playout_pts_ms = last_playout_pts_ms_;
    snapshot_.audio_playout_pts_ms = last_playout_pts_ms_;
    snapshot_.playout_pts_ms = last_playout_pts_ms_;
  }

  const bool was_locked = snapshot_.confidence == ClockConfidence::locked;
  const bool was_degraded = snapshot_.confidence == ClockConfidence::degraded;
  const bool supplied_invalid_correlation =
      correlation_check.supplied && !correlation_check.valid;
  if (observation.discontinuity || observation.underrun ||
      supplied_invalid_correlation) {
    set_confidence(ClockConfidence::degraded);
  } else if (was_degraded && !observation.correlation.has_value()) {
    set_confidence(ClockConfidence::degraded);
  } else if (!have_playout_pts_) {
    set_confidence(ClockConfidence::unavailable);
  } else if (observation.output_latency_frames.has_value() &&
             (correlation_check.valid || relock_correlation_ready_ ||
              (last_correlation_.has_value() && !requires_relock_) ||
              (was_locked && !requires_relock_))) {
    set_confidence(ClockConfidence::locked);
    requires_relock_ = false;
    relock_correlation_ready_ = false;
  } else if (was_locked && !requires_relock_ &&
             observation.output_latency_frames.has_value()) {
    set_confidence(ClockConfidence::locked);
  } else {
    set_confidence(ClockConfidence::provisional);
  }

  return snapshot_;
}

MovieAudioClockSnapshot MovieAudioClock::snapshot() const noexcept {
  return snapshot_;
}

void MovieAudioClock::clear_playback_anchor() noexcept {
  anchor_.reset();
  last_correlation_.reset();
  relock_floor_.reset();
  relock_correlation_ready_ = false;
  requires_relock_ = false;
  have_playout_pts_ = false;
  last_playout_pts_ms_ = 0;
  pts_playback_generation_ = 0;
  pts_host_audio_epoch_ = 0;
  pts_renderer_clock_epoch_ = 0;
  snapshot_.estimated_playout_pts_ms = 0;
  snapshot_.audio_playout_pts_ms = 0;
  snapshot_.playout_pts_ms = 0;
  snapshot_.has_playout_pts = false;
  set_confidence(consumption_unknown_ ? ClockConfidence::invalid
                                      : ClockConfidence::unavailable);
}

void MovieAudioClock::reset() noexcept {
  snapshot_ = {};
  initialized_ = false;
  consumption_unknown_ = false;
  renderer_clock_epoch_locally_advanced_ = false;
  renderer_clock_epoch_overflowed_ = false;
  requires_relock_ = false;
  relock_correlation_ready_ = false;
  have_observation_time_ = false;
  last_observation_time_ = {};
  logical_consumed_frames_ = 0;
  renderer_clock_epoch_ = 0;
  playback_generation_ = 0;
  host_audio_epoch_ = 0;
  route_generation_ = 0;
  sample_rate_ = 0;
  anchor_.reset();
  last_correlation_.reset();
  relock_floor_.reset();
  have_playout_pts_ = false;
  last_playout_pts_ms_ = 0;
  pts_playback_generation_ = 0;
  pts_host_audio_epoch_ = 0;
  pts_renderer_clock_epoch_ = 0;
}

void MovieAudioClock::set_confidence(ClockConfidence confidence) noexcept {
  snapshot_.confidence = confidence;
  snapshot_.clock_confidence = confidence;
}

void MovieAudioClock::begin_relock() noexcept {
  if (last_correlation_.has_value() &&
      last_correlation_->source_sequence.has_value() &&
      last_correlation_->decoded_sequence.has_value()) {
    relock_floor_ = CorrelationSequence{
        *last_correlation_->source_sequence,
        *last_correlation_->decoded_sequence,
    };
  }
  last_correlation_.reset();
  relock_correlation_ready_ = false;
  requires_relock_ = true;
}

void MovieAudioClock::mark_invalid() noexcept {
  begin_relock();
  set_confidence(ClockConfidence::invalid);
  snapshot_.logical_consumed_frames = logical_consumed_frames_;
  snapshot_.playback_generation = playback_generation_;
  snapshot_.host_audio_epoch = host_audio_epoch_;
  snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
  snapshot_.route_generation = route_generation_;
  snapshot_.consumption_known = false;
  snapshot_.has_playout_pts = have_playout_pts_;
  if (have_playout_pts_) {
    snapshot_.estimated_playout_pts_ms = last_playout_pts_ms_;
    snapshot_.audio_playout_pts_ms = last_playout_pts_ms_;
    snapshot_.playout_pts_ms = last_playout_pts_ms_;
  }
}

bool MovieAudioClock::increment_renderer_clock_epoch() noexcept {
  if (renderer_clock_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  ++renderer_clock_epoch_;
  renderer_clock_epoch_locally_advanced_ = true;
  return true;
}

}  // namespace shareme::core
