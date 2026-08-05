#include "shareme/core/movie_audio_clock.hpp"

#include <limits>

namespace shareme::core {
namespace {

[[nodiscard]] std::optional<std::uint64_t> frames_to_milliseconds(
    std::uint64_t frames, std::uint32_t sample_rate) noexcept {
  if (sample_rate == 0) {
    return std::nullopt;
  }

  const auto whole_seconds = frames / sample_rate;
  const auto remainder = frames % sample_rate;
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (whole_seconds > max / 1'000U) {
    return std::nullopt;
  }

  const auto whole_ms = whole_seconds * 1'000U;
  const auto fractional_ms =
      (remainder * 1'000U) / static_cast<std::uint64_t>(sample_rate);
  if (whole_ms > max - fractional_ms) {
    return std::nullopt;
  }
  return whole_ms + fractional_ms;
}

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

[[nodiscard]] bool choose_anchor(
    const AudioClockObservation& observation, std::int64_t& pts_ms,
    std::uint64_t& consumed_frames, bool& present) noexcept {
  if (observation.media_pts_ms.has_value()) {
    pts_ms = *observation.media_pts_ms;
    consumed_frames = observation.media_pts_frame.value_or(
        observation.logical_consumed_frames);
    present = true;
    return true;
  }
  if (observation.anchor_pts_ms.has_value()) {
    pts_ms = *observation.anchor_pts_ms;
    consumed_frames = observation.anchor_consumed_frames.value_or(
        observation.logical_consumed_frames);
    present = true;
    return true;
  }
  present = false;
  return true;
}

[[nodiscard]] bool has_lock_evidence(
    const AudioClockObservation& observation) noexcept {
  return observation.correlation_valid || observation.correlation_locked ||
      observation.correlation_confidence == ClockConfidence::locked;
}

[[nodiscard]] bool has_degraded_evidence(
    const AudioClockObservation& observation) noexcept {
  return observation.discontinuity || observation.underrun ||
      observation.correlation_confidence == ClockConfidence::degraded;
}

}  // namespace

MovieAudioClockSnapshot MovieAudioClock::observe(
    AudioClockObservation observation,
    MonotonicTime monotonic_now) noexcept {
  if (have_observation_time_ && monotonic_now < last_observation_time_) {
    mark_invalid();
    return snapshot_;
  }
  last_observation_time_ = monotonic_now;
  have_observation_time_ = true;

  if (!initialized_) {
    initialized_ = true;
    playback_generation_ = observation.playback_generation;
    host_audio_epoch_ = observation.host_audio_epoch;
    route_generation_ = observation.route_generation;
    renderer_clock_epoch_ = observation.renderer_clock_epoch;
    logical_consumed_frames_ = observation.consumption_known
        ? observation.logical_consumed_frames
        : 0;
    consumption_unknown_ = !observation.consumption_known;
    sample_rate_ = observation.sample_rate;
  } else {
    if (observation.playback_generation < playback_generation_ ||
        observation.host_audio_epoch < host_audio_epoch_ ||
        observation.route_generation < route_generation_) {
      mark_invalid();
      return snapshot_;
    }

    if (observation.renderer_clock_epoch < renderer_clock_epoch_ &&
        !consumption_unknown_ && !renderer_clock_epoch_locally_advanced_) {
      mark_invalid();
      return snapshot_;
    }
    if (observation.renderer_clock_epoch > renderer_clock_epoch_) {
      renderer_clock_epoch_ = observation.renderer_clock_epoch;
      renderer_clock_epoch_locally_advanced_ = false;
      have_playout_pts_ = false;
      requires_relock_ = true;
    } else if (observation.renderer_clock_epoch == renderer_clock_epoch_) {
      renderer_clock_epoch_locally_advanced_ = false;
    }

    if (observation.playback_generation > playback_generation_ ||
        observation.host_audio_epoch > host_audio_epoch_) {
      if (observation.playback_generation > playback_generation_) {
        playback_generation_ = observation.playback_generation;
      }
      if (observation.host_audio_epoch > host_audio_epoch_) {
        host_audio_epoch_ = observation.host_audio_epoch;
      }
      have_anchor_ = false;
      have_playout_pts_ = false;
      requires_relock_ = true;
    }
    if (observation.route_generation > route_generation_) {
      route_generation_ = observation.route_generation;
      requires_relock_ = true;
    }
    if (observation.sample_rate != 0) {
      if (sample_rate_ != 0 && observation.sample_rate != sample_rate_) {
        mark_invalid();
        return snapshot_;
      }
      sample_rate_ = observation.sample_rate;
    }
  }

  if (!initialized_) {
    mark_invalid();
    return snapshot_;
  }

  if (!observation.consumption_known) {
    if (!consumption_unknown_) {
      if (!increment_renderer_clock_epoch()) {
        mark_invalid();
        return snapshot_;
      }
    }
    consumption_unknown_ = true;
    requires_relock_ = true;
    snapshot_.logical_consumed_frames = logical_consumed_frames_;
    snapshot_.output_latency_frames = observation.output_latency_frames;
    snapshot_.playback_generation = playback_generation_;
    snapshot_.host_audio_epoch = host_audio_epoch_;
    snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
    snapshot_.route_generation = route_generation_;
    snapshot_.consumption_known = false;
    set_confidence(ClockConfidence::invalid);
    return snapshot_;
  }

  if (consumption_unknown_) {
    consumption_unknown_ = false;
  }
  if (observation.logical_consumed_frames < logical_consumed_frames_) {
    mark_invalid();
    return snapshot_;
  }
  logical_consumed_frames_ = observation.logical_consumed_frames;

  std::int64_t anchor_pts_ms = 0;
  std::uint64_t anchor_consumed_frames = 0;
  bool has_new_anchor = false;
  if (!choose_anchor(observation, anchor_pts_ms, anchor_consumed_frames,
                     has_new_anchor)) {
    mark_invalid();
    return snapshot_;
  }
  if (has_new_anchor) {
    if (anchor_consumed_frames > logical_consumed_frames_) {
      mark_invalid();
      return snapshot_;
    }
    anchor_pts_ms_ = anchor_pts_ms;
    anchor_consumed_frames_ = anchor_consumed_frames;
    have_anchor_ = true;
  }

  std::optional<std::int64_t> calculated_pts_ms;
  if (have_anchor_) {
    if (sample_rate_ == 0 || logical_consumed_frames_ < anchor_consumed_frames_) {
      mark_invalid();
      return snapshot_;
    }
    const auto elapsed_frames = logical_consumed_frames_ - anchor_consumed_frames_;
    const auto elapsed_ms = frames_to_milliseconds(elapsed_frames, sample_rate_);
    if (!elapsed_ms.has_value()) {
      mark_invalid();
      return snapshot_;
    }

    auto pts_ms = std::int64_t{0};
    if (!checked_add_unsigned(anchor_pts_ms_, *elapsed_ms, pts_ms)) {
      mark_invalid();
      return snapshot_;
    }
    if (observation.output_latency_frames.has_value()) {
      const auto latency_ms = frames_to_milliseconds(
          *observation.output_latency_frames, sample_rate_);
      if (!latency_ms.has_value() ||
          !checked_subtract_unsigned(pts_ms, *latency_ms, pts_ms)) {
        mark_invalid();
        return snapshot_;
      }
    }
    calculated_pts_ms = pts_ms;
  }

  if (calculated_pts_ms.has_value()) {
    const bool new_pts_epoch =
        !have_playout_pts_ ||
        pts_playback_generation_ != playback_generation_ ||
        pts_renderer_clock_epoch_ != renderer_clock_epoch_;
    if (!new_pts_epoch && *calculated_pts_ms < last_playout_pts_ms_) {
      mark_invalid();
      return snapshot_;
    }
    last_playout_pts_ms_ = *calculated_pts_ms;
    pts_playback_generation_ = playback_generation_;
    pts_renderer_clock_epoch_ = renderer_clock_epoch_;
    have_playout_pts_ = true;
  }

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
  if (observation.correlation_confidence == ClockConfidence::invalid) {
    set_confidence(ClockConfidence::invalid);
  } else if (has_degraded_evidence(observation)) {
    set_confidence(ClockConfidence::degraded);
  } else if (!have_playout_pts_) {
    set_confidence(ClockConfidence::unavailable);
  } else if (has_lock_evidence(observation) &&
             observation.output_latency_frames.has_value() &&
             (!requires_relock_ || observation.correlation_valid ||
              observation.correlation_locked)) {
    set_confidence(ClockConfidence::locked);
    requires_relock_ = false;
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

void MovieAudioClock::reset() noexcept {
  snapshot_ = {};
  initialized_ = false;
  consumption_unknown_ = false;
  renderer_clock_epoch_locally_advanced_ = false;
  requires_relock_ = false;
  have_observation_time_ = false;
  last_observation_time_ = {};
  logical_consumed_frames_ = 0;
  renderer_clock_epoch_ = 0;
  playback_generation_ = 0;
  host_audio_epoch_ = 0;
  route_generation_ = 0;
  sample_rate_ = 0;
  have_anchor_ = false;
  anchor_pts_ms_ = 0;
  anchor_consumed_frames_ = 0;
  have_playout_pts_ = false;
  last_playout_pts_ms_ = 0;
  pts_playback_generation_ = 0;
  pts_renderer_clock_epoch_ = 0;
}

void MovieAudioClock::set_confidence(ClockConfidence confidence) noexcept {
  snapshot_.confidence = confidence;
  snapshot_.clock_confidence = confidence;
}

void MovieAudioClock::mark_invalid() noexcept {
  set_confidence(ClockConfidence::invalid);
  snapshot_.logical_consumed_frames = logical_consumed_frames_;
  snapshot_.playback_generation = playback_generation_;
  snapshot_.host_audio_epoch = host_audio_epoch_;
  snapshot_.renderer_clock_epoch = renderer_clock_epoch_;
  snapshot_.route_generation = route_generation_;
  snapshot_.consumption_known = !consumption_unknown_;
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
  have_playout_pts_ = false;
  return true;
}

}  // namespace shareme::core
