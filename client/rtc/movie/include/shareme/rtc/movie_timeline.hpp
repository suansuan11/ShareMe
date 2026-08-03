#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>

namespace shareme::rtc {

enum class MovieTimelineState { playing, paused };
enum class MovieTimelineWaitResult { due, generation_changed, stopped };

struct MovieTimelineSnapshot {
  MovieTimelineState state{MovieTimelineState::paused};
  std::int64_t start_pts_ms{};
  std::int64_t duration_ms{};
  std::int64_t media_pts_ms{};
  std::uint64_t generation{};
  std::uint64_t revision{};
};

class MovieTimeline {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  [[nodiscard]] bool initialize(std::int64_t start_pts_ms,
                                std::int64_t duration_ms);
  [[nodiscard]] std::optional<MovieTimelineSnapshot> snapshot() const;
  [[nodiscard]] bool pause();
  [[nodiscard]] bool resume();
  [[nodiscard]] bool seek(std::int64_t target_pts_ms);
  [[nodiscard]] MovieTimelineWaitResult
  wait_until(std::int64_t target_pts_ms, std::uint64_t generation,
             std::stop_token stop_token) const;

private:
  [[nodiscard]] std::int64_t current_pts_locked(TimePoint now) const noexcept;

  mutable std::mutex mutex_;
  mutable std::condition_variable_any changed_;
  bool initialized_{false};
  MovieTimelineState state_{MovieTimelineState::paused};
  std::int64_t start_pts_ms_{};
  std::int64_t duration_ms_{};
  std::int64_t end_pts_ms_{};
  std::int64_t anchor_pts_ms_{};
  TimePoint anchor_time_{};
  std::uint64_t generation_{};
  std::uint64_t revision_{};
};

} // namespace shareme::rtc
