#include "desktop_frame_converter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

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

void converts_padded_bgra_rows_without_reading_padding() {
  constexpr int width = 4;
  constexpr int height = 2;
  constexpr std::size_t row_pitch = 20;
  std::array<std::uint8_t, row_pitch * height> pixels{};
  for (int y = 0; y < height; ++y) {
    auto *row = pixels.data() + static_cast<std::size_t>(y) * row_pitch;
    for (int x = 0; x < width; ++x) {
      const auto white = x % 2 != 0;
      row[x * 4 + 0] = white ? 255 : 0;
      row[x * 4 + 1] = white ? 255 : 0;
      row[x * 4 + 2] = white ? 255 : 0;
      row[x * 4 + 3] = 255;
    }
    row[16] = 255;
    row[17] = 0;
    row[18] = 255;
    row[19] = 255;
  }

  const auto converted = shareme::rtc::detail::convert_mapped_bgra_to_i420(
      {.data = pixels.data(),
       .width = width,
       .height = height,
       .row_pitch = row_pitch});

  REQUIRE(converted != nullptr);
  REQUIRE(converted->width() == width);
  REQUIRE(converted->height() == height);
  REQUIRE(converted->DataY()[0] < 32);
  REQUIRE(converted->DataY()[1] > 200);
  REQUIRE(converted->DataY()[converted->StrideY()] < 32);
  REQUIRE(converted->DataY()[converted->StrideY() + 1] > 200);
}

void rejects_invalid_mapped_views() {
  std::array<std::uint8_t, 16> pixels{};
  using shareme::rtc::detail::MappedBgraFrame;
  using shareme::rtc::detail::convert_mapped_bgra_to_i420;

  REQUIRE(convert_mapped_bgra_to_i420(
              MappedBgraFrame{nullptr, 1, 1, 4}) == nullptr);
  REQUIRE(convert_mapped_bgra_to_i420(
              MappedBgraFrame{pixels.data(), 0, 1, 4}) == nullptr);
  REQUIRE(convert_mapped_bgra_to_i420(
              MappedBgraFrame{pixels.data(), 4, 1, 15}) == nullptr);
  REQUIRE(convert_mapped_bgra_to_i420(
              MappedBgraFrame{pixels.data(), 1, 2,
                              std::numeric_limits<std::size_t>::max()}) ==
          nullptr);
}

void converts_hdr_float_rows_directly_to_i420() {
  constexpr int width = 2;
  constexpr int height = 2;
  constexpr std::size_t row_pitch = 20;
  constexpr std::uint16_t zero = 0x0000;
  constexpr std::uint16_t one = 0x3c00;
  std::array<std::uint16_t, row_pitch * height / sizeof(std::uint16_t)>
      pixels{};
  for (int y = 0; y < height; ++y) {
    auto *row = pixels.data() +
                static_cast<std::size_t>(y) * row_pitch / sizeof(std::uint16_t);
    row[0] = zero;
    row[1] = zero;
    row[2] = zero;
    row[3] = one;
    row[4] = one;
    row[5] = one;
    row[6] = one;
    row[7] = one;
  }

  const auto converted =
      shareme::rtc::detail::convert_mapped_rgba16_float_to_i420(
          {.data = reinterpret_cast<const std::uint8_t *>(pixels.data()),
           .width = width,
           .height = height,
           .row_pitch = row_pitch});

  REQUIRE(converted != nullptr);
  REQUIRE(converted->DataY()[0] < 32);
  REQUIRE(converted->DataY()[1] > 220);
  REQUIRE(converted->DataY()[converted->StrideY()] < 32);
  REQUIRE(converted->DataY()[converted->StrideY() + 1] > 220);
  REQUIRE(converted->DataU()[0] > 120 && converted->DataU()[0] < 136);
  REQUIRE(converted->DataV()[0] > 120 && converted->DataV()[0] < 136);
}

void handles_hdr_nonfinite_values_and_odd_dimensions() {
  constexpr int width = 3;
  constexpr int height = 3;
  constexpr std::size_t row_pitch = 26;
  constexpr std::uint16_t positive_infinity = 0x7c00;
  constexpr std::uint16_t quiet_nan = 0x7e00;
  constexpr std::uint16_t negative_one = 0xbc00;
  constexpr std::uint16_t one = 0x3c00;
  std::array<std::uint16_t, row_pitch * height / sizeof(std::uint16_t)>
      pixels{};
  for (int y = 0; y < height; ++y) {
    auto *row = pixels.data() +
                static_cast<std::size_t>(y) * row_pitch / sizeof(std::uint16_t);
    for (int x = 0; x < width; ++x) {
      row[x * 4 + 0] = positive_infinity;
      row[x * 4 + 1] = positive_infinity;
      row[x * 4 + 2] = positive_infinity;
      row[x * 4 + 3] = one;
    }
  }
  pixels[0] = quiet_nan;
  pixels[1] = quiet_nan;
  pixels[2] = quiet_nan;
  pixels[4] = negative_one;
  pixels[5] = negative_one;
  pixels[6] = negative_one;

  using shareme::rtc::detail::MappedRgba16FloatFrame;
  using shareme::rtc::detail::convert_mapped_rgba16_float_to_i420;
  const auto converted = convert_mapped_rgba16_float_to_i420(
      {reinterpret_cast<const std::uint8_t *>(pixels.data()), width, height,
       row_pitch});

  REQUIRE(converted != nullptr);
  REQUIRE(converted->width() == width);
  REQUIRE(converted->height() == height);
  REQUIRE(converted->DataY()[0] < 32);
  REQUIRE(converted->DataY()[1] < 32);
  REQUIRE(converted->DataY()[2] > 220);
  REQUIRE(converted->DataY()[2 * converted->StrideY() + 2] > 220);
  REQUIRE(convert_mapped_rgba16_float_to_i420(
              MappedRgba16FloatFrame{
                  reinterpret_cast<const std::uint8_t *>(pixels.data()),
                  width, height, width * 8U - 1U}) == nullptr);
}

} // namespace

int main() {
  converts_padded_bgra_rows_without_reading_padding();
  converts_hdr_float_rows_directly_to_i420();
  handles_hdr_nonfinite_values_and_odd_dimensions();
  rejects_invalid_mapped_views();
  return EXIT_SUCCESS;
}
