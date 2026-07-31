#include "desktop_frame_converter.hpp"

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

} // namespace shareme::rtc::detail
