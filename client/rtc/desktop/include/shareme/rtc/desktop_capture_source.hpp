#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"
#include "shareme/rtc/local_video_source.hpp"

namespace webrtc {
class I420Buffer;
}

namespace shareme::rtc {

struct DesktopCaptureConfig {
  int max_frames_per_second{60};
};

[[nodiscard]] constexpr bool
valid_desktop_capture_config(const DesktopCaptureConfig &config) noexcept {
  return config.max_frames_per_second > 0 &&
         config.max_frames_per_second <= 240;
}

class DesktopCaptureSource final : private webrtc::RefCountedBase,
                                   public LocalVideoSource {
public:
  static webrtc::scoped_refptr<DesktopCaptureSource>
  create(DesktopCaptureConfig config = {});

  explicit DesktopCaptureSource(DesktopCaptureConfig config);
  ~DesktopCaptureSource() override;

  DesktopCaptureSource(const DesktopCaptureSource &) = delete;
  DesktopCaptureSource &operator=(const DesktopCaptureSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;
  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept override;
  [[nodiscard]] std::string error() const override;
  [[nodiscard]] int last_width() const noexcept;
  [[nodiscard]] int last_height() const noexcept;

  [[nodiscard]] bool is_screencast() const override;
  [[nodiscard]] std::optional<bool> needs_denoising() const override;
  [[nodiscard]] SourceState state() const override;
  [[nodiscard]] bool remote() const override;

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  class Impl;
  void deliver_frame(webrtc::scoped_refptr<webrtc::I420Buffer> buffer,
                     int rotation_degrees);

  DesktopCaptureConfig config_;
  std::unique_ptr<Impl> impl_;
  std::atomic_bool running_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic<std::uint64_t> dropped_count_{0};
  std::atomic<int> last_width_{0};
  std::atomic<int> last_height_{0};
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
