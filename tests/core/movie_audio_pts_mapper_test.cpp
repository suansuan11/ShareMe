#include "shareme/core/movie_audio_pts_mapper.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::core::AudioAnchor anchor() {
  return {
      .control_sequence = 10,
      .playback_generation = 7,
      .audio_epoch = 4,
      .host_source_sequence = 100,
      .media_pts_ms = 1'000,
      .sample_rate = 48'000,
      .channel_count = 2,
  };
}

template <typename Mutator>
void rejects_anchor_with_reason(
    Mutator mutator, shareme::core::AnchorRejectionReason expected_reason) {
  shareme::core::MovieAudioPtsMapper mapper;
  REQUIRE(mapper.accept_anchor(anchor()).accepted);

  auto candidate = anchor();
  ++candidate.control_sequence;
  ++candidate.host_source_sequence;
  candidate.media_pts_ms += 10;
  mutator(candidate);

  const auto result = mapper.accept_anchor(candidate);
  REQUIRE(!result.accepted);
  REQUIRE(result.reason == expected_reason);
}

void accepts_a_monotonic_anchor() {
  shareme::core::MovieAudioPtsMapper mapper;
  const auto result = mapper.accept_anchor(anchor());
  REQUIRE(result.accepted);
  REQUIRE(result.reason == shareme::core::AnchorRejectionReason::none);
}

void rejects_control_sequence_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.control_sequence = 9; },
      shareme::core::AnchorRejectionReason::control_sequence_regression);
}

void rejects_playback_generation_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.playback_generation = 6; },
      shareme::core::AnchorRejectionReason::playback_generation_regression);
}

void rejects_audio_epoch_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.audio_epoch = 3; },
      shareme::core::AnchorRejectionReason::audio_epoch_regression);
}

void rejects_format_change() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.sample_rate = 44'100; },
      shareme::core::AnchorRejectionReason::format_change);
}

void rejects_residual_overflow() {
  rejects_anchor_with_reason(
      [](auto& candidate) {
        candidate.media_pts_ms = std::numeric_limits<std::int64_t>::min();
      },
      shareme::core::AnchorRejectionReason::residual_overflow);
}

}  // namespace

int main() {
  accepts_a_monotonic_anchor();
  rejects_control_sequence_regression();
  rejects_playback_generation_regression();
  rejects_audio_epoch_regression();
  rejects_format_change();
  rejects_residual_overflow();
  return EXIT_SUCCESS;
}
