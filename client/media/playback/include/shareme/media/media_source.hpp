#pragma once

#include "shareme/media/media_frame.hpp"

#include <cstdint>
#include <filesystem>
#include <variant>

namespace shareme::media {

struct MediaInfo {
  std::int64_t duration_ms{0};
  bool has_video{false};
  bool has_audio{false};
  int video_width{0};
  int video_height{0};
};

struct EndOfStream {};

using MediaEvent = std::variant<VideoFrame, AudioFrame, EndOfStream>;

class IMediaSource {
public:
  virtual ~IMediaSource() = default;

  virtual MediaInfo open(const std::filesystem::path& path) = 0;
  virtual MediaEvent read_next(std::uint64_t generation) = 0;
  virtual void seek(std::int64_t target_ms) = 0;
  virtual void close() noexcept = 0;
};

}  // namespace shareme::media
