#include "shareme/core/screen_stream_profile.hpp"

#include <algorithm>
#include <cstdint>

namespace shareme::core {

std::optional<ScreenStreamProfile> parse_screen_stream_profile(
    std::string_view value) noexcept {
  if (value == "standard")
    return ScreenStreamProfile::standard;
  if (value == "quality")
    return ScreenStreamProfile::quality;
  if (value == "cinema")
    return ScreenStreamProfile::cinema;
  return std::nullopt;
}

ScreenDimensions fit_screen_dimensions(
    int source_width, int source_height,
    ScreenStreamProfile profile) noexcept {
  if (source_width <= 0 || source_height <= 0)
    return {};

  const auto bounds = screen_stream_profile_bounds(profile);
  if (source_width <= bounds.max_width && source_height <= bounds.max_height)
    return {.width = source_width, .height = source_height};

  // int dimensions squared stay within int64_t, so the comparison cannot
  // overflow for the accepted platform dimensions.
  const auto width_at_max_height = static_cast<std::int64_t>(
      bounds.max_height) * source_width;
  const auto height_at_max_width = static_cast<std::int64_t>(
      bounds.max_width) * source_height;
  if (height_at_max_width <= width_at_max_height) {
    const auto height = static_cast<int>(
        static_cast<std::int64_t>(bounds.max_width) * source_height /
        source_width);
    return height > 0 ? ScreenDimensions{bounds.max_width, height}
                      : ScreenDimensions{};
  }

  const auto width = static_cast<int>(
      static_cast<std::int64_t>(bounds.max_height) * source_width /
      source_height);
  return width > 0 ? ScreenDimensions{width, bounds.max_height}
                   : ScreenDimensions{};
}

}  // namespace shareme::core
