#pragma once

#include <cstdint>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

namespace shareme::rtc {

enum class ScreenFrameBacking {
  native,
  i420,
};

struct ScreenFrame {
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer;
  int width{0};
  int height{0};
  std::int64_t capture_timestamp_us{0};
  ScreenFrameBacking backing{ScreenFrameBacking::i420};

  [[nodiscard]] bool valid() const noexcept {
    if (buffer == nullptr || width <= 0 || height <= 0 ||
        capture_timestamp_us < 0 || buffer->width() != width ||
        buffer->height() != height) {
      return false;
    }

    if (backing == ScreenFrameBacking::native) {
      return buffer->type() == webrtc::VideoFrameBuffer::Type::kNative;
    }
    return buffer->type() == webrtc::VideoFrameBuffer::Type::kI420;
  }
};

} // namespace shareme::rtc
