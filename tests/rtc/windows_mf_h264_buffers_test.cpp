#include "windows_mf_h264_buffers.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

#include "api/video/i420_buffer.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void copies_i420_into_padded_nv12_without_touching_padding() {
  auto input = webrtc::I420Buffer::Create(4, 2);
  const std::uint8_t y[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const std::uint8_t u[] = {9, 10};
  const std::uint8_t v[] = {11, 12};
  std::copy(y, y + 8, input->MutableDataY());
  std::copy(u, u + 2, input->MutableDataU());
  std::copy(v, v + 2, input->MutableDataV());

  std::vector<std::byte> output(18, std::byte{0xee});
  REQUIRE(shareme::rtc::copy_i420_to_nv12(*input, output, 6));
  const std::vector<std::byte> expected{
      std::byte{1},  std::byte{2},  std::byte{3},  std::byte{4},
      std::byte{0xee}, std::byte{0xee}, std::byte{5},  std::byte{6},
      std::byte{7},  std::byte{8},  std::byte{0xee}, std::byte{0xee},
      std::byte{9},  std::byte{11}, std::byte{10}, std::byte{12},
      std::byte{0xee}, std::byte{0xee}};
  REQUIRE(output == expected);
}

void rejects_invalid_nv12_storage_without_partial_writes() {
  auto input = webrtc::I420Buffer::Create(4, 2);
  std::vector<std::byte> output(17, std::byte{0x7a});
  REQUIRE(!shareme::rtc::copy_i420_to_nv12(*input, output, 6));
  REQUIRE(std::all_of(output.begin(), output.end(), [](std::byte value) {
    return value == std::byte{0x7a};
  }));
}

void copies_padded_nv12_into_i420() {
  const std::vector<std::uint8_t> input{
      1, 2, 3, 4, 99, 99, 5, 6, 7, 8, 99, 99,
      9, 11, 10, 12, 99, 99};
  auto output = webrtc::I420Buffer::Create(4, 2);
  REQUIRE(shareme::rtc::copy_nv12_to_i420(
      input, 4, 2, 6, static_cast<webrtc::I420Buffer &>(*output)));
  REQUIRE(std::equal(output->DataY(), output->DataY() + 4,
                     std::vector<std::uint8_t>{1, 2, 3, 4}.begin()));
  REQUIRE(std::equal(output->DataY() + output->StrideY(),
                     output->DataY() + output->StrideY() + 4,
                     std::vector<std::uint8_t>{5, 6, 7, 8}.begin()));
  REQUIRE(output->DataU()[0] == 9);
  REQUIRE(output->DataU()[1] == 10);
  REQUIRE(output->DataV()[0] == 11);
  REQUIRE(output->DataV()[1] == 12);
}

void normalizes_annex_b_and_avcc_access_units() {
  const std::vector<std::uint8_t> annex_b{
      0, 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce,
      0, 0, 0, 1, 0x65, 0xaa};
  std::vector<std::uint8_t> output;
  const auto annex_result =
      shareme::rtc::normalize_h264_access_unit(annex_b, output);
  REQUIRE(annex_result.valid);
  REQUIRE(annex_result.keyframe);
  REQUIRE(output == annex_b);

  const std::vector<std::uint8_t> avcc{
      0, 0, 0, 2, 0x67, 0x42, 0, 0, 0, 2, 0x65, 0xaa};
  const std::vector<std::uint8_t> expected{
      0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x65, 0xaa};
  const auto avcc_result =
      shareme::rtc::normalize_h264_access_unit(avcc, output);
  REQUIRE(avcc_result.valid);
  REQUIRE(avcc_result.keyframe);
  REQUIRE(output == expected);
}

void rejects_malformed_avcc_without_mutating_output() {
  const std::vector<std::uint8_t> malformed{0, 0, 0, 5, 0x65, 0xaa};
  std::vector<std::uint8_t> output{1, 2, 3};
  const auto result =
      shareme::rtc::normalize_h264_access_unit(malformed, output);
  REQUIRE(!result.valid);
  REQUIRE(!result.keyframe);
  REQUIRE(output == std::vector<std::uint8_t>({1, 2, 3}));
}

} // namespace

int main() {
  copies_i420_into_padded_nv12_without_touching_padding();
  rejects_invalid_nv12_storage_without_partial_writes();
  copies_padded_nv12_into_i420();
  normalizes_annex_b_and_avcc_access_units();
  rejects_malformed_avcc_without_mutating_output();
  return EXIT_SUCCESS;
}
