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
  format_change,
  pts_regression,
  monotonic_pts_regression = pts_regression,
  residual_overflow,
  residual_exceeded,
};

struct AnchorResult {
  bool accepted = false;
  AnchorRejectionReason reason = AnchorRejectionReason::none;
  PtsMappingConfidence confidence = PtsMappingConfidence::unavailable;
};

struct CorrelationObservation {
  // Missing either sequence means that this observation cannot lock the map.
  std::optional<std::uint64_t> source_sequence = std::nullopt;
  std::optional<std::uint64_t> decoded_sequence = std::nullopt;
  std::uint64_t playback_generation = 0;
  std::uint64_t audio_epoch = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  std::int64_t residual_ms = 0;
  bool valid = false;
};

enum class CorrelationRejectionReason {
  none,
  no_anchor,
  invalid_observation,
  missing_source_sequence,
  missing_decoded_sequence,
  playback_generation_regression,
  playback_generation_mismatch,
  audio_epoch_regression,
  audio_epoch_mismatch,
  format_change,
  sequence_regression,
  residual_exceeded,
};

struct CorrelationResult {
  std::uint64_t source_sequence = 0;
  std::uint64_t decoded_sequence = 0;
  std::int64_t residual_ms = 0;
  bool valid = false;
  CorrelationRejectionReason reason = CorrelationRejectionReason::no_anchor;
  PtsMappingConfidence confidence = PtsMappingConfidence::unavailable;
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
