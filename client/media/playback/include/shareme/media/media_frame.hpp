#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace shareme::media {

struct VideoFrame {
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
