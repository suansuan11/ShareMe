#include "windows/h264_bitstream.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void converts_four_byte_avcc_and_classifies_parameter_sets_and_idr() {
  const std::array<std::uint8_t, 19> avcc{
      0, 0, 0, 2, 0x67, 0x01, 0, 0, 0, 2,
      0x68, 0x02, 0, 0, 0, 3, 0x65, 0x03, 0x04};

  const auto converted =
      shareme::rtc::convert_avcc_to_annex_b(avcc, 4, 64);
  REQUIRE(converted.has_value());
  const std::array<std::uint8_t, 19> expected{
      0, 0, 0, 1, 0x67, 0x01, 0, 0, 0, 1,
      0x68, 0x02, 0, 0, 0, 1, 0x65, 0x03, 0x04};
  REQUIRE(std::ranges::equal(*converted, expected));
  const auto info = shareme::rtc::inspect_annex_b(*converted);
  REQUIRE(info.has_value());
  REQUIRE(info.has_sps);
  REQUIRE(info.has_pps);
  REQUIRE(info.has_idr);
}

void converts_three_byte_avcc() {
  const std::array<std::uint8_t, 9> avcc{
      0, 0, 2, 0x67, 0x01, 0, 0, 1, 0x65};

  const auto converted =
      shareme::rtc::convert_avcc_to_annex_b(avcc, 3, 64);
  REQUIRE(converted.has_value());
  const std::array<std::uint8_t, 11> expected{
      0, 0, 0, 1, 0x67, 0x01, 0, 0, 0, 1, 0x65};
  REQUIRE(std::ranges::equal(*converted, expected));
}

void accepts_annex_b_without_copying_the_retained_input() {
  const std::array<std::uint8_t, 11> annex_b{
      0, 0, 1, 0x67, 0x01, 0, 0, 0, 1, 0x65, 0x02};

  const auto converted =
      shareme::rtc::convert_avcc_to_annex_b(annex_b, 4, annex_b.size());
  REQUIRE(converted.has_value());
  REQUIRE(converted.uses_input_storage());
  REQUIRE(std::span<const std::uint8_t>(*converted).data() == annex_b.data());
}

void rejects_malformed_inputs_with_typed_errors() {
  const std::array<std::uint8_t, 5> zero_length{0, 0, 0, 0, 0x65};
  const auto zero = shareme::rtc::convert_avcc_to_annex_b(zero_length, 4, 64);
  REQUIRE(!zero.has_value());
  REQUIRE(zero.error() == shareme::rtc::H264BitstreamError::zero_nal_length);

  const std::array<std::uint8_t, 3> truncated_size{0, 0, 0};
  const auto size =
      shareme::rtc::convert_avcc_to_annex_b(truncated_size, 4, 64);
  REQUIRE(!size.has_value());
  REQUIRE(size.error() ==
          shareme::rtc::H264BitstreamError::truncated_length_field);

  const std::array<std::uint8_t, 6> truncated_nal{0, 0, 0, 3, 0x65, 0x01};
  const auto nal =
      shareme::rtc::convert_avcc_to_annex_b(truncated_nal, 4, 64);
  REQUIRE(!nal.has_value());
  REQUIRE(nal.error() == shareme::rtc::H264BitstreamError::truncated_nal_unit);

  const std::array<std::uint8_t, 1> empty{};
  const auto unsupported =
      shareme::rtc::convert_avcc_to_annex_b(empty, 2, 64);
  REQUIRE(!unsupported.has_value());
  REQUIRE(unsupported.error() ==
          shareme::rtc::H264BitstreamError::unsupported_length_field_size);

  const std::array<std::uint8_t, 6> too_large{0, 0, 0, 2, 0x65, 0x01};
  const auto limit = shareme::rtc::convert_avcc_to_annex_b(too_large, 4, 5);
  REQUIRE(!limit.has_value());
  REQUIRE(limit.error() ==
          shareme::rtc::H264BitstreamError::output_limit_exceeded);
}

void rejects_annex_b_with_empty_nal_units() {
  const std::array<std::uint8_t, 8> malformed{0, 0, 1, 0x67, 0, 0, 1, 0};
  const auto info = shareme::rtc::inspect_annex_b(malformed);
  REQUIRE(!info.has_value());
  REQUIRE(info.error == shareme::rtc::H264BitstreamError::malformed_annex_b);
}

} // namespace

int main() {
  converts_four_byte_avcc_and_classifies_parameter_sets_and_idr();
  converts_three_byte_avcc();
  accepts_annex_b_without_copying_the_retained_input();
  rejects_malformed_inputs_with_typed_errors();
  rejects_annex_b_with_empty_nal_units();
  return EXIT_SUCCESS;
}
