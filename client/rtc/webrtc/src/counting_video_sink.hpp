#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace shareme::rtc {

class CountingVideoSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  void OnFrame(const webrtc::VideoFrame &frame) override {
    const auto previous_count =
        frame_count_.fetch_add(1, std::memory_order_relaxed);
    const auto timestamp_us = frame.timestamp_us();
    const auto rtp_timestamp = frame.rtp_timestamp();

    if (previous_count > 0 &&
        (timestamp_us <= last_timestamp_us_.load(std::memory_order_relaxed) ||
         rtp_timestamp <=
             last_rtp_timestamp_.load(std::memory_order_relaxed))) {
      timestamps_increase_.store(false, std::memory_order_relaxed);
    }

    last_timestamp_us_.store(timestamp_us, std::memory_order_relaxed);
    last_rtp_timestamp_.store(rtp_timestamp, std::memory_order_relaxed);
    last_width_.store(frame.width(), std::memory_order_relaxed);
    last_height_.store(frame.height(), std::memory_order_relaxed);

    const auto buffer = frame.video_frame_buffer()->ToI420();
    std::uint8_t minimum = std::numeric_limits<std::uint8_t>::max();
    std::uint8_t maximum = std::numeric_limits<std::uint8_t>::min();
    const auto width = static_cast<std::size_t>(buffer->width());
    for (int y = 0; y < buffer->height(); ++y) {
      const auto *row =
          buffer->DataY() + static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(buffer->StrideY());
      const auto [row_minimum, row_maximum] =
          std::minmax_element(row, row + width);
      minimum = std::min(minimum, *row_minimum);
      maximum = std::max(maximum, *row_maximum);
    }
    last_luma_min_.store(minimum, std::memory_order_relaxed);
    last_luma_max_.store(maximum, std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t frame_count() const noexcept {
    return frame_count_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] int last_width() const noexcept {
    return last_width_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] int last_height() const noexcept {
    return last_height_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] bool timestamps_increase() const noexcept {
    return timestamps_increase_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint8_t last_luma_min() const noexcept {
    return last_luma_min_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint8_t last_luma_max() const noexcept {
    return last_luma_max_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint64_t> frame_count_{0};
  std::atomic<std::int64_t> last_timestamp_us_{0};
  std::atomic<std::uint32_t> last_rtp_timestamp_{0};
  std::atomic<int> last_width_{0};
  std::atomic<int> last_height_{0};
  std::atomic<bool> timestamps_increase_{true};
  std::atomic<std::uint8_t> last_luma_min_{0};
  std::atomic<std::uint8_t> last_luma_max_{0};
};

} // namespace shareme::rtc
