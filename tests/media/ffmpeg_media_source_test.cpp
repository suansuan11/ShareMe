#include "shareme/media/ffmpeg_media_source.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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

void decodes_audio_without_video(const std::filesystem::path& movie_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  const auto info = source.open(movie_path);

  REQUIRE_FALSE(info.has_video);
  REQUIRE(info.has_audio);

  bool saw_audio = false;
  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(9);
    REQUIRE_FALSE(std::holds_alternative<VideoFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      REQUIRE(info.start_time_ms <= audio->pts_ms);
      REQUIRE(audio->generation == 9);
      saw_audio = true;
      break;
    }
  }

  REQUIRE(saw_audio);
}

void decodes_video_without_audio(const std::filesystem::path& movie_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = true, .decode_audio = false}};
  const auto info = source.open(movie_path);

  REQUIRE(info.has_video);
  REQUIRE_FALSE(info.has_audio);

  bool saw_video = false;
  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(10);
    REQUIRE_FALSE(std::holds_alternative<AudioFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* video = std::get_if<VideoFrame>(&event)) {
      REQUIRE(video->generation == 10);
      saw_video = true;
      break;
    }
  }

  REQUIRE(saw_video);
}

void seeks_audio_without_video(const std::filesystem::path& movie_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  static_cast<void>(source.open(movie_path));
  source.seek(600);

  bool saw_audio = false;
  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(11);
    REQUIRE_FALSE(std::holds_alternative<VideoFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      REQUIRE(audio->pts_ms >= 600);
      REQUIRE(audio->generation == 11);
      saw_audio = true;
      break;
    }
  }

  REQUIRE(saw_audio);
}

void rejects_video_only_media_for_audio_decode(
    const std::filesystem::path& video_only_path) {
  using shareme::media::AudioStreamUnavailable;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  try {
    static_cast<void>(source.open(video_only_path));
    REQUIRE(false);
  } catch (const AudioStreamUnavailable& error) {
    REQUIRE(
        std::string{error.what()} ==
        "Media file has no decodable audio stream");
  }
}

void rejects_when_all_decoders_are_disabled() {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  try {
    FfmpegMediaSource source{
        FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = false}};
    REQUIRE(false);
  } catch (const std::invalid_argument&) {
  }
}

}  // namespace

int main(int argc, char** argv) {
  REQUIRE(argc == 3);
  decodes_generated_movie(argv[1]);
  seeks_to_requested_region(argv[1]);
  decodes_audio_without_video(argv[1]);
  decodes_video_without_audio(argv[1]);
  seeks_audio_without_video(argv[1]);
  rejects_video_only_media_for_audio_decode(argv[2]);
  rejects_when_all_decoders_are_disabled();
  return EXIT_SUCCESS;
}
