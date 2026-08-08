#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"
#include "shareme/core/screen_stream_profile.hpp"
#include "shareme/rtc/local_video_source.hpp"
#include "shareme/rtc/screen_frame.hpp"

namespace shareme::rtc {

struct ScreenCaptureConfig {
  core::ScreenStreamProfile profile{core::ScreenStreamProfile::standard};
  std::optional<std::uint32_t> display_id;
  bool show_cursor{true};
};

class ScreenCaptureBackend {
public:
  using FrameCallback = std::function<void(ScreenFrame)>;

  virtual ~ScreenCaptureBackend() = default;

  [[nodiscard]] virtual bool start(FrameCallback callback) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::string error() const { return {}; }
  [[nodiscard]] virtual std::uint64_t
  pending_frame_count() const noexcept {
    return 0;
  }
  [[nodiscard]] virtual std::uint64_t
  dropped_frame_count() const noexcept {
    return 0;
  }
};

[[nodiscard]] std::unique_ptr<ScreenCaptureBackend>
create_platform_screen_capture_backend(const ScreenCaptureConfig &config);

class ScreenVideoSource final : private webrtc::RefCountedBase,
                                public LocalVideoSource {
public:
  static webrtc::scoped_refptr<ScreenVideoSource>
  create(ScreenCaptureConfig config = {});

  ScreenVideoSource(ScreenCaptureConfig config,
                    std::unique_ptr<ScreenCaptureBackend> backend);
  ~ScreenVideoSource() override;

  ScreenVideoSource(const ScreenVideoSource &) = delete;
  ScreenVideoSource &operator=(const ScreenVideoSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;
  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept override;
  [[nodiscard]] std::optional<std::int64_t>
  last_pts_ms() const noexcept override;
  [[nodiscard]] std::string error() const override;
  [[nodiscard]] std::uint64_t pending_frame_count() const noexcept;

  [[nodiscard]] bool is_screencast() const override;
  [[nodiscard]] std::optional<bool> needs_denoising() const override;
  [[nodiscard]] SourceState state() const override;
  [[nodiscard]] bool remote() const override;

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  void deliver_frame(ScreenFrame frame) noexcept;
  void set_error(std::string value);

  const ScreenCaptureConfig config_;
  std::unique_ptr<ScreenCaptureBackend> backend_;
  std::atomic_bool running_{false};
  std::atomic_bool accepting_callbacks_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic<std::uint64_t> dropped_count_{0};
  std::atomic<std::int64_t> last_capture_timestamp_us_{0};
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
