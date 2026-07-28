#include "shareme/media/ffmpeg_media_source.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <variant>

namespace {

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

void decodes_generated_movie(const std::filesystem::path& movie_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source;
  const auto info = source.open(movie_path);

  REQUIRE(info.duration_ms >= 900);
  REQUIRE(info.duration_ms <= 1'100);
  REQUIRE(info.has_video);
  REQUIRE(info.has_audio);
  REQUIRE(info.video_width == 160);
  REQUIRE(info.video_height == 90);

  bool saw_video = false;
  bool saw_audio = false;
  std::optional<std::int64_t> last_video_pts;
  std::optional<std::int64_t> last_audio_pts;

  for (int event_count = 0; event_count < 2'000; ++event_count) {
    auto event = source.read_next(7);
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }

    if (const auto* video = std::get_if<VideoFrame>(&event)) {
      REQUIRE(video->width == 160);
      REQUIRE(video->height == 90);
      REQUIRE(video->stride >= video->width * 4);
      REQUIRE_FALSE(video->rgba.empty());
      REQUIRE(video->generation == 7);
      if (last_video_pts.has_value()) {
        REQUIRE(video->pts_ms >= *last_video_pts);
      }
      last_video_pts = video->pts_ms;
      saw_video = true;
    }

    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      REQUIRE(audio->sample_rate == 48'000);
      REQUIRE(audio->channels == 2);
      REQUIRE_FALSE(audio->interleaved_samples.empty());
      REQUIRE(audio->interleaved_samples.size() % 2 == 0);
      REQUIRE(audio->generation == 7);
      if (last_audio_pts.has_value()) {
        REQUIRE(audio->pts_ms >= *last_audio_pts);
      }
      last_audio_pts = audio->pts_ms;
      saw_audio = true;
    }
  }

  REQUIRE(saw_video);
  REQUIRE(saw_audio);
  source.close();
}

void seeks_to_requested_region(const std::filesystem::path& movie_path) {
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source;
  static_cast<void>(source.open(movie_path));
  source.seek(600);

  bool saw_video = false;
  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(8);
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* video = std::get_if<VideoFrame>(&event)) {
      REQUIRE(video->pts_ms >= 600);
      REQUIRE(video->pts_ms <= 700);
      REQUIRE(video->generation == 8);
      saw_video = true;
      break;
    }
  }

  REQUIRE(saw_video);
  source.close();
}

}  // namespace

int main(int argc, char** argv) {
  REQUIRE(argc == 2);
  decodes_generated_movie(argv[1]);
  seeks_to_requested_region(argv[1]);
  return EXIT_SUCCESS;
}
