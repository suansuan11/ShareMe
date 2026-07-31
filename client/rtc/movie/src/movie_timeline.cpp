#include "shareme/rtc/movie_timeline.hpp"

namespace shareme::rtc {

MovieTimeline::TimePoint MovieTimeline::start() {
  std::lock_guard lock(mutex_);
  if (!epoch_)
    epoch_ = Clock::now();
  return *epoch_;
}

std::optional<MovieTimeline::TimePoint> MovieTimeline::epoch() const {
  std::lock_guard lock(mutex_);
  return epoch_;
}

} // namespace shareme::rtc
