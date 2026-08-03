#include "shareme/rtc/movie_timeline.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

namespace {
void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void controls_state_and_generation() {
  using namespace std::chrono_literals;
  using shareme::rtc::MovieTimeline;
  using shareme::rtc::MovieTimelineState;

  MovieTimeline timeline;
  REQUIRE(!timeline.snapshot());
  REQUIRE(!timeline.pause());
  REQUIRE(!timeline.resume());
  REQUIRE(!timeline.seek(0));
  REQUIRE(timeline.initialize(5'000, 2'000));
  REQUIRE(timeline.initialize(5'000, 2'000));
  REQUIRE(!timeline.initialize(5'001, 2'000));
  REQUIRE(!timeline.initialize(5'000, 2'001));

  const auto initial = timeline.snapshot();
  REQUIRE(initial.has_value());
  REQUIRE(initial->state == MovieTimelineState::playing);
  REQUIRE(initial->start_pts_ms == 5'000);
  REQUIRE(initial->duration_ms == 2'000);
  REQUIRE(initial->generation == 0);

  REQUIRE(timeline.pause());
  REQUIRE(!timeline.pause());
  const auto paused = *timeline.snapshot();
  std::this_thread::sleep_for(30ms);
  const auto still_paused = *timeline.snapshot();
  REQUIRE(still_paused.state == MovieTimelineState::paused);
  REQUIRE(still_paused.media_pts_ms == paused.media_pts_ms);
  REQUIRE(still_paused.generation == 0);
  REQUIRE(still_paused.revision == paused.revision);

  REQUIRE(timeline.seek(6'500));
  const auto sought_paused = *timeline.snapshot();
  REQUIRE(sought_paused.state == MovieTimelineState::paused);
  REQUIRE(sought_paused.media_pts_ms == 6'500);
  REQUIRE(sought_paused.generation == 1);
  REQUIRE(!timeline.seek(4'999));
  REQUIRE(!timeline.seek(7'001));
  REQUIRE(timeline.snapshot()->generation == 1);

  REQUIRE(timeline.resume());
  REQUIRE(!timeline.resume());
  std::this_thread::sleep_for(20ms);
  const auto resumed = *timeline.snapshot();
  REQUIRE(resumed.state == MovieTimelineState::playing);
  REQUIRE(resumed.media_pts_ms >= 6'510);
  REQUIRE(resumed.media_pts_ms <= 7'000);
  REQUIRE(resumed.generation == 1);
}

void rejects_invalid_ranges() {
  shareme::rtc::MovieTimeline timeline;
  REQUIRE(!timeline.initialize(0, -1));
  REQUIRE(!timeline.initialize(std::numeric_limits<std::int64_t>::max(), 1));
  REQUIRE(!timeline.snapshot());
}

void waits_for_due_generation_or_stop() {
  using namespace std::chrono_literals;
  using shareme::rtc::MovieTimeline;
  using shareme::rtc::MovieTimelineWaitResult;

  MovieTimeline due;
  REQUIRE(due.initialize(0, 1'000));
  REQUIRE(due.wait_until(20, 0, {}) == MovieTimelineWaitResult::due);

  MovieTimeline changed;
  REQUIRE(changed.initialize(0, 5'000));
  std::promise<MovieTimelineWaitResult> changed_promise;
  auto changed_future = changed_promise.get_future();
  std::jthread changed_waiter([&](std::stop_token stop_token) {
    changed_promise.set_value(changed.wait_until(4'000, 0, stop_token));
  });
  std::this_thread::sleep_for(20ms);
  REQUIRE(changed.seek(2'000));
  REQUIRE(changed_future.wait_for(500ms) == std::future_status::ready);
  REQUIRE(changed_future.get() == MovieTimelineWaitResult::generation_changed);

  MovieTimeline stopped;
  REQUIRE(stopped.initialize(0, 5'000));
  std::promise<MovieTimelineWaitResult> stopped_promise;
  auto stopped_future = stopped_promise.get_future();
  std::jthread stopped_waiter([&](std::stop_token stop_token) {
    stopped_promise.set_value(stopped.wait_until(4'000, 0, stop_token));
  });
  std::this_thread::sleep_for(20ms);
  stopped_waiter.request_stop();
  REQUIRE(stopped_future.wait_for(500ms) == std::future_status::ready);
  REQUIRE(stopped_future.get() == MovieTimelineWaitResult::stopped);
}
} // namespace

int main() {
  controls_state_and_generation();
  rejects_invalid_ranges();
  waits_for_due_generation_or_stop();
  return EXIT_SUCCESS;
}
