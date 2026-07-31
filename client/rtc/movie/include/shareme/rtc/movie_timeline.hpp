#pragma once

#include <chrono>
#include <mutex>
#include <optional>

namespace shareme::rtc {

class MovieTimeline {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  [[nodiscard]] TimePoint start();
  [[nodiscard]] std::optional<TimePoint> epoch() const;

private:
  mutable std::mutex mutex_;
  std::optional<TimePoint> epoch_;
};

} // namespace shareme::rtc
