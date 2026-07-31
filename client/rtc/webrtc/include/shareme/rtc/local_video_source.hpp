#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "api/video/adapted_video_track_source.h"

namespace shareme::rtc {

class LocalVideoSource : public webrtc::AdaptedVideoTrackSource {
public:
  [[nodiscard]] virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::uint64_t generated_count() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t dropped_count() const noexcept = 0;
  [[nodiscard]] virtual std::optional<std::int64_t>
  last_pts_ms() const noexcept {
    return std::nullopt;
  }
  [[nodiscard]] virtual std::string error() const = 0;

protected:
  ~LocalVideoSource() override = default;
};

} // namespace shareme::rtc
