#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace shareme::media {

inline constexpr auto kNoTimestamp = std::numeric_limits<std::int64_t>::min();

struct Rational {
  int numerator;
  int denominator;
};

[[nodiscard]] std::optional<std::int64_t> to_milliseconds(
    std::int64_t timestamp,
    Rational time_base);

}  // namespace shareme::media
