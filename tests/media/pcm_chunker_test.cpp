#include "shareme/media/pcm_chunker.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
#define REQUIRE_FALSE(expression) require(!(expression), "!(" #expression ")", __LINE__)

shareme::media::AudioFrame make_frame(
    std::vector<std::int16_t> samples,
    std::int64_t pts_ms) {
  shareme::media::AudioFrame frame;
  frame.interleaved_samples = std::move(samples);
  frame.sample_rate = 48'000;
  frame.channels = 2;
  frame.pts_ms = pts_ms;
  return frame;
}

std::vector<std::int16_t> numbered_samples(
    std::size_t sample_count,
    std::int16_t first = 0) {
  std::vector<std::int16_t> samples(sample_count);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] =
        static_cast<std::int16_t>(first + static_cast<std::int16_t>(index));
  }
  return samples;
}

void emits_exact_ten_millisecond_stereo_chunk() {
  shareme::media::PcmChunker chunker;
  const auto samples = numbered_samples(480U * 2U);

  REQUIRE(chunker.push(make_frame(samples, 25)));
  const auto chunk = chunker.pop();

  REQUIRE(chunk.has_value());
  REQUIRE(chunk->interleaved_samples.size() == 480U * 2U);
  REQUIRE(chunk->interleaved_samples == samples);
  REQUIRE(chunk->pts_ms == 25);
  REQUIRE_FALSE(chunker.pop().has_value());
}

void preserves_sample_order_across_input_frames() {
  shareme::media::PcmChunker chunker;
  const auto first = numbered_samples(200U * 2U);
  const auto second = numbered_samples(280U * 2U, 400);
  auto expected = first;
  expected.insert(expected.end(), second.begin(), second.end());

  REQUIRE(chunker.push(make_frame(first, 100)));
  REQUIRE(chunker.push(make_frame(second, 104)));
  const auto chunk = chunker.pop();

  REQUIRE(chunk.has_value());
  REQUIRE(chunk->interleaved_samples == expected);
}

void carries_partial_samples_until_a_chunk_is_complete() {
  shareme::media::PcmChunker chunker;

  REQUIRE(chunker.push(make_frame(numbered_samples(240U * 2U), 200)));
  REQUIRE_FALSE(chunker.pop().has_value());
  REQUIRE(chunker.push(make_frame(numbered_samples(239U * 2U), 205)));
  REQUIRE_FALSE(chunker.pop().has_value());
  REQUIRE(chunker.push(make_frame(numbered_samples(1U * 2U), 210)));
  REQUIRE(chunker.pop().has_value());
}

void emits_monotonic_ten_millisecond_pts() {
  shareme::media::PcmChunker chunker;

  REQUIRE(chunker.push(make_frame(numbered_samples(960U * 2U), 123)));
  const auto first = chunker.pop();
  const auto second = chunker.pop();

  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(first->pts_ms == 123);
  REQUIRE(second->pts_ms == 133);
}

void rejects_invalid_audio_formats_with_a_stable_error() {
  shareme::media::PcmChunker chunker;
  auto invalid = make_frame(numbered_samples(480U * 2U), 0);
  invalid.sample_rate = 44'100;

  REQUIRE_FALSE(chunker.push(invalid));
  const auto first_error = chunker.error();
  REQUIRE_FALSE(first_error.empty());
  REQUIRE_FALSE(chunker.push(invalid));
  REQUIRE(chunker.error() == first_error);

  invalid.sample_rate = 48'000;
  invalid.channels = 1;
  REQUIRE_FALSE(chunker.push(invalid));

  invalid.channels = 2;
  invalid.interleaved_samples.pop_back();
  REQUIRE_FALSE(chunker.push(invalid));
}

void resets_partial_audio_after_a_large_discontinuity() {
  shareme::media::PcmChunker chunker;
  const std::vector<std::int16_t> stale_samples(240U * 2U, 11);
  const std::vector<std::int16_t> fresh_samples(480U * 2U, 22);

  REQUIRE(chunker.push(make_frame(stale_samples, 100)));
  REQUIRE(chunker.push(make_frame(fresh_samples, 120)));
  const auto chunk = chunker.pop();

  REQUIRE(chunk.has_value());
  REQUIRE(chunk->pts_ms == 120);
  REQUIRE(std::all_of(
      chunk->interleaved_samples.begin(),
      chunk->interleaved_samples.end(),
      [](std::int16_t sample) { return sample == 22; }));
}

void rejects_pending_audio_overflow() {
  shareme::media::PcmChunker chunker;

  REQUIRE_FALSE(
      chunker.push(make_frame(numbered_samples(4'801U * 2U), 0)));
  REQUIRE_FALSE(chunker.error().empty());
  REQUIRE_FALSE(chunker.pop().has_value());
}

void reset_clears_pending_audio_and_error() {
  shareme::media::PcmChunker chunker;
  auto invalid = make_frame(numbered_samples(2U), 0);
  invalid.sample_rate = 44'100;

  REQUIRE(chunker.push(make_frame(numbered_samples(240U * 2U), 0)));
  REQUIRE_FALSE(chunker.push(invalid));
  chunker.reset();

  REQUIRE_FALSE(chunker.pop().has_value());
  REQUIRE(chunker.error().empty());
}

}  // namespace

int main() {
  emits_exact_ten_millisecond_stereo_chunk();
  preserves_sample_order_across_input_frames();
  carries_partial_samples_until_a_chunk_is_complete();
  emits_monotonic_ten_millisecond_pts();
  rejects_invalid_audio_formats_with_a_stable_error();
  resets_partial_audio_after_a_large_discontinuity();
  rejects_pending_audio_overflow();
  reset_clears_pending_audio_and_error();
  return EXIT_SUCCESS;
}
