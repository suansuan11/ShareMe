#pragma once

#include <functional>
#include <mutex>
#include <utility>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "counting_video_sink.hpp"

namespace shareme::rtc {

class RemoteVideoSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  using Callback = std::function<void(const webrtc::VideoFrame &)>;

  explicit RemoteVideoSink(Callback callback = {})
      : callback_(std::move(callback)) {}

  void OnFrame(const webrtc::VideoFrame &frame) override {
    counting_sink_.OnFrame(frame);
    std::lock_guard lock(callback_mutex_);
    if (callback_)
      callback_(frame);
  }

  void clear_callback() noexcept {
    std::lock_guard lock(callback_mutex_);
    callback_ = {};
  }

  [[nodiscard]] std::uint64_t frame_count() const noexcept {
    return counting_sink_.frame_count();
  }

  [[nodiscard]] int last_width() const noexcept {
    return counting_sink_.last_width();
  }

  [[nodiscard]] int last_height() const noexcept {
    return counting_sink_.last_height();
  }

private:
  CountingVideoSink counting_sink_;
  std::mutex callback_mutex_;
  Callback callback_;
};

} // namespace shareme::rtc
