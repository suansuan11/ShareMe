#pragma once

#include <cstddef>
#include <cstdint>
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

struct AudioFrame {
  std::vector<std::int16_t> interleaved_samples;
  int sample_rate{0};
  int channels{0};
  std::int64_t pts_ms{0};
  std::uint64_t generation{0};
};

}  // namespace shareme::media
