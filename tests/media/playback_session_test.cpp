#include "shareme/media/playback_session.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;

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

class FakeMediaSource final : public shareme::media::IMediaSource {
public:
  shareme::media::MediaInfo open(const std::filesystem::path&) override {
    ++open_count;
    return {
        .duration_ms = 1'000,
        .has_video = true,
        .has_audio = true,
        .video_width = 160,
        .video_height = 90,
    };
  }

  shareme::media::MediaEvent read_next(std::uint64_t generation) override {
    std::scoped_lock lock{mutex_};
    if (events_.empty()) {
      return shareme::media::EndOfStream{};
    }

    auto event = std::move(events_.front());
    events_.pop_front();
    if (auto* video = std::get_if<shareme::media::VideoFrame>(&event);
        video != nullptr && video->generation == use_requested_generation) {
      video->generation = generation;
    }
    return event;
  }

  void seek(std::int64_t target_ms) override {
    ++seek_count;
    last_seek_ms = target_ms;
  }

  void close() noexcept override {
    ++close_count;
  }

  void push_video(std::int64_t pts_ms, std::uint64_t generation) {
    shareme::media::VideoFrame frame;
    frame.width = 1;
    frame.height = 1;
    frame.stride = 4;
    frame.pts_ms = pts_ms;
    frame.generation = generation;
    frame.rgba.resize(4);

    std::scoped_lock lock{mutex_};
    events_.emplace_back(std::move(frame));
  }

  static constexpr std::uint64_t use_requested_generation =
      std::numeric_limits<std::uint64_t>::max();

  std::atomic<int> open_count{0};
  std::atomic<int> seek_count{0};
  std::atomic<int> close_count{0};
  std::atomic<std::int64_t> last_seek_ms{-1};

private:
  std::mutex mutex_;
  std::deque<shareme::media::MediaEvent> events_;
};

template <typename Predicate>
bool wait_until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

void opens_paused_and_controls_idempotently() {
  using shareme::media::PlaybackSession;
  using shareme::media::PlaybackState;

  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};

  const auto info = session.open("movie.mp4");
  REQUIRE(info.duration_ms == 1'000);
  REQUIRE(session.state() == PlaybackState::paused);
  REQUIRE(observed_source->open_count == 1);

  session.pause();
  session.pause();
  REQUIRE(session.state() == PlaybackState::paused);

  session.play();
  session.play();
  REQUIRE(wait_until(
      [&session] { return session.state() == PlaybackState::ended; }));

  session.close();
  session.close();
  REQUIRE(observed_source->close_count == 1);
  REQUIRE(session.state() == PlaybackState::closed);
}

void seek_increments_generation_and_discards_stale_frames() {
  using shareme::media::PlaybackSession;
  using shareme::media::PlaybackState;

  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));

  const auto old_generation = session.generation();
  session.seek(600);
  REQUIRE(session.generation() == old_generation + 1);
  REQUIRE(observed_source->seek_count == 1);
  REQUIRE(observed_source->last_seek_ms == 600);
  REQUIRE(session.state() == PlaybackState::paused);

  observed_source->push_video(610, old_generation);
  observed_source->push_video(620, session.generation());
  session.play();
  REQUIRE(wait_until(
      [&session] { return session.state() == PlaybackState::ended; }));

  const auto frame = session.pop_video();
  REQUIRE(frame.has_value());
  REQUIRE(frame->pts_ms == 620);
  REQUIRE(frame->generation == session.generation());
  REQUIRE_FALSE(session.pop_video().has_value());
}

void drops_oldest_video_when_queue_is_full() {
  using shareme::media::PlaybackSession;
  using shareme::media::PlaybackState;

  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));

  observed_source->push_video(1, FakeMediaSource::use_requested_generation);
  observed_source->push_video(2, FakeMediaSource::use_requested_generation);
  observed_source->push_video(3, FakeMediaSource::use_requested_generation);
  observed_source->push_video(4, FakeMediaSource::use_requested_generation);
  session.play();
  REQUIRE(wait_until(
      [&session] { return session.state() == PlaybackState::ended; }));

  REQUIRE(session.video_dropped_count() == 1);
  REQUIRE(session.pop_video()->pts_ms == 2);
  REQUIRE(session.pop_video()->pts_ms == 3);
  REQUIRE(session.pop_video()->pts_ms == 4);
}

}  // namespace

int main() {
  opens_paused_and_controls_idempotently();
  seek_increments_generation_and_discards_stale_frames();
  drops_oldest_video_when_queue_is_full();
  return EXIT_SUCCESS;
}
