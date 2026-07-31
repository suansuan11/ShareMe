#pragma once

#include <cstddef>
#include <cstdint>

#include "api/scoped_refptr.h"

namespace webrtc {
class I420Buffer;
}

namespace shareme::rtc::detail {

struct MappedBgraFrame {
  const std::uint8_t *data;
  int width;
  int height;
  std::size_t row_pitch;
};

struct MappedRgba16FloatFrame {
  const std::uint8_t *data;
  int width;
  int height;
  std::size_t row_pitch;
};

[[nodiscard]] webrtc::scoped_refptr<webrtc::I420Buffer>
convert_mapped_bgra_to_i420(const MappedBgraFrame &frame) noexcept;

[[nodiscard]] webrtc::scoped_refptr<webrtc::I420Buffer>
convert_mapped_rgba16_float_to_i420(
    const MappedRgba16FloatFrame &frame) noexcept;

} // namespace shareme::rtc::detail
