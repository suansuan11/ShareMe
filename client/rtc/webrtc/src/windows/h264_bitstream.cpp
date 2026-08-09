#include "windows/h264_bitstream.hpp"

#include <limits>
#include <optional>

namespace shareme::rtc {
namespace {

struct StartCode {
  std::size_t offset;
  std::size_t size;
};

std::optional<StartCode> find_start_code(std::span<const std::uint8_t> input,
                                         std::size_t offset) {
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

std::uint32_t read_big_endian_length(std::span<const std::uint8_t> input,
                                     std::size_t offset,
                                     std::size_t length_field_bytes) {
  std::uint32_t length = 0;
  for (std::size_t index = 0; index < length_field_bytes; ++index)
    length = (length << 8U) | input[offset + index];
  return length;
}

void classify_nal(std::uint8_t header, H264AccessUnitInfo &info) {
  switch (header & 0x1fU) {
  case 7:
    info.has_sps = true;
    break;
  case 8:
    info.has_pps = true;
    break;
  case 5:
    info.has_idr = true;
    break;
  default:
    break;
  }
  ++info.nal_unit_count;
}

bool exceeds_limit(std::size_t output_size, std::size_t addition,
                   std::size_t max_output_bytes) {
  return output_size > max_output_bytes || addition > max_output_bytes - output_size;
}

} // namespace

bool H264AnnexBConversion::has_value() const noexcept {
  return error_ == H264BitstreamError::none;
}

H264BitstreamError H264AnnexBConversion::error() const noexcept {
  return error_;
}

bool H264AnnexBConversion::uses_input_storage() const noexcept {
  return has_value() && !borrowed_annex_b_.empty();
}

std::span<const std::uint8_t> H264AnnexBConversion::value() const noexcept {
  return uses_input_storage()
             ? borrowed_annex_b_
             : std::span<const std::uint8_t>(converted_annex_b_);
}

std::span<const std::uint8_t> H264AnnexBConversion::operator*() const noexcept {
  return value();
}

H264AccessUnitInfo inspect_annex_b(std::span<const std::uint8_t> input) {
  if (input.empty())
    return {.error = H264BitstreamError::empty_input};

  const auto first = find_start_code(input, 0);
  if (!first.has_value() || first->offset != 0)
    return {.error = H264BitstreamError::malformed_annex_b};

  H264AccessUnitInfo info;
  auto current = *first;
  while (true) {
    const auto payload_offset = current.offset + current.size;
    const auto next = find_start_code(input, payload_offset);
    const auto end_offset = next.has_value() ? next->offset : input.size();
    if (payload_offset >= end_offset)
      return {.error = H264BitstreamError::malformed_annex_b};
    if ((input[payload_offset] & 0x80U) != 0 ||
        (input[payload_offset] & 0x1fU) == 0) {
      return {.error = H264BitstreamError::malformed_annex_b};
    }
    classify_nal(input[payload_offset], info);
    if (!next.has_value())
      return info;
    current = *next;
  }
}

H264AnnexBConversion convert_avcc_to_annex_b(
    std::span<const std::uint8_t> input, std::size_t length_field_bytes,
    std::size_t max_output_bytes) {
  const auto annex_b_info = inspect_annex_b(input);
  if (annex_b_info.has_value()) {
    if (input.size() > max_output_bytes)
      return H264AnnexBConversion{H264BitstreamError::output_limit_exceeded};
    H264AnnexBConversion result;
    result.error_ = H264BitstreamError::none;
    result.borrowed_annex_b_ = input;
    return result;
  }

  if (length_field_bytes != 3 && length_field_bytes != 4)
    return H264AnnexBConversion{
        H264BitstreamError::unsupported_length_field_size};
  if (input.empty())
    return H264AnnexBConversion{H264BitstreamError::empty_input};

  std::size_t offset = 0;
  std::size_t output_size = 0;
  while (offset < input.size()) {
    if (input.size() - offset < length_field_bytes)
      return H264AnnexBConversion{H264BitstreamError::truncated_length_field};
    const auto nal_size = static_cast<std::size_t>(
        read_big_endian_length(input, offset, length_field_bytes));
    offset += length_field_bytes;
    if (nal_size == 0)
      return H264AnnexBConversion{H264BitstreamError::zero_nal_length};
    if (nal_size > input.size() - offset)
      return H264AnnexBConversion{H264BitstreamError::truncated_nal_unit};
    if (nal_size > std::numeric_limits<std::size_t>::max() - 4)
      return H264AnnexBConversion{H264BitstreamError::size_overflow};
    const auto output_addition = 4 + nal_size;
    if (exceeds_limit(output_size, output_addition, max_output_bytes))
      return H264AnnexBConversion{H264BitstreamError::output_limit_exceeded};
    output_size += output_addition;
    offset += nal_size;
  }

  H264AnnexBConversion result;
  result.error_ = H264BitstreamError::none;
  result.converted_annex_b_.resize(output_size);
  offset = 0;
  std::size_t output_offset = 0;
  while (offset < input.size()) {
    const auto nal_size = static_cast<std::size_t>(
        read_big_endian_length(input, offset, length_field_bytes));
    offset += length_field_bytes;
    auto *destination = result.converted_annex_b_.data() + output_offset;
    destination[0] = 0;
    destination[1] = 0;
    destination[2] = 0;
    destination[3] = 1;
    for (std::size_t index = 0; index < nal_size; ++index)
      destination[4 + index] = input[offset + index];
    output_offset += 4 + nal_size;
    offset += nal_size;
  }
  return result;
}

} // namespace shareme::rtc
