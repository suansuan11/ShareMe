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

} // namespace

int main(int argc, char **argv) {
  REQUIRE(argc == 3);
  const std::filesystem::path movie_path{argv[1]};
  decodes_and_paces_movie_frames(movie_path);
  missing_movie_is_typed_failure(movie_path.parent_path());
  video_less_movie_is_typed_failure(std::filesystem::path{argv[2]});
  return EXIT_SUCCESS;
}
