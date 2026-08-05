#pragma once

#include "shareme/media/media_source.hpp"

#include <memory>

namespace shareme::media {

enum class VideoAccelerationMode { auto_mode, software };

struct FfmpegMediaSourceOptions {
  bool decode_video{true};
  bool decode_audio{true};
  VideoAccelerationMode video_acceleration{VideoAccelerationMode::auto_mode};
};

class FfmpegMediaSource final : public IMediaSource {
public:
  explicit FfmpegMediaSource(FfmpegMediaSourceOptions options = {});
  ~FfmpegMediaSource() override;

  FfmpegMediaSource(const FfmpegMediaSource&) = delete;
  FfmpegMediaSource& operator=(const FfmpegMediaSource&) = delete;
  FfmpegMediaSource(FfmpegMediaSource&&) noexcept;
  FfmpegMediaSource& operator=(FfmpegMediaSource&&) noexcept;

  MediaInfo open(const std::filesystem::path& path) override;
  MediaEvent read_next(std::uint64_t generation) override;
  void seek(std::int64_t target_ms) override;
  void close() noexcept override;
  [[nodiscard]] MediaSourceMetrics metrics() const noexcept override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shareme::media
