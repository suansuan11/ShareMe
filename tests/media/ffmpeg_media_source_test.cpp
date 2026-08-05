#include "shareme/media/ffmpeg_media_source.hpp"
#include "shareme/media/pcm_chunker.hpp"
#include "shareme/media/video_path.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
  using shareme::media::VideoPixelFormat;

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
      REQUIRE(video->pixel_format == VideoPixelFormat::i420);
      REQUIRE(video->stride_y >= video->width);
      REQUIRE(video->stride_u >= (video->width + 1) / 2);
      REQUIRE(video->stride_v >= (video->width + 1) / 2);
      REQUIRE_FALSE(video->i420_y.empty());
      REQUIRE_FALSE(video->i420_u.empty());
      REQUIRE_FALSE(video->i420_v.empty());
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

void defaults_to_software_decoder_path(
    const std::filesystem::path& movie_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoDecoderPath;
  using shareme::media::VideoAccelerationMode;

  FfmpegMediaSource source;
  const auto info = source.open(movie_path);
  REQUIRE(info.video_path.requested == VideoAccelerationMode::software);
  REQUIRE(info.video_path.decoder == VideoDecoderPath::software);
  REQUIRE(info.video_acceleration == "software");

  FfmpegMediaSource explicit_source{FfmpegMediaSourceOptions{
      .decode_video = true,
      .decode_audio = false,
      .video_acceleration = VideoAccelerationMode::software}};
  const auto explicit_info = explicit_source.open(movie_path);
  REQUIRE(explicit_info.video_path.requested == VideoAccelerationMode::software);
  REQUIRE(explicit_info.video_path.decoder == VideoDecoderPath::software);
  REQUIRE(explicit_info.video_acceleration == "software");
}

void explicit_auto_reports_decoder_path_without_encoder_claim(
    const std::filesystem::path& movie_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoAccelerationMode;
  using shareme::media::VideoDecoderPath;

  FfmpegMediaSource source{FfmpegMediaSourceOptions{
      .decode_video = true,
      .decode_audio = false,
      .video_acceleration = VideoAccelerationMode::auto_mode}};
  const auto info = source.open(movie_path);
  REQUIRE(info.video_path.requested == VideoAccelerationMode::auto_mode);
  REQUIRE(info.video_path.decoder == VideoDecoderPath::fallback);
  REQUIRE(info.video_acceleration == "software");
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

void pending_metrics_stay_bounded_and_tail_is_present(
    const std::filesystem::path& movie_path) {
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{FfmpegMediaSourceOptions{
      .decode_video = true,
      .decode_audio = false,
  }};
  static_cast<void>(source.open(movie_path));

  std::size_t video_count = 0;
  while (true) {
    const auto metrics = source.metrics();
    REQUIRE(metrics.pending_events <= 3);
    REQUIRE(metrics.pending_bytes <= metrics.peak_pending_bytes);
    const auto event = source.read_next(23);
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    REQUIRE(std::holds_alternative<VideoFrame>(event));
    ++video_count;
  }

  REQUIRE(video_count == 30);
  const auto final_metrics = source.metrics();
  REQUIRE(final_metrics.decoded_video_frames == video_count);
  REQUIRE(final_metrics.peak_pending_events > 0);
  REQUIRE(final_metrics.peak_pending_bytes > 0);
  REQUIRE(final_metrics.pending_events == 0);
}

void seek_clears_pending_metrics(const std::filesystem::path& movie_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{FfmpegMediaSourceOptions{
      .decode_video = true,
      .decode_audio = false,
  }};
  static_cast<void>(source.open(movie_path));
  static_cast<void>(source.read_next(24));
  source.seek(500);
  REQUIRE(source.metrics().pending_events == 0);
  REQUIRE(source.metrics().pending_bytes == 0);

  for (int attempt = 0; attempt < 20; ++attempt) {
    const auto event = source.read_next(24);
    if (const auto* const video = std::get_if<VideoFrame>(&event)) {
      REQUIRE(video->pts_ms >= 500);
      REQUIRE(video->generation == 24);
      return;
    }
  }
  REQUIRE(false);
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

std::int64_t first_audio_pts(
    shareme::media::FfmpegMediaSource& source,
    std::uint64_t generation) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::VideoFrame;

  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(generation);
    REQUIRE_FALSE(std::holds_alternative<VideoFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      REQUIRE(audio->sample_rate == 48'000);
      REQUIRE(audio->channels == 2);
      REQUIRE(audio->generation == generation);
      return audio->pts_ms;
    }
  }

  REQUIRE(false);
  return 0;
}

void decodes_real_audio_only_media(
    const std::filesystem::path& audio_only_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  const auto info = source.open(audio_only_path);

  REQUIRE_FALSE(info.has_video);
  REQUIRE(info.has_audio);
  REQUIRE(info.start_time_ms <= first_audio_pts(source, 12));
}

void rejects_audio_only_media_for_video_decode(
    const std::filesystem::path& audio_only_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::VideoStreamUnavailable;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = true, .decode_audio = false}};
  try {
    static_cast<void>(source.open(audio_only_path));
    REQUIRE(false);
  } catch (const VideoStreamUnavailable& error) {
    REQUIRE(
        std::string{error.what()} ==
        "Media file has no decodable video stream");
  }
}

void reports_nonzero_container_start_time(
    const std::filesystem::path& nonzero_audio_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  const auto info = source.open(nonzero_audio_path);

  REQUIRE(info.start_time_ms >= 2'900);
  REQUIRE(info.start_time_ms <= 3'050);
  REQUIRE(info.start_time_ms <= first_audio_pts(source, 13));
}

void defaults_unknown_container_start_time_to_zero(
    const std::filesystem::path& raw_aac_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  const auto info = source.open(raw_aac_path);

  REQUIRE(info.start_time_ms == 0);
  static_cast<void>(first_audio_pts(source, 14));
}

void drains_all_resampled_audio_at_end_of_stream(
    const std::filesystem::path& audio_44100_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;
  using shareme::media::PcmChunker;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  const auto info = source.open(audio_44100_path);
  REQUIRE_FALSE(info.has_video);
  REQUIRE(info.has_audio);

  PcmChunker chunker;
  std::size_t samples_per_channel = 0;
  std::size_t chunk_count = 0;
  struct AudioPosition {
    std::int64_t pts_ms;
    std::size_t samples_per_channel;
  };
  std::optional<AudioPosition> previous_audio_position;
  std::optional<AudioPosition> last_audio_position;
  for (int event_count = 0; event_count < 2'000; ++event_count) {
    auto event = source.read_next(15);
    REQUIRE_FALSE(std::holds_alternative<VideoFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      const auto frame_samples_per_channel =
          audio->interleaved_samples.size() / 2U;
      previous_audio_position = last_audio_position;
      last_audio_position =
          AudioPosition{audio->pts_ms, frame_samples_per_channel};
      samples_per_channel += frame_samples_per_channel;
      for (std::size_t offset = 0; offset < frame_samples_per_channel;) {
        const auto slice_samples =
            std::min<std::size_t>(480U, frame_samples_per_channel - offset);
        AudioFrame slice;
        slice.sample_rate = audio->sample_rate;
        slice.channels = audio->channels;
        slice.pts_ms =
            audio->pts_ms + static_cast<std::int64_t>(offset / 48U);
        slice.generation = audio->generation;
        slice.interleaved_samples.assign(
            audio->interleaved_samples.begin() +
                static_cast<std::ptrdiff_t>(offset * 2U),
            audio->interleaved_samples.begin() +
                static_cast<std::ptrdiff_t>((offset + slice_samples) * 2U));
        REQUIRE(chunker.push(std::move(slice)));
        while (chunker.pop().has_value()) {
          ++chunk_count;
        }
        offset += slice_samples;
      }
    }
  }

  REQUIRE(samples_per_channel == 48'000U);
  REQUIRE(chunk_count == 100U);
  REQUIRE_FALSE(chunker.pop().has_value());
  REQUIRE(previous_audio_position.has_value());
  REQUIRE(last_audio_position.has_value());
  REQUIRE(last_audio_position->samples_per_channel < 480U);
  const auto expected_tail_pts_ms =
      previous_audio_position->pts_ms +
      static_cast<std::int64_t>(
          (previous_audio_position->samples_per_channel * 1'000U + 24'000U) /
          48'000U);
  REQUIRE(last_audio_position->pts_ms == expected_tail_pts_ms);
}

std::size_t count_audio_samples_to_end(
    shareme::media::FfmpegMediaSource& source,
    std::uint64_t generation) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::VideoFrame;

  std::size_t samples_per_channel = 0;
  for (int event_count = 0; event_count < 2'000; ++event_count) {
    auto event = source.read_next(generation);
    REQUIRE_FALSE(std::holds_alternative<VideoFrame>(event));
    if (std::holds_alternative<EndOfStream>(event)) {
      return samples_per_channel;
    }
    if (const auto* audio = std::get_if<AudioFrame>(&event)) {
      REQUIRE(audio->generation == generation);
      samples_per_channel += audio->interleaved_samples.size() / 2U;
    }
  }

  REQUIRE(false);
  return 0;
}

void resets_resampler_after_partial_decode_seek(
    const std::filesystem::path& audio_44100_path) {
  using shareme::media::AudioFrame;
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  static_cast<void>(source.open(audio_44100_path));
  while (true) {
    auto event = source.read_next(17);
    REQUIRE_FALSE(std::holds_alternative<EndOfStream>(event));
    if (std::holds_alternative<AudioFrame>(event)) {
      break;
    }
  }

  source.seek(0);
  REQUIRE(count_audio_samples_to_end(source, 18) == 48'000U);
}

void resets_resampler_after_end_of_stream_seek(
    const std::filesystem::path& audio_44100_path) {
  using shareme::media::FfmpegMediaSource;
  using shareme::media::FfmpegMediaSourceOptions;

  FfmpegMediaSource source{
      FfmpegMediaSourceOptions{.decode_video = false, .decode_audio = true}};
  static_cast<void>(source.open(audio_44100_path));
  REQUIRE(count_audio_samples_to_end(source, 19) == 48'000U);

  source.seek(0);
  REQUIRE(count_audio_samples_to_end(source, 20) == 48'000U);
}

void default_options_decode_video_only_media(
    const std::filesystem::path& video_only_path) {
  using shareme::media::EndOfStream;
  using shareme::media::FfmpegMediaSource;
  using shareme::media::VideoFrame;

  FfmpegMediaSource source;
  const auto info = source.open(video_only_path);
  REQUIRE(info.has_video);
  REQUIRE_FALSE(info.has_audio);

  bool saw_video = false;
  for (int event_count = 0; event_count < 1'000; ++event_count) {
    auto event = source.read_next(16);
    if (std::holds_alternative<EndOfStream>(event)) {
      break;
    }
    if (const auto* video = std::get_if<VideoFrame>(&event)) {
      REQUIRE(video->generation == 16);
      saw_video = true;
      break;
    }
  }
  REQUIRE(saw_video);
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
  REQUIRE(argc == 7);
  decodes_generated_movie(argv[1]);
  defaults_to_software_decoder_path(argv[1]);
  explicit_auto_reports_decoder_path_without_encoder_claim(argv[1]);
  seeks_to_requested_region(argv[1]);
  decodes_audio_without_video(argv[1]);
  decodes_video_without_audio(argv[1]);
  pending_metrics_stay_bounded_and_tail_is_present(argv[2]);
  seek_clears_pending_metrics(argv[2]);
  seeks_audio_without_video(argv[1]);
  rejects_video_only_media_for_audio_decode(argv[2]);
  decodes_real_audio_only_media(argv[3]);
  rejects_audio_only_media_for_video_decode(argv[3]);
  reports_nonzero_container_start_time(argv[4]);
  defaults_unknown_container_start_time_to_zero(argv[5]);
  drains_all_resampled_audio_at_end_of_stream(argv[6]);
  resets_resampler_after_partial_decode_seek(argv[6]);
  resets_resampler_after_end_of_stream_seek(argv[6]);
  default_options_decode_video_only_media(argv[2]);
  rejects_when_all_decoders_are_disabled();
  return EXIT_SUCCESS;
}
