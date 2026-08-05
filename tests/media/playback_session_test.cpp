#include "shareme/media/playback_session.hpp"

#include "shareme/media/ffmpeg_media_source.hpp"
#include "shareme/media/pcm_chunker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

class BlockingMetricsSource final : public shareme::media::IMediaSource {
public:
  shareme::media::MediaInfo open(const std::filesystem::path&) override {
    return {.duration_ms = 1'000, .has_video = true};
  }

  shareme::media::MediaEvent read_next(std::uint64_t) override {
    read_started.store(true, std::memory_order_release);
    std::unique_lock lock{mutex_};
    released_.wait(lock, [this] { return release; });
    return shareme::media::EndOfStream{};
  }

  void seek(std::int64_t) override {}

  void close() noexcept override { release_read(); }

  shareme::media::MediaSourceMetrics metrics() const noexcept override {
    return {.decoded_video_frames = 7};
  }

  void release_read() {
    {
      std::lock_guard lock{mutex_};
      release = true;
    }
    released_.notify_all();
  }

  std::atomic_bool read_started{false};

private:
  mutable std::mutex mutex_;
  std::condition_variable released_;
  bool release{false};
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

void limits_decode_ahead_of_playhead() {
  using shareme::media::PlaybackSession;
  using shareme::media::PlaybackState;

  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));

  observed_source->push_video(
      500, FakeMediaSource::use_requested_generation);
  session.play();
  REQUIRE(wait_until([&session] { return session.pop_video().has_value(); }));
  REQUIRE(session.state() == PlaybackState::playing);

  session.set_playhead_ms(500);
  REQUIRE(wait_until(
      [&session] { return session.state() == PlaybackState::ended; }));
}

void reports_bounded_video_bytes() {
  using shareme::media::AudioFrame;
  using shareme::media::PlaybackSession;
  using shareme::media::VideoFrame;

  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));

  VideoFrame video;
  video.rgba.resize(4);
  REQUIRE(shareme::media::video_frame_capacity_bytes(video) == 4);

  AudioFrame audio;
  audio.interleaved_samples.resize(4);
  REQUIRE(shareme::media::audio_frame_capacity_bytes(audio) ==
          4 * sizeof(std::int16_t));

  observed_source->push_video(1, FakeMediaSource::use_requested_generation);
  observed_source->push_video(2, FakeMediaSource::use_requested_generation);
  session.play();
  REQUIRE(wait_until([&session] {
    return session.metrics().video_queue_size > 0;
  }));

  const auto metrics = session.metrics();
  REQUIRE(metrics.video_queue_capacity == 3);
  REQUIRE(metrics.video_queue_size <= metrics.video_queue_capacity);
  REQUIRE(metrics.video_queue_bytes > 0);
  REQUIRE(metrics.video_queue_peak_bytes >= metrics.video_queue_bytes);
  REQUIRE(metrics.video_dropped_count == session.video_dropped_count());
}

void metrics_do_not_wait_for_source_decode() {
  using shareme::media::PlaybackSession;

  auto source = std::make_unique<BlockingMetricsSource>();
  auto* observed_source = source.get();
  PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));
  session.play();
  REQUIRE(wait_until([observed_source] {
    return observed_source->read_started.load(std::memory_order_acquire);
  }));

  auto metrics = std::async(std::launch::async, [&session] {
    return session.metrics();
  });
  REQUIRE(metrics.wait_for(200ms) == std::future_status::ready);
  REQUIRE(metrics.get().source.decoded_video_frames == 7);
  observed_source->release_read();
  REQUIRE(wait_until(
      [&session] { return session.state() == shareme::media::PlaybackState::ended; }));
}

void negative_start_throttles_without_dropping_audio(
    const std::filesystem::path& negative_start_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::PcmChunker;
  using shareme::media::PlaybackSession;
  using shareme::media::PlaybackState;

  PlaybackSession session{
      std::make_unique<FfmpegMediaSource>(
          FfmpegMediaSourceOptions{
              .decode_video = false,
              .decode_audio = true,
          })};
  const auto info = session.open(negative_start_path);
  REQUIRE(info.start_time_ms >= -1'100);
  REQUIRE(info.start_time_ms <= -900);
  REQUIRE(info.has_audio);

  PcmChunker chunker;
  std::vector<std::int64_t> chunk_pts_ms;
  const auto started_at = std::chrono::steady_clock::now();
  session.play();
  std::this_thread::sleep_for(50ms);

  const auto deadline = started_at + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
    session.set_playhead_ms(info.start_time_ms + elapsed.count());

    while (auto audio = session.pop_audio()) {
      REQUIRE(chunker.push(std::move(*audio)));
      while (auto chunk = chunker.pop())
        chunk_pts_ms.push_back(chunk->pts_ms);
    }

    if (session.state() == PlaybackState::ended)
      break;
    std::this_thread::sleep_for(1ms);
  }

  while (auto audio = session.pop_audio()) {
    REQUIRE(chunker.push(std::move(*audio)));
    while (auto chunk = chunker.pop())
      chunk_pts_ms.push_back(chunk->pts_ms);
  }

  REQUIRE(session.state() == PlaybackState::ended);
  REQUIRE(session.audio_dropped_count() == 0);
  REQUIRE(chunk_pts_ms.size() >= 290);
  for (std::size_t index = 1; index < chunk_pts_ms.size(); ++index) {
    const auto step_ms = chunk_pts_ms[index] - chunk_pts_ms[index - 1U];
    REQUIRE(step_ms >= 9);
    REQUIRE(step_ms <= 11);
    const auto ideal_pts_ms =
        chunk_pts_ms.front() + static_cast<std::int64_t>(index * 10U);
    REQUIRE(std::llabs(chunk_pts_ms[index] - ideal_pts_ms) <= 1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  REQUIRE(argc == 2);
  opens_paused_and_controls_idempotently();
  seek_increments_generation_and_discards_stale_frames();
  drops_oldest_video_when_queue_is_full();
  limits_decode_ahead_of_playhead();
  reports_bounded_video_bytes();
  metrics_do_not_wait_for_source_decode();
  negative_start_throttles_without_dropping_audio(argv[1]);
  return EXIT_SUCCESS;
}
