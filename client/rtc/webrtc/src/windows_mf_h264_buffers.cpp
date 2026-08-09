#include "windows_mf_h264_buffers.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "libyuv/convert.h"

namespace shareme::rtc {
namespace {

struct StartCode {
  std::size_t offset{0};
  std::size_t size{0};
};

std::optional<StartCode> find_start_code(
    std::span<const std::uint8_t> input, std::size_t offset) {
  for (std::size_t index = offset; index + 3 <= input.size(); ++index) {
    if (input[index] != 0 || input[index + 1] != 0)
      continue;
    if (input[index + 2] == 1)
      return StartCode{index, 3};
    if (index + 4 <= input.size() && input[index + 2] == 0 &&
        input[index + 3] == 1) {
      return StartCode{index, 4};
    }
  }
  return std::nullopt;
}

void observe_nal(std::uint8_t header, bool &keyframe) {
  if ((header & 0x1fU) == 5U)
    keyframe = true;
}

H264AccessUnitResult validate_annex_b(
    std::span<const std::uint8_t> input) {
  const auto first = find_start_code(input, 0);
  if (!first.has_value() || first->offset != 0)
    return {};

  bool keyframe = false;
  auto current = *first;
  while (true) {
    const auto payload = current.offset + current.size;
    const auto next = find_start_code(input, payload);
    const auto end = next.has_value() ? next->offset : input.size();
    if (payload >= end)
      return {};
    observe_nal(input[payload], keyframe);
    if (!next.has_value())
      break;
    current = *next;
  }
  return {.valid = true, .keyframe = keyframe};
}

std::uint32_t read_big_endian_length(std::span<const std::uint8_t> input,
                                     std::size_t offset) {
  return (static_cast<std::uint32_t>(input[offset]) << 24U) |
         (static_cast<std::uint32_t>(input[offset + 1]) << 16U) |
         (static_cast<std::uint32_t>(input[offset + 2]) << 8U) |
         static_cast<std::uint32_t>(input[offset + 3]);
}

} // namespace

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

H264AccessUnitResult normalize_h264_access_unit(
    std::span<const std::uint8_t> input, std::vector<std::uint8_t> &output) {
  if (input.empty())
    return {};

  const auto annex_result = validate_annex_b(input);
  if (annex_result.valid) {
    output.assign(input.begin(), input.end());
    return annex_result;
  }

  std::vector<std::uint8_t> normalized;
  normalized.reserve(input.size());
  bool keyframe = false;
  std::size_t offset = 0;
  while (offset < input.size()) {
    if (input.size() - offset < 4)
      return {};
    const auto length = static_cast<std::size_t>(
        read_big_endian_length(input, offset));
    offset += 4;
    if (length == 0 || length > input.size() - offset)
      return {};
    observe_nal(input[offset], keyframe);
    normalized.insert(normalized.end(), {0, 0, 0, 1});
    normalized.insert(normalized.end(), input.begin() + offset,
                      input.begin() + offset + length);
    offset += length;
  }
  if (normalized.empty())
    return {};
  output = std::move(normalized);
  return {.valid = true, .keyframe = keyframe};
}

} // namespace shareme::rtc
