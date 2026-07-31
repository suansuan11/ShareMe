#include "counting_video_sink.hpp"
#include "shareme/rtc/movie_video_source.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void decodes_and_paces_movie_frames(const std::filesystem::path &movie_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieVideoSource::create(movie_path);
  shareme::rtc::CountingVideoSink sink;
  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_source = source.get();
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});

  REQUIRE(source->start());
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (sink.frame_count() < 20 && source->error().empty() &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(10ms);
  source->stop();
  source->stop();
  video_source->RemoveSink(&sink);

  REQUIRE(source->error().empty());
  REQUIRE(sink.frame_count() >= 20);
  REQUIRE(sink.last_width() == 320);
  REQUIRE(sink.last_height() == 180);
  REQUIRE(sink.timestamps_increase());
  REQUIRE(sink.last_luma_min() < sink.last_luma_max());
  REQUIRE(source->generated_count() >= sink.frame_count());
  REQUIRE(source->last_pts_ms().has_value());
}

void missing_movie_is_typed_failure(const std::filesystem::path &directory) {
  auto source =
      shareme::rtc::MovieVideoSource::create(directory / "does-not-exist.mp4");
  REQUIRE(!source->start());
  REQUIRE(source->generated_count() == 0);
  REQUIRE(source->error() == "movie-open-failed");
  source->stop();
}

void video_less_movie_is_typed_failure(
    const std::filesystem::path &audio_only_path) {
  auto source = shareme::rtc::MovieVideoSource::create(audio_only_path);
  REQUIRE(!source->start());
  REQUIRE(source->generated_count() == 0);
  REQUIRE(source->error() == "movie-video-unavailable");
  source->stop();
}

void nonzero_pts_is_normalized(const std::filesystem::path &movie_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieVideoSource::create(movie_path);
  shareme::rtc::CountingVideoSink sink;
  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_source = source.get();
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});
  REQUIRE(source->start());

  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (sink.frame_count() < 20 && source->error().empty() &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(10ms);

  source->stop();
  video_source->RemoveSink(&sink);
  REQUIRE(source->error().empty());
  REQUIRE(sink.frame_count() >= 20);
  REQUIRE(sink.timestamps_increase());
  REQUIRE(source->last_pts_ms().has_value());
  REQUIRE(*source->last_pts_ms() >= 5'000);
}

void stop_interrupts_pts_gap(const std::filesystem::path &movie_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieVideoSource::create(movie_path);
  shareme::rtc::CountingVideoSink sink;
  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_source = source.get();
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});
  REQUIRE(source->start());

  const auto first_frame_deadline = std::chrono::steady_clock::now() + 1s;
  while (sink.frame_count() == 0 &&
         std::chrono::steady_clock::now() < first_frame_deadline)
    std::this_thread::sleep_for(10ms);
  REQUIRE(sink.frame_count() == 1);
  std::this_thread::sleep_for(50ms);

  const auto stop_started_at = std::chrono::steady_clock::now();
  source->stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started_at;
  video_source->RemoveSink(&sink);
  REQUIRE(stop_elapsed < 500ms);
}

} // namespace

int main(int argc, char **argv) {
  REQUIRE(argc == 6);
  const std::filesystem::path movie_path{argv[1]};
  decodes_and_paces_movie_frames(movie_path);
  missing_movie_is_typed_failure(movie_path.parent_path());
  video_less_movie_is_typed_failure(std::filesystem::path{argv[2]});
  nonzero_pts_is_normalized(std::filesystem::path{argv[3]});
  stop_interrupts_pts_gap(std::filesystem::path{argv[4]});
  nonzero_pts_is_normalized(std::filesystem::path{argv[5]});
  return EXIT_SUCCESS;
}
