#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "shareme/core/screen_stream_profile.hpp"
#include "shareme/rtc/screen_video_source.hpp"

namespace shareme::rtc {

struct MacScreenCaptureConfig {
  core::ScreenStreamProfile profile{core::ScreenStreamProfile::standard};
  std::optional<std::uint32_t> display_id;
  bool show_cursor{true};
};

class MacScreenCaptureEventGate final {
public:
  using Generation = std::uint64_t;

  [[nodiscard]] Generation begin() noexcept;
  void end(Generation generation) noexcept;
  [[nodiscard]] bool accepts(Generation generation) const noexcept;

private:
  mutable std::mutex mutex_;
  Generation next_generation_{1};
  std::optional<Generation> active_generation_;
};

class MacScreenCaptureStream {
public:
  using FrameCallback = std::function<void(ScreenFrame)>;

  virtual ~MacScreenCaptureStream() = default;

  [[nodiscard]] virtual bool start(FrameCallback callback) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::string error() const { return {}; }
};

[[nodiscard]] std::unique_ptr<MacScreenCaptureStream>
create_screen_capture_kit_stream(const MacScreenCaptureConfig &config);

class MacScreenCaptureFrameQueue final {
public:
  [[nodiscard]] bool submit(ScreenFrame frame);
  [[nodiscard]] std::optional<ScreenFrame> take();
  [[nodiscard]] std::optional<ScreenFrame> wait();
  void reset();
  void close() noexcept;

  [[nodiscard]] std::uint64_t frames_captured() const noexcept;
  [[nodiscard]] std::uint64_t frames_dropped() const noexcept;
  [[nodiscard]] std::uint64_t pending_frame_count() const noexcept;

private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::optional<ScreenFrame> pending_frame_;
  std::uint64_t frames_captured_{0};
  std::uint64_t frames_dropped_{0};
  bool closed_{false};
};

class MacScreenCaptureSource final : public ScreenCaptureBackend {
public:
  MacScreenCaptureSource(MacScreenCaptureConfig config,
                         std::unique_ptr<MacScreenCaptureStream> stream);
  ~MacScreenCaptureSource() override;

  MacScreenCaptureSource(const MacScreenCaptureSource &) = delete;
  MacScreenCaptureSource &operator=(const MacScreenCaptureSource &) = delete;

  [[nodiscard]] bool start(FrameCallback callback) override;
  void stop() noexcept override;
  [[nodiscard]] std::string error() const override;

  [[nodiscard]] std::uint64_t frames_captured() const noexcept;
  [[nodiscard]] std::uint64_t frames_dropped() const noexcept;
  [[nodiscard]] std::uint64_t pending_frame_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_frame_count() const noexcept override;

private:
  void receive_capture_frame(ScreenFrame frame);
  void deliver_frames();
  void set_error(std::string value);

  const MacScreenCaptureConfig config_;
  std::unique_ptr<MacScreenCaptureStream> stream_;
  MacScreenCaptureFrameQueue frame_queue_;
  FrameCallback callback_;
  std::thread worker_;
  std::atomic_bool running_{false};
  std::atomic_bool accepting_callbacks_{false};
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
