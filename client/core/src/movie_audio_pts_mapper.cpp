#include "shareme/core/movie_audio_pts_mapper.hpp"

#include <cstdint>
#include <limits>

namespace shareme::core {
namespace {

constexpr std::int64_t kMaximumCorrelationResidualMs = 80;

[[nodiscard]] std::uint64_t magnitude(std::int64_t value) noexcept {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }
  return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

[[nodiscard]] bool checked_nonnegative_difference(
    std::int64_t first, std::int64_t second,
    std::uint64_t& difference) noexcept {
  if (second < first) {
    return false;
  }

  const auto first_unsigned = static_cast<std::uint64_t>(first);
  const auto second_unsigned = static_cast<std::uint64_t>(second);
  const auto unsigned_difference = second_unsigned - first_unsigned;
  if (unsigned_difference >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }

  difference = unsigned_difference;
  return true;
}

[[nodiscard]] bool is_unspecified_or_equal(
    std::uint64_t value, std::uint64_t expected) noexcept {
  return value == 0 || value == expected;
}

}  // namespace

AnchorResult MovieAudioPtsMapper::accept_anchor(AudioAnchor anchor) noexcept {
  if (anchor.sample_rate == 0 || anchor.channel_count == 0) {
    invalidate();
    return {false, AnchorRejectionReason::invalid_format, confidence_};
  }

  if (have_anchor_) {
    if (anchor.control_sequence <= last_anchor_.control_sequence) {
      invalidate();
      return {false, AnchorRejectionReason::control_sequence_regression,
              confidence_};
    }
    if (anchor.playback_generation < last_anchor_.playback_generation) {
      invalidate();
      return {false, AnchorRejectionReason::playback_generation_regression,
              confidence_};
    }
    if (anchor.audio_epoch < last_anchor_.audio_epoch) {
      invalidate();
      return {false, AnchorRejectionReason::audio_epoch_regression,
              confidence_};
    }
    if (anchor.sample_rate != last_anchor_.sample_rate ||
        anchor.channel_count != last_anchor_.channel_count) {
      invalidate();
      return {false, AnchorRejectionReason::format_change, confidence_};
    }

    if (anchor.playback_generation == last_anchor_.playback_generation) {
      if (anchor.media_pts_ms < last_anchor_.media_pts_ms) {
        invalidate();
        return {false, AnchorRejectionReason::pts_regression, confidence_};
      }

      std::uint64_t pts_delta = 0;
      if (!checked_nonnegative_difference(
              last_anchor_.media_pts_ms, anchor.media_pts_ms, pts_delta)) {
        invalidate();
        return {false, AnchorRejectionReason::residual_overflow, confidence_};
      }
    }
  }

  const bool generation_changed =
      !have_anchor_ ||
      anchor.playback_generation != last_anchor_.playback_generation;
  last_anchor_ = anchor;
  have_anchor_ = true;
  if (generation_changed) {
    have_correlation_ = false;
    last_correlation_ = {};
  }
  // A control anchor establishes a usable PTS origin, not correlation.
  confidence_ = PtsMappingConfidence::provisional;
  return {true, AnchorRejectionReason::none, confidence_};
}

CorrelationResult MovieAudioPtsMapper::observe_correlation(
    CorrelationObservation observation) noexcept {
  CorrelationResult result;
  result.confidence = confidence_;

  if (!have_anchor_) {
    result.reason = CorrelationRejectionReason::no_anchor;
    confidence_ = PtsMappingConfidence::unavailable;
    result.confidence = confidence_;
    return result;
  }

  if (!observation.source_sequence.has_value()) {
    result.reason = CorrelationRejectionReason::missing_source_sequence;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (!observation.decoded_sequence.has_value()) {
    result.reason = CorrelationRejectionReason::missing_decoded_sequence;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }

  result.source_sequence = *observation.source_sequence;
  result.decoded_sequence = *observation.decoded_sequence;
  result.residual_ms = observation.residual_ms;

  if (!observation.valid) {
    result.reason = CorrelationRejectionReason::invalid_observation;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }

  if (observation.playback_generation != 0 &&
      observation.playback_generation < last_anchor_.playback_generation) {
    result.reason = CorrelationRejectionReason::playback_generation_regression;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (!is_unspecified_or_equal(
          observation.playback_generation,
          last_anchor_.playback_generation)) {
    result.reason = CorrelationRejectionReason::playback_generation_mismatch;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (observation.audio_epoch != 0 &&
      observation.audio_epoch < last_anchor_.audio_epoch) {
    result.reason = CorrelationRejectionReason::audio_epoch_regression;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (!is_unspecified_or_equal(observation.audio_epoch,
                              last_anchor_.audio_epoch)) {
    result.reason = CorrelationRejectionReason::audio_epoch_mismatch;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if ((observation.sample_rate != 0 &&
       observation.sample_rate != last_anchor_.sample_rate) ||
      (observation.channel_count != 0 &&
       observation.channel_count != last_anchor_.channel_count)) {
    result.reason = CorrelationRejectionReason::format_change;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (magnitude(observation.residual_ms) >
      static_cast<std::uint64_t>(kMaximumCorrelationResidualMs)) {
    result.reason = CorrelationRejectionReason::residual_exceeded;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }
  if (have_correlation_ &&
      (*observation.source_sequence < last_correlation_.source_sequence ||
       *observation.decoded_sequence < last_correlation_.decoded_sequence)) {
    result.reason = CorrelationRejectionReason::sequence_regression;
    retain_nonlocked_confidence();
    result.confidence = confidence_;
    return result;
  }

  result.valid = true;
  result.reason = CorrelationRejectionReason::none;
  result.confidence = PtsMappingConfidence::locked;
  last_correlation_ = result;
  have_correlation_ = true;
  confidence_ = PtsMappingConfidence::locked;
  return result;
}

PtsMappingConfidence MovieAudioPtsMapper::confidence() const noexcept {
  return confidence_;
}

MovieAudioPtsMapperSnapshot MovieAudioPtsMapper::snapshot() const noexcept {
  MovieAudioPtsMapperSnapshot result;
  result.confidence = confidence_;
  if (have_anchor_) {
    result.latest_anchor = last_anchor_;
  }
  if (have_correlation_) {
    result.latest_correlation = last_correlation_;
  }
  return result;
}

void MovieAudioPtsMapper::reset() noexcept {
  have_anchor_ = false;
  last_anchor_ = {};
  confidence_ = PtsMappingConfidence::unavailable;
  have_correlation_ = false;
  last_correlation_ = {};
}

void MovieAudioPtsMapper::invalidate() noexcept {
  confidence_ = PtsMappingConfidence::invalid;
  have_correlation_ = false;
  last_correlation_ = {};
}

void MovieAudioPtsMapper::retain_nonlocked_confidence() noexcept {
  if (confidence_ == PtsMappingConfidence::locked) {
    confidence_ = PtsMappingConfidence::degraded;
  } else if (confidence_ != PtsMappingConfidence::invalid) {
    confidence_ = have_anchor_ ? PtsMappingConfidence::provisional
                               : PtsMappingConfidence::unavailable;
  }
}

}  // namespace shareme::core
