#pragma once

#include "shareme/media/media_source.hpp"

#include <memory>

namespace shareme::media {

class FfmpegMediaSource final : public IMediaSource {
public:
  FfmpegMediaSource();
  ~FfmpegMediaSource() override;

  FfmpegMediaSource(const FfmpegMediaSource&) = delete;
  FfmpegMediaSource& operator=(const FfmpegMediaSource&) = delete;
  FfmpegMediaSource(FfmpegMediaSource&&) noexcept;
  FfmpegMediaSource& operator=(FfmpegMediaSource&&) noexcept;

  MediaInfo open(const std::filesystem::path& path) override;
  MediaEvent read_next(std::uint64_t generation) override;
  void seek(std::int64_t target_ms) override;
  void close() noexcept override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shareme::media
