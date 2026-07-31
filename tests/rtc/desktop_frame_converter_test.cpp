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

} // namespace

int main() {
  converts_padded_bgra_rows_without_reading_padding();
  rejects_invalid_mapped_views();
  return EXIT_SUCCESS;
}
