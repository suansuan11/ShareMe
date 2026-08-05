#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace shareme::media {

enum class VideoPixelFormat { i420, rgba };

struct VideoFrame {
  VideoPixelFormat pixel_format{VideoPixelFormat::rgba};
  std::vector<std::byte> i420_y;
  std::vector<std::byte> i420_u;
  std::vector<std::byte> i420_v;
  int stride_y{0};
  int stride_u{0};
  int stride_v{0};
  std::vector<std::byte> rgba;
  int width{0};
  int height{0};
  int stride{0};
  std::int64_t pts_ms{0};
  std::uint64_t generation{0};
};

namespace detail {

[[nodiscard]] inline std::size_t saturating_add(
    std::size_t lhs,
    std::size_t rhs) noexcept {
  return rhs > std::numeric_limits<std::size_t>::max() - lhs
             ? std::numeric_limits<std::size_t>::max()
             : lhs + rhs;
}

[[nodiscard]] inline std::size_t saturating_multiply(
    std::size_t lhs,
    std::size_t rhs) noexcept {
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    return std::numeric_limits<std::size_t>::max();
  }
  return lhs * rhs;
}

}  // namespace detail

[[nodiscard]] inline std::size_t video_frame_capacity_bytes(
    const VideoFrame& frame) noexcept {
  return detail::saturating_add(
      detail::saturating_add(
          detail::saturating_add(frame.i420_y.capacity(), frame.i420_u.capacity()),
          frame.i420_v.capacity()),
      frame.rgba.capacity());
}

struct AudioFrame {
  std::vector<std::int16_t> interleaved_samples;
  int sample_rate{0};
  int channels{0};
  std::int64_t pts_ms{0};
  std::uint64_t generation{0};
};

[[nodiscard]] inline std::size_t audio_frame_capacity_bytes(
    const AudioFrame& frame) noexcept {
  return detail::saturating_multiply(
      frame.interleaved_samples.capacity(), sizeof(std::int16_t));
}

}  // namespace shareme::media
