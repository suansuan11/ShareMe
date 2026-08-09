#include "windows_mf_h264_buffers.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "libyuv/convert.h"
#include "windows/h264_bitstream.hpp"

namespace shareme::rtc {
bool copy_i420_to_nv12(const webrtc::I420BufferInterface &input,
                       std::span<std::byte> output, int output_pitch) {
  const int width = input.width();
  const int height = input.height();
  if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0 ||
      output_pitch < width) {
    return false;
  }
  const auto pitch = static_cast<std::size_t>(output_pitch);
  const auto rows = static_cast<std::size_t>(height) * 3U / 2U;
  if (pitch > std::numeric_limits<std::size_t>::max() / rows ||
      output.size() < pitch * rows) {
    return false;
  }

  auto *destination = reinterpret_cast<std::uint8_t *>(output.data());
  auto *destination_uv = destination + pitch * static_cast<std::size_t>(height);
  return libyuv::I420ToNV12(
             input.DataY(), input.StrideY(), input.DataU(), input.StrideU(),
             input.DataV(), input.StrideV(), destination, output_pitch,
             destination_uv, output_pitch, width, height) == 0;
}

bool copy_nv12_to_i420(std::span<const std::uint8_t> input, int width,
                       int height, int input_pitch,
                       webrtc::I420Buffer &output) {
  if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0 ||
      input_pitch < width || output.width() != width ||
      output.height() != height) {
    return false;
  }
  const auto pitch = static_cast<std::size_t>(input_pitch);
  const auto rows = static_cast<std::size_t>(height) * 3U / 2U;
  if (pitch > std::numeric_limits<std::size_t>::max() / rows ||
      input.size() < pitch * rows) {
    return false;
  }
  const auto *input_uv = input.data() + pitch * static_cast<std::size_t>(height);
  return libyuv::NV12ToI420(
             input.data(), input_pitch, input_uv, input_pitch,
             output.MutableDataY(), output.StrideY(), output.MutableDataU(),
             output.StrideU(), output.MutableDataV(), output.StrideV(), width,
             height) == 0;
}

H264AccessUnitResult normalize_h264_access_unit(
    std::span<const std::uint8_t> input, std::vector<std::uint8_t> &output) {
  const auto converted = convert_avcc_to_annex_b(input, 4, input.size());
  if (!converted.has_value())
    return {};
  const auto info = inspect_annex_b(*converted);
  if (!info.has_value())
    return {};
  const auto normalized = converted.value();
  output.assign(normalized.begin(), normalized.end());
  return {.valid = true, .keyframe = info.has_idr};
}

} // namespace shareme::rtc
