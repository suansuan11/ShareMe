#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace shareme::core {

enum class ScreenStreamProfile {
  standard,
  quality,
  cinema,
};

struct ScreenStreamProfileBounds {
  int max_width = 0;
  int max_height = 0;
  int max_frames_per_second = 0;

  friend constexpr bool operator==(
      ScreenStreamProfileBounds, ScreenStreamProfileBounds) = default;
};

struct ScreenDimensions {
  int width = 0;
  int height = 0;

  friend constexpr bool operator==(ScreenDimensions, ScreenDimensions) =
      default;
};

[[nodiscard]] constexpr ScreenStreamProfileBounds screen_stream_profile_bounds(
    ScreenStreamProfile profile) noexcept {
  switch (profile) {
  case ScreenStreamProfile::standard:
    return {.max_width = 1'920,
            .max_height = 1'080,
            .max_frames_per_second = 60};
  case ScreenStreamProfile::quality:
    return {.max_width = 2'560,
            .max_height = 1'440,
            .max_frames_per_second = 60};
  case ScreenStreamProfile::cinema:
    return {.max_width = 3'840,
            .max_height = 2'160,
            .max_frames_per_second = 30};
  }
  return {};
}

[[nodiscard]] std::optional<ScreenStreamProfile> parse_screen_stream_profile(
    std::string_view value) noexcept;

[[nodiscard]] ScreenDimensions fit_screen_dimensions(
    int source_width, int source_height,
    ScreenStreamProfile profile) noexcept;

}  // namespace shareme::core
