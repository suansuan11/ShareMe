#pragma once

#include "shareme/media/media_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <variant>

namespace shareme::media {

struct MediaInfo {
  std::int64_t duration_ms{0};
  std::int64_t start_time_ms{0};
  bool has_video{false};
  bool has_audio{false};
  int video_width{0};
  int video_height{0};
  int video_frame_rate_num{0};
  int video_frame_rate_den{0};
  int video_pixel_aspect_num{0};
  int video_pixel_aspect_den{0};
  std::string video_codec;
  std::string video_profile;
  std::string video_color_range;
  std::string video_color_space;
  std::string video_acceleration{"software"};
};

struct MediaSourceMetrics {
  std::uint64_t decoded_video_frames{0};
  std::uint64_t decoded_audio_frames{0};
  std::size_t pending_events{0};
  std::size_t pending_bytes{0};
  std::size_t peak_pending_events{0};
  std::size_t peak_pending_bytes{0};
  std::uint64_t backpressure_events{0};
};

struct EndOfStream {};

class VideoStreamUnavailable final : public std::runtime_error {
public:
  VideoStreamUnavailable()
      : std::runtime_error{"Media file has no decodable video stream"} {}
};

class AudioStreamUnavailable final : public std::runtime_error {
public:
  AudioStreamUnavailable()
      : std::runtime_error{"Media file has no decodable audio stream"} {}
};

using MediaEvent = std::variant<VideoFrame, AudioFrame, EndOfStream>;

class IMediaSource {
public:
  virtual ~IMediaSource() = default;

  virtual MediaInfo open(const std::filesystem::path& path) = 0;
  virtual MediaEvent read_next(std::uint64_t generation) = 0;
  virtual void seek(std::int64_t target_ms) = 0;
  virtual void close() noexcept = 0;
  // Metrics must be safe to read while read_next() is decoding.
  [[nodiscard]] virtual MediaSourceMetrics metrics() const noexcept {
    return {};
  }
};

}  // namespace shareme::media
