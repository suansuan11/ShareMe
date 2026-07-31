#include "shareme/media/pcm_chunker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

namespace shareme::media {
namespace {

constexpr int required_sample_rate = 48'000;
constexpr int required_channels = 2;
constexpr std::int64_t discontinuity_tolerance_ms = 10;
constexpr std::int64_t chunk_duration_ms = 10;

constexpr auto invalid_format_error = "pcm-invalid-format";
constexpr auto pending_overflow_error = "pcm-buffer-overflow";

[[nodiscard]] std::int64_t distance(
    std::int64_t first,
    std::int64_t second) {
  return first >= second ? first - second : second - first;
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

  if (!pending_samples_.empty()) {
    const auto pending_samples_per_channel =
        pending_samples_.size() / channel_count;
    const auto expected_pts_ms =
        *pending_start_pts_ms_ +
        static_cast<std::int64_t>(
            pending_samples_per_channel * 1'000U / required_sample_rate);
    if (distance(frame.pts_ms, expected_pts_ms) >
        discontinuity_tolerance_ms) {
      pending_samples_.clear();
      pending_start_pts_ms_.reset();
    }
  }

  if (frame.interleaved_samples.size() >
      max_pending_samples - pending_samples_.size()) {
    error_ = pending_overflow_error;
    return false;
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
    *pending_start_pts_ms_ += chunk_duration_ms;
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
