#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "api/video/i420_buffer.h"

namespace shareme::rtc {

struct H264AccessUnitResult {
  bool valid{false};
  bool keyframe{false};
};

[[nodiscard]] bool copy_i420_to_nv12(
    const webrtc::I420BufferInterface &input, std::span<std::byte> output,
    int output_pitch);

[[nodiscard]] bool copy_nv12_to_i420(
    std::span<const std::uint8_t> input, int width, int height,
    int input_pitch, webrtc::I420Buffer &output);

[[nodiscard]] H264AccessUnitResult normalize_h264_access_unit(
    std::span<const std::uint8_t> input, std::vector<std::uint8_t> &output);

} // namespace shareme::rtc
