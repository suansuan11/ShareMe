#include "shareme/media/media_time.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)
#define REQUIRE_FALSE(expression) require(!(expression), "!(" #expression ")", __LINE__)

void converts_common_time_bases() {
  using shareme::media::Rational;
  using shareme::media::to_milliseconds;

  REQUIRE(to_milliseconds(90'000, Rational{1, 90'000}) == 1'000);
  REQUIRE(to_milliseconds(45'000, Rational{1, 90'000}) == 500);
  REQUIRE(to_milliseconds(-45'000, Rational{1, 90'000}) == -500);
}

void preserves_missing_timestamps() {
  using shareme::media::Rational;
  using shareme::media::kNoTimestamp;
  using shareme::media::to_milliseconds;

  REQUIRE_FALSE(to_milliseconds(kNoTimestamp, Rational{1, 90'000}).has_value());
}

void rejects_invalid_time_bases() {
  using shareme::media::Rational;
  using shareme::media::to_milliseconds;

  bool threw = false;
  try {
    static_cast<void>(to_milliseconds(1, Rational{1, 0}));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  REQUIRE(threw);
}

}  // namespace

int main() {
  converts_common_time_bases();
  preserves_missing_timestamps();
  rejects_invalid_time_bases();
  return EXIT_SUCCESS;
}
