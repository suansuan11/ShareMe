#include "shareme/core/movie_audio_pts_mapper.hpp"

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

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

using shareme::core::AnchorRejectionReason;
using shareme::core::AnchorResult;
using shareme::core::AudioAnchor;
using shareme::core::MovieAudioPtsMapper;

static_assert(std::same_as<decltype(AudioAnchor::control_sequence),
                           std::uint64_t>);
static_assert(std::same_as<decltype(AudioAnchor::playback_generation),
                           std::uint64_t>);
static_assert(std::same_as<decltype(AudioAnchor::audio_epoch), std::uint64_t>);
static_assert(std::same_as<decltype(AudioAnchor::host_source_sequence),
                           std::uint64_t>);
static_assert(std::same_as<decltype(AudioAnchor::media_pts_ms),
                           std::int64_t>);
static_assert(std::is_signed_v<decltype(AudioAnchor::media_pts_ms)>);
static_assert(std::same_as<decltype(AudioAnchor::sample_rate), std::uint32_t>);
static_assert(std::same_as<decltype(AudioAnchor::channel_count), std::uint16_t>);
static_assert(std::same_as<decltype(AnchorResult::accepted), bool>);
static_assert(std::same_as<decltype(AnchorResult::reason),
                           AnchorRejectionReason>);
static_assert(std::same_as<
              decltype(std::declval<MovieAudioPtsMapper&>().accept_anchor(
                  std::declval<AudioAnchor>())),
              AnchorResult>);

AudioAnchor anchor() {
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
    Mutator mutator, AnchorRejectionReason expected_reason) {
  MovieAudioPtsMapper mapper;
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

void accepts_two_monotonic_anchors() {
  MovieAudioPtsMapper mapper;
  auto first = anchor();
  const auto first_result = mapper.accept_anchor(first);
  REQUIRE(first_result.accepted);
  REQUIRE(first_result.reason == AnchorRejectionReason::none);

  auto second = first;
  ++second.control_sequence;
  ++second.host_source_sequence;
  second.media_pts_ms += 10;
  const auto second_result = mapper.accept_anchor(second);
  REQUIRE(second_result.accepted);
  REQUIRE(second_result.reason == AnchorRejectionReason::none);
}

void rejects_control_sequence_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.control_sequence = 9; },
      AnchorRejectionReason::control_sequence_regression);
}

void rejects_playback_generation_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.playback_generation = 6; },
      AnchorRejectionReason::playback_generation_regression);
}

void rejects_audio_epoch_regression() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.audio_epoch = 3; },
      AnchorRejectionReason::audio_epoch_regression);
}

void rejects_sample_rate_change() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.sample_rate = 44'100; },
      AnchorRejectionReason::format_change);
}

void rejects_channel_count_change() {
  rejects_anchor_with_reason(
      [](auto& candidate) { candidate.channel_count = 1; },
      AnchorRejectionReason::format_change);
}

void rejects_residual_overflow() {
  MovieAudioPtsMapper mapper;
  auto first = anchor();
  first.media_pts_ms = std::numeric_limits<std::int64_t>::min() + 1;
  REQUIRE(mapper.accept_anchor(first).accepted);

  auto second = first;
  ++second.control_sequence;
  ++second.host_source_sequence;
  second.media_pts_ms = std::numeric_limits<std::int64_t>::max() - 1;

  const auto result = mapper.accept_anchor(second);
  REQUIRE(!result.accepted);
  REQUIRE(result.reason == AnchorRejectionReason::residual_overflow);
}

}  // namespace

int main() {
  accepts_two_monotonic_anchors();
  rejects_control_sequence_regression();
  rejects_playback_generation_regression();
  rejects_audio_epoch_regression();
  rejects_sample_rate_change();
  rejects_channel_count_change();
  rejects_residual_overflow();
  return EXIT_SUCCESS;
}
