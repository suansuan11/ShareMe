#include "desktop_frame_converter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "api/video/i420_buffer.h"
#include "libyuv/convert.h"

namespace shareme::rtc::detail {
namespace {

constexpr int kMaximumDesktopDimension = 16'384;

[[nodiscard]] bool valid_frame(const MappedBgraFrame &frame) noexcept {
  if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 ||
      frame.width > kMaximumDesktopDimension ||
      frame.height > kMaximumDesktopDimension) {
    return false;
  }
  const auto width = static_cast<std::size_t>(frame.width);
  if (width > std::numeric_limits<std::size_t>::max() / 4U)
    return false;
  const auto minimum_pitch = width * 4U;
  if (frame.row_pitch < minimum_pitch ||
      frame.row_pitch >
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  return static_cast<std::size_t>(frame.height) <=
         std::numeric_limits<std::size_t>::max() / frame.row_pitch;
}

[[nodiscard]] bool
valid_frame(const MappedRgba16FloatFrame &frame) noexcept {
  if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 ||
      frame.width > kMaximumDesktopDimension ||
      frame.height > kMaximumDesktopDimension) {
    return false;
  }
  const auto width = static_cast<std::size_t>(frame.width);
  if (width > std::numeric_limits<std::size_t>::max() / 8U)
    return false;
  const auto minimum_pitch = width * 8U;
  if (frame.row_pitch < minimum_pitch)
    return false;
  return static_cast<std::size_t>(frame.height) <=
         std::numeric_limits<std::size_t>::max() / frame.row_pitch;
}

[[nodiscard]] float half_to_float(std::uint16_t half) noexcept {
  const auto sign = static_cast<std::uint32_t>(half & 0x8000U) << 16U;
  const auto exponent = static_cast<std::uint32_t>((half >> 10U) & 0x1fU);
  auto mantissa = static_cast<std::uint32_t>(half & 0x03ffU);
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      std::uint32_t adjusted_exponent = 113U;
      while ((mantissa & 0x0400U) == 0) {
        mantissa <<= 1U;
        --adjusted_exponent;
      }
      mantissa &= 0x03ffU;
      bits = sign | (adjusted_exponent << 23U) | (mantissa << 13U);
    }
  } else if (exponent == 31U) {
    bits = sign | 0x7f800000U | (mantissa << 13U);
  } else {
    bits = sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
  }
  float result = 0.0F;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

[[nodiscard]] float srgb(float linear) noexcept {
  if (std::isnan(linear) || linear <= 0.0F)
    return 0.0F;
  if (!std::isfinite(linear))
    return 1.0F;
  linear = std::clamp(linear, 0.0F, 1.0F);
  if (linear <= 0.0031308F)
    return linear * 12.92F;
  return 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
}

[[nodiscard]] const std::array<float, 65'536> &srgb_half_lut() noexcept {
  static const auto table = [] {
    std::array<float, 65'536> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
      values[index] = srgb(half_to_float(static_cast<std::uint16_t>(index)));
    }
    return values;
  }();
  return table;
}

struct Rgb {
  float red;
  float green;
  float blue;
};

[[nodiscard]] Rgb read_rgba16_float(const std::uint8_t *pixel) noexcept {
  std::uint16_t components[3]{};
  std::memcpy(components, pixel, sizeof(components));
  const auto &table = srgb_half_lut();
  return {table[components[0]], table[components[1]], table[components[2]]};
}

[[nodiscard]] std::uint8_t byte(float value) noexcept {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(value), 0L, 255L));
}

[[nodiscard]] std::uint8_t luma(const Rgb &rgb) noexcept {
  return byte(16.0F +
              255.0F * (0.182586F * rgb.red + 0.614231F * rgb.green +
                        0.062007F * rgb.blue));
}

[[nodiscard]] std::uint8_t chroma_u(const Rgb &rgb) noexcept {
  return byte(128.0F +
              255.0F * (-0.100644F * rgb.red - 0.338572F * rgb.green +
                        0.439216F * rgb.blue));
}

[[nodiscard]] std::uint8_t chroma_v(const Rgb &rgb) noexcept {
  return byte(128.0F +
              255.0F * (0.439216F * rgb.red - 0.398942F * rgb.green -
                        0.040274F * rgb.blue));
}

} // namespace

webrtc::scoped_refptr<webrtc::I420Buffer>
convert_mapped_bgra_to_i420(const MappedBgraFrame &frame) noexcept {
  if (!valid_frame(frame))
    return nullptr;

  auto output = webrtc::I420Buffer::Create(frame.width, frame.height);
  const auto result = libyuv::ARGBToI420(
      frame.data, static_cast<int>(frame.row_pitch), output->MutableDataY(),
      output->StrideY(), output->MutableDataU(), output->StrideU(),
      output->MutableDataV(), output->StrideV(), frame.width, frame.height);
  if (result != 0)
    return nullptr;
  return output;
}

webrtc::scoped_refptr<webrtc::I420Buffer>
convert_mapped_rgba16_float_to_i420(
    const MappedRgba16FloatFrame &frame) noexcept {
  if (!valid_frame(frame))
    return nullptr;

  auto output = webrtc::I420Buffer::Create(frame.width, frame.height);
  for (int y = 0; y < frame.height; y += 2) {
    auto *target_u = output->MutableDataU() +
                     static_cast<std::size_t>(y / 2) * output->StrideU();
    auto *target_v = output->MutableDataV() +
                     static_cast<std::size_t>(y / 2) * output->StrideV();
    for (int x = 0; x < frame.width; x += 2) {
      Rgb average{};
      int sample_count = 0;
      for (int sample_y = y; sample_y < std::min(y + 2, frame.height);
           ++sample_y) {
        const auto *source_row =
            frame.data + static_cast<std::size_t>(sample_y) * frame.row_pitch;
        auto *target_y = output->MutableDataY() +
                         static_cast<std::size_t>(sample_y) * output->StrideY();
        for (int sample_x = x;
             sample_x < std::min(x + 2, frame.width); ++sample_x) {
          const auto rgb = read_rgba16_float(source_row + sample_x * 8);
          target_y[sample_x] = luma(rgb);
          average.red += rgb.red;
          average.green += rgb.green;
          average.blue += rgb.blue;
          ++sample_count;
        }
      }
      average.red /= static_cast<float>(sample_count);
      average.green /= static_cast<float>(sample_count);
      average.blue /= static_cast<float>(sample_count);
      target_u[x / 2] = chroma_u(average);
      target_v[x / 2] = chroma_v(average);
    }
  }
  return output;
}

} // namespace shareme::rtc::detail
