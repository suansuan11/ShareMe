#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace shareme::rtc {

enum class H264BitstreamError {
  none,
  empty_input,
  malformed_annex_b,
  unsupported_length_field_size,
  zero_nal_length,
  truncated_length_field,
  truncated_nal_unit,
  output_limit_exceeded,
  size_overflow,
};

struct H264AccessUnitInfo {
  H264BitstreamError error{H264BitstreamError::none};
  bool has_sps{false};
  bool has_pps{false};
  bool has_idr{false};
  std::size_t nal_unit_count{0};

  [[nodiscard]] bool has_value() const noexcept {
    return error == H264BitstreamError::none;
  }
};

class H264AnnexBConversion {
public:
  H264AnnexBConversion() = default;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] H264BitstreamError error() const noexcept;
  [[nodiscard]] bool uses_input_storage() const noexcept;
  [[nodiscard]] std::span<const std::uint8_t> value() const noexcept;
  [[nodiscard]] std::span<const std::uint8_t> operator*() const noexcept;

private:
  friend H264AnnexBConversion convert_avcc_to_annex_b(
      std::span<const std::uint8_t> input, std::size_t length_field_bytes,
      std::size_t max_output_bytes);

  explicit H264AnnexBConversion(H264BitstreamError error) : error_(error) {}

  H264BitstreamError error_{H264BitstreamError::empty_input};
  std::span<const std::uint8_t> borrowed_annex_b_;
  std::vector<std::uint8_t> converted_annex_b_;
};

[[nodiscard]] H264AccessUnitInfo inspect_annex_b(
    std::span<const std::uint8_t> input);

[[nodiscard]] H264AnnexBConversion convert_avcc_to_annex_b(
    std::span<const std::uint8_t> input, std::size_t length_field_bytes,
    std::size_t max_output_bytes);

} // namespace shareme::rtc
