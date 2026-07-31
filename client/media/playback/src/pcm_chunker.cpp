#include "shareme/media/pcm_chunker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace shareme::media {
namespace {

constexpr int required_sample_rate = 48'000;
constexpr int required_channels = 2;
constexpr std::int64_t discontinuity_tolerance_ms = 10;
constexpr std::int64_t chunk_duration_ms = 10;

constexpr auto invalid_format_error = "pcm-invalid-format";
constexpr auto pending_overflow_error = "pcm-buffer-overflow";
constexpr auto timestamp_overflow_error = "pcm-timestamp-overflow";

[[nodiscard]] bool distance_exceeds(
    std::int64_t first,
    std::int64_t second,
    std::uint64_t tolerance) noexcept {
  const auto first_unsigned = static_cast<std::uint64_t>(first);
  const auto second_unsigned = static_cast<std::uint64_t>(second);
  const auto difference =
      first >= second ? first_unsigned - second_unsigned
                      : second_unsigned - first_unsigned;
  return difference > tolerance;
}

[[nodiscard]] bool add_milliseconds(
    std::int64_t timestamp,
    std::uint64_t milliseconds,
    std::int64_t& result) noexcept {
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if (milliseconds > static_cast<std::uint64_t>(maximum)) {
    return false;
  }
  const auto signed_milliseconds =
      static_cast<std::int64_t>(milliseconds);
  if (timestamp > maximum - signed_milliseconds) {
    return false;
  }
  result = timestamp + signed_milliseconds;
  return true;
}

[[nodiscard]] constexpr std::uint64_t samples_to_milliseconds(
    std::size_t samples_per_channel) noexcept {
  return static_cast<std::uint64_t>(samples_per_channel) * 1'000U /
         required_sample_rate;
}

}  // namespace

bool PcmChunker::push(AudioFrame frame) {
  if (frame.sample_rate != required_sample_rate ||
      frame.channels != required_channels ||
      frame.interleaved_samples.empty() ||
      frame.interleaved_samples.size() % channel_count != 0) {
    error_ = invalid_format_error;
    return false;
  }

  if (frame.interleaved_samples.size() > max_pending_samples) {
    error_ = pending_overflow_error;
    return false;
  }

  bool discontinuity = false;
  if (!pending_samples_.empty()) {
    const auto pending_samples_per_channel =
        pending_samples_.size() / channel_count;
    auto expected_pts_ms = std::int64_t{0};
    if (!add_milliseconds(
            *pending_start_pts_ms_,
            samples_to_milliseconds(pending_samples_per_channel),
            expected_pts_ms)) {
      error_ = timestamp_overflow_error;
      return false;
    }
    discontinuity = distance_exceeds(
        frame.pts_ms,
        expected_pts_ms,
        discontinuity_tolerance_ms);
  }

  const auto retained_samples =
      discontinuity ? std::size_t{0} : pending_samples_.size();
  if (frame.interleaved_samples.size() >
      max_pending_samples - retained_samples) {
    error_ = pending_overflow_error;
    return false;
  }

  const auto resulting_samples =
      retained_samples + frame.interleaved_samples.size();
  const auto resulting_samples_per_channel =
      resulting_samples / channel_count;
  const auto furthest_chunk_offset_ms =
      ((resulting_samples_per_channel - 1U) /
       samples_per_channel_per_chunk) *
      static_cast<std::uint64_t>(chunk_duration_ms);
  const auto resulting_start_pts_ms =
      retained_samples == 0 ? frame.pts_ms : *pending_start_pts_ms_;
  auto furthest_chunk_pts_ms = std::int64_t{0};
  if (!add_milliseconds(
          resulting_start_pts_ms,
          furthest_chunk_offset_ms,
          furthest_chunk_pts_ms)) {
    error_ = timestamp_overflow_error;
    return false;
  }

  if (discontinuity) {
    pending_samples_.clear();
    pending_start_pts_ms_.reset();
  }
  if (pending_samples_.empty()) {
    pending_start_pts_ms_ = frame.pts_ms;
  }
  pending_samples_.insert(
      pending_samples_.end(),
      std::make_move_iterator(frame.interleaved_samples.begin()),
      std::make_move_iterator(frame.interleaved_samples.end()));
  error_.clear();
  return true;
}

std::optional<PcmChunk> PcmChunker::pop() {
  if (pending_samples_.size() < samples_per_chunk) {
    return std::nullopt;
  }

  auto next_pts_ms = std::int64_t{0};
  if (pending_samples_.size() > samples_per_chunk &&
      !add_milliseconds(
          *pending_start_pts_ms_,
          chunk_duration_ms,
          next_pts_ms)) {
    error_ = timestamp_overflow_error;
    return std::nullopt;
  }

  PcmChunk chunk;
  chunk.pts_ms = *pending_start_pts_ms_;
  chunk.interleaved_samples.assign(
      std::make_move_iterator(pending_samples_.begin()),
      std::make_move_iterator(
          pending_samples_.begin() +
          static_cast<std::ptrdiff_t>(samples_per_chunk)));
  pending_samples_.erase(
      pending_samples_.begin(),
      pending_samples_.begin() +
          static_cast<std::ptrdiff_t>(samples_per_chunk));

  if (pending_samples_.empty()) {
    pending_start_pts_ms_.reset();
  } else {
    *pending_start_pts_ms_ = next_pts_ms;
  }
  return chunk;
}

void PcmChunker::reset() noexcept {
  pending_samples_.clear();
  pending_start_pts_ms_.reset();
  error_.clear();
}

std::string PcmChunker::error() const {
  return error_;
}

}  // namespace shareme::media
