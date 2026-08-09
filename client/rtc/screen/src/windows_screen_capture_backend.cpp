#include "windows_screen_capture_backend.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "shareme/core/screen_stream_profile.hpp"
#include "shareme/rtc/desktop_capture_source.hpp"

namespace shareme::rtc {
namespace {

class WindowsScreenCaptureBackend final
    : public ScreenCaptureBackend,
      private webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  explicit WindowsScreenCaptureBackend(ScreenCaptureConfig config)
      : config_(std::move(config)) {}

  ~WindowsScreenCaptureBackend() override { stop(); }

  bool start(FrameCallback callback) override {
    if (!callback)
      return false;

    {
      std::lock_guard lock(mutex_);
      if (source_ != nullptr)
        return true;
    }

    const auto bounds = core::screen_stream_profile_bounds(config_.profile);
    auto source = DesktopCaptureSource::create(
        {.max_frames_per_second = bounds.max_frames_per_second});
    auto *video_source =
        static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
            source.get());
    video_source->AddOrUpdateSink(this, webrtc::VideoSinkWants{});

    {
      std::lock_guard lock(mutex_);
      callback_ = std::move(callback);
      source_ = source;
      accepting_frames_ = true;
      error_.clear();
    }

    if (source->start())
      return true;

    video_source->RemoveSink(this);
    const auto source_error = source->error();
    {
      std::lock_guard lock(mutex_);
      accepting_frames_ = false;
      callback_ = {};
      source_ = nullptr;
      error_ = source_error.empty() ? "screen-desktop-start-failed"
                                    : source_error;
    }
    return false;
  }

  void stop() noexcept override {
    webrtc::scoped_refptr<DesktopCaptureSource> source;
    {
      std::lock_guard lock(mutex_);
      accepting_frames_ = false;
      source = source_;
    }
    if (source == nullptr)
      return;

    source->stop();
    auto *video_source =
        static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
            source.get());
    video_source->RemoveSink(this);

    std::lock_guard lock(mutex_);
    callback_ = {};
    source_ = nullptr;
  }

  [[nodiscard]] std::string error() const override {
    webrtc::scoped_refptr<DesktopCaptureSource> source;
    std::string error;
    {
      std::lock_guard lock(mutex_);
      source = source_;
      error = error_;
    }
    if (!error.empty() || source == nullptr)
      return error;
    return source->error();
  }

  [[nodiscard]] std::uint64_t dropped_frame_count() const noexcept override {
    webrtc::scoped_refptr<DesktopCaptureSource> source;
    {
      std::lock_guard lock(mutex_);
      source = source_;
    }
    const auto adapter_drops = dropped_frames_.load(std::memory_order_relaxed);
    return adapter_drops + (source == nullptr ? 0 : source->dropped_count());
  }

private:
  void OnFrame(const webrtc::VideoFrame &frame) override {
    if (frame.video_frame_buffer() == nullptr ||
        frame.video_frame_buffer()->type() !=
            webrtc::VideoFrameBuffer::Type::kI420) {
      dropped_frames_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    FrameCallback callback;
    {
      std::lock_guard lock(mutex_);
      if (!accepting_frames_)
        return;
      callback = callback_;
    }
    if (callback) {
      auto output = frame.video_frame_buffer();
      auto dimensions = core::fit_screen_dimensions(
          frame.width(), frame.height(), config_.profile);
      dimensions.width &= ~1;
      dimensions.height &= ~1;
      if (dimensions.width <= 0 || dimensions.height <= 0) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (dimensions.width != frame.width() ||
          dimensions.height != frame.height()) {
        const auto *i420 = frame.video_frame_buffer()->GetI420();
        if (i420 == nullptr) {
          dropped_frames_.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        auto scaled =
            webrtc::I420Buffer::Create(dimensions.width, dimensions.height);
        scaled->CropAndScaleFrom(*i420);
        output = std::move(scaled);
      }
      callback({.buffer = std::move(output),
                .width = dimensions.width,
                .height = dimensions.height,
                .capture_timestamp_us = frame.timestamp_us(),
                .backing = ScreenFrameBacking::i420});
    }
  }

  const ScreenCaptureConfig config_;
  mutable std::mutex mutex_;
  FrameCallback callback_;
  webrtc::scoped_refptr<DesktopCaptureSource> source_;
  std::string error_;
  std::atomic<std::uint64_t> dropped_frames_{0};
  bool accepting_frames_{false};
};

} // namespace

std::unique_ptr<ScreenCaptureBackend>
create_windows_screen_capture_backend(const ScreenCaptureConfig &config) {
  return std::make_unique<WindowsScreenCaptureBackend>(config);
}

} // namespace shareme::rtc
