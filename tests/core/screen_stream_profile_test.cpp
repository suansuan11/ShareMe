#include "shareme/core/screen_stream_profile.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

using namespace shareme::core;

void profile_bounds_are_frozen() {
  const ScreenStreamProfileBounds standard{1'920, 1'080, 60};
  const ScreenStreamProfileBounds quality{2'560, 1'440, 60};
  const ScreenStreamProfileBounds cinema{3'840, 2'160, 30};
  REQUIRE(screen_stream_profile_bounds(ScreenStreamProfile::standard) ==
          standard);
  REQUIRE(screen_stream_profile_bounds(ScreenStreamProfile::quality) ==
          quality);
  REQUIRE(screen_stream_profile_bounds(ScreenStreamProfile::cinema) ==
          cinema);
}

void profile_names_round_trip_and_reject_unknown_values() {
  REQUIRE(parse_screen_stream_profile("standard") ==
          ScreenStreamProfile::standard);
  REQUIRE(parse_screen_stream_profile("quality") ==
          ScreenStreamProfile::quality);
  REQUIRE(parse_screen_stream_profile("cinema") ==
          ScreenStreamProfile::cinema);
  REQUIRE(!parse_screen_stream_profile("extreme"));
  REQUIRE(!parse_screen_stream_profile("1080p60"));
}

void fitting_preserves_aspect_and_never_upscales() {
  const ScreenDimensions quality_dimensions{2'560, 1'440};
  const ScreenDimensions cinema_dimensions{3'840, 2'160};
  const ScreenDimensions source_dimensions{1'280, 720};
  REQUIRE(fit_screen_dimensions(3'840, 2'160, ScreenStreamProfile::quality) ==
          quality_dimensions);
  REQUIRE(fit_screen_dimensions(5'120, 2'880, ScreenStreamProfile::cinema) ==
          cinema_dimensions);
  REQUIRE(fit_screen_dimensions(1'280, 720, ScreenStreamProfile::quality) ==
          source_dimensions);

  const auto ultrawide =
      fit_screen_dimensions(3'440, 1'440, ScreenStreamProfile::quality);
  REQUIRE(ultrawide.width == 2'560);
  REQUIRE(ultrawide.height > 0);
  REQUIRE(ultrawide.height < 1'440);
  REQUIRE(ultrawide.width * 1'440 - ultrawide.height * 3'440 <= 3'440);
}

void invalid_dimensions_return_an_empty_fit() {
  const ScreenDimensions empty{};
  REQUIRE(fit_screen_dimensions(0, 720, ScreenStreamProfile::standard) == empty);
  REQUIRE(fit_screen_dimensions(1'920, -1, ScreenStreamProfile::standard) ==
          empty);
}

}  // namespace

int main() {
  profile_bounds_are_frozen();
  profile_names_round_trip_and_reject_unknown_values();
  fitting_preserves_aspect_and_never_upscales();
  invalid_dimensions_return_an_empty_fit();
  return EXIT_SUCCESS;
}
