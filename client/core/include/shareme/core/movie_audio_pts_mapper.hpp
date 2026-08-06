#pragma once

#include <cstdint>
#include <optional>

namespace shareme::core {

enum class PtsMappingConfidence {
  unavailable,
  provisional,
  locked,
  degraded,
  invalid,
};

using MappingConfidence = PtsMappingConfidence;
using CorrelationConfidence = PtsMappingConfidence;

enum class CorrelationProvenance {
  none,
  validated_shared_sequence,
  approved_estimator,
};

using CorrelationEvidence = CorrelationProvenance;

[[nodiscard]] constexpr bool is_approved_correlation_provenance(
    CorrelationProvenance provenance) noexcept {
  return provenance == CorrelationProvenance::validated_shared_sequence ||
      provenance == CorrelationProvenance::approved_estimator;
}

struct AudioAnchor {
  std::uint64_t control_sequence = 0;
  std::uint64_t playback_generation = 0;
  std::uint64_t audio_epoch = 0;
  std::uint64_t host_source_sequence = 0;
  std::int64_t media_pts_ms = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
};

enum class AnchorRejectionReason {
  none,
  invalid_format,
  control_sequence_regression,
  playback_generation_regression,
  audio_epoch_regression,
  host_source_sequence_regression,
  format_change,
  pts_regression,
  monotonic_pts_regression = pts_regression,
  residual_overflow,
};

struct AnchorResult {
  bool accepted = false;
  AnchorRejectionReason reason = AnchorRejectionReason::none;
  PtsMappingConfidence confidence = PtsMappingConfidence::unavailable;
};

struct CorrelationObservation {
  // Every identity component is explicit; zero is not a wildcard.
  std::optional<std::uint64_t> anchor_control_sequence = std::nullopt;
  std::optional<std::uint64_t> source_sequence = std::nullopt;
  std::optional<std::uint64_t> decoded_sequence = std::nullopt;
  std::optional<std::uint64_t> playback_generation = std::nullopt;
  std::optional<std::uint64_t> audio_epoch = std::nullopt;
  std::optional<std::uint32_t> sample_rate = std::nullopt;
  std::optional<std::uint16_t> channel_count = std::nullopt;
  std::int64_t residual_ms = 0;
  bool valid = false;
  CorrelationProvenance provenance = CorrelationProvenance::none;
};

enum class CorrelationRejectionReason {
  none,
  no_anchor,
  invalid_observation,
  missing_anchor_identity,
  missing_source_sequence,
  missing_decoded_sequence,
  missing_playback_generation,
  missing_audio_epoch,
  provenance_not_approved,
  anchor_identity_mismatch,
  playback_generation_regression,
  playback_generation_mismatch,
  audio_epoch_regression,
  audio_epoch_mismatch,
  format_change,
  sequence_regression,
  residual_exceeded,
};

struct CorrelationResult {
  std::optional<std::uint64_t> anchor_control_sequence = std::nullopt;
  std::optional<std::uint64_t> source_sequence = std::nullopt;
  std::optional<std::uint64_t> decoded_sequence = std::nullopt;
  std::optional<std::uint64_t> playback_generation = std::nullopt;
  std::optional<std::uint64_t> audio_epoch = std::nullopt;
  std::optional<std::uint32_t> sample_rate = std::nullopt;
  std::optional<std::uint16_t> channel_count = std::nullopt;
  std::int64_t residual_ms = 0;
  bool valid = false;
  CorrelationRejectionReason reason = CorrelationRejectionReason::no_anchor;
  PtsMappingConfidence confidence = PtsMappingConfidence::unavailable;
  CorrelationProvenance provenance = CorrelationProvenance::none;
};

struct MovieAudioPtsMapperSnapshot {
  PtsMappingConfidence confidence = PtsMappingConfidence::unavailable;
  std::optional<AudioAnchor> latest_anchor = std::nullopt;
  std::optional<CorrelationResult> latest_correlation = std::nullopt;
};

class MovieAudioPtsMapper {
 public:
  [[nodiscard]] AnchorResult accept_anchor(AudioAnchor anchor) noexcept;
  [[nodiscard]] CorrelationResult observe_correlation(
      CorrelationObservation observation) noexcept;

  [[nodiscard]] PtsMappingConfidence confidence() const noexcept;
  [[nodiscard]] MovieAudioPtsMapperSnapshot snapshot() const noexcept;
  void reset() noexcept;

 private:
  void invalidate() noexcept;
  void retain_nonlocked_confidence() noexcept;

  bool have_anchor_ = false;
  AudioAnchor last_anchor_{};
  PtsMappingConfidence confidence_ = PtsMappingConfidence::unavailable;
  bool have_correlation_ = false;
  CorrelationResult last_correlation_{};
};

}  // namespace shareme::core
