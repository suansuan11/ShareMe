#include "shareme/rtc/movie_timeline.hpp"

#include <limits>

namespace shareme::rtc {

bool MovieTimeline::initialize(std::int64_t start_pts_ms,
                               std::int64_t duration_ms) {
  if (duration_ms < 0 ||
      start_pts_ms > std::numeric_limits<std::int64_t>::max() - duration_ms)
    return false;

  std::lock_guard lock(mutex_);
  if (initialized_)
    return start_pts_ms_ == start_pts_ms && duration_ms_ == duration_ms;
  initialized_ = true;
  state_ = MovieTimelineState::playing;
  start_pts_ms_ = start_pts_ms;
  duration_ms_ = duration_ms;
  end_pts_ms_ = start_pts_ms + duration_ms;
  anchor_pts_ms_ = start_pts_ms;
  anchor_time_ = Clock::now();
  generation_ = 0;
  revision_ = 0;
  return true;
}

std::optional<MovieTimelineSnapshot> MovieTimeline::snapshot() const {
  std::lock_guard lock(mutex_);
  if (!initialized_)
    return std::nullopt;
  return MovieTimelineSnapshot{.state = state_,
                               .start_pts_ms = start_pts_ms_,
                               .duration_ms = duration_ms_,
                               .media_pts_ms = current_pts_locked(Clock::now()),
                               .generation = generation_,
                               .revision = revision_};
}

bool MovieTimeline::pause() {
  {
    std::lock_guard lock(mutex_);
    if (!initialized_ || state_ != MovieTimelineState::playing ||
        revision_ == std::numeric_limits<std::uint64_t>::max())
      return false;
    anchor_pts_ms_ = current_pts_locked(Clock::now());
    state_ = MovieTimelineState::paused;
    ++revision_;
  }
  changed_.notify_all();
  return true;
}

bool MovieTimeline::resume() {
  {
    std::lock_guard lock(mutex_);
    if (!initialized_ || state_ != MovieTimelineState::paused ||
        revision_ == std::numeric_limits<std::uint64_t>::max())
      return false;
    anchor_time_ = Clock::now();
    state_ = MovieTimelineState::playing;
    ++revision_;
  }
  changed_.notify_all();
  return true;
}

bool MovieTimeline::seek(std::int64_t target_pts_ms) {
  {
    std::lock_guard lock(mutex_);
    if (!initialized_ || target_pts_ms < start_pts_ms_ ||
        target_pts_ms > end_pts_ms_ ||
        generation_ == std::numeric_limits<std::uint64_t>::max() ||
        revision_ == std::numeric_limits<std::uint64_t>::max())
      return false;
    anchor_pts_ms_ = target_pts_ms;
    anchor_time_ = Clock::now();
    ++generation_;
    ++revision_;
  }
  changed_.notify_all();
  return true;
}

MovieTimelineWaitResult
MovieTimeline::wait_until(std::int64_t target_pts_ms,
                          std::uint64_t generation,
                          std::stop_token stop_token) const {
  std::unique_lock lock(mutex_);
  while (!stop_token.stop_requested()) {
    if (!initialized_ || generation != generation_)
      return MovieTimelineWaitResult::generation_changed;
    const auto now = Clock::now();
    const auto current = current_pts_locked(now);
    if (target_pts_ms <= current || current == end_pts_ms_)
      return MovieTimelineWaitResult::due;

    const auto revision = revision_;
    const auto changed = [this, generation, revision] {
      return !initialized_ || generation_ != generation ||
             revision_ != revision;
    };
    if (state_ == MovieTimelineState::paused) {
      changed_.wait(lock, stop_token, changed);
      continue;
    }

    const auto delta_ms = static_cast<std::uint64_t>(target_pts_ms) -
                          static_cast<std::uint64_t>(current);
    const auto since_epoch = now.time_since_epoch();
    const auto available =
        since_epoch < Clock::duration::zero()
            ? Clock::duration::max()
            : Clock::duration::max() - since_epoch;
    const auto maximum_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(available)
            .count();
    if (maximum_ms < 0 ||
        delta_ms > static_cast<std::uint64_t>(maximum_ms)) {
      changed_.wait(lock, stop_token, changed);
      continue;
    }
    const auto deadline =
        now + std::chrono::milliseconds(static_cast<std::int64_t>(delta_ms));
    changed_.wait_until(lock, stop_token, deadline, changed);
  }
  return MovieTimelineWaitResult::stopped;
}

std::int64_t MovieTimeline::current_pts_locked(TimePoint now) const noexcept {
  if (state_ == MovieTimelineState::paused || now <= anchor_time_)
    return anchor_pts_ms_;
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - anchor_time_)
          .count();
  if (elapsed <= 0)
    return anchor_pts_ms_;
  const auto remaining = static_cast<std::uint64_t>(end_pts_ms_) -
                         static_cast<std::uint64_t>(anchor_pts_ms_);
  if (static_cast<std::uint64_t>(elapsed) >= remaining)
    return end_pts_ms_;
  return anchor_pts_ms_ + elapsed;
}

} // namespace shareme::rtc
