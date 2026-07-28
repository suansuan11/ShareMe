#include "shareme/media/media_time.hpp"

#include <stdexcept>

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace shareme::media {

std::optional<std::int64_t> to_milliseconds(
    std::int64_t timestamp,
    Rational time_base) {
  if (time_base.numerator <= 0 || time_base.denominator <= 0) {
    throw std::invalid_argument{"Media time base must be positive"};
  }
  if (timestamp == kNoTimestamp) {
    return std::nullopt;
  }

  const AVRational source_time_base{
      time_base.numerator,
      time_base.denominator,
  };
  constexpr AVRational milliseconds_time_base{1, 1'000};
  return av_rescale_q(timestamp, source_time_base, milliseconds_time_base);
}

}  // namespace shareme::media
