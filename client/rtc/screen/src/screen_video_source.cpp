#include "shareme/rtc/screen_video_source.hpp"

#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"

namespace shareme::rtc {

ScreenVideoSource::ScreenVideoSource(
    ScreenCaptureConfig config, std::unique_ptr<ScreenCaptureBackend> backend)
    : config_(config), backend_(std::move(backend)) {}

ScreenVideoSource::~ScreenVideoSource() { stop(); }

bool ScreenVideoSource::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return true;
  }

  if (backend_ == nullptr) {
    running_.store(false, std::memory_order_release);
    set_error("screen-capture-backend-unavailable");
    return false;
  }

  set_error({});
  accepting_callbacks_.store(true, std::memory_order_release);
  const bool started = backend_->start([this](ScreenFrame frame) {
    deliver_frame(std::move(frame));
  });
  if (started)
    return true;

  accepting_callbacks_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  auto backend_error = backend_->error();
  set_error(backend_error.empty() ? "screen-capture-start-failed"
                                  : std::move(backend_error));
  return false;
}

void ScreenVideoSource::stop() noexcept {
  accepting_callbacks_.store(false, std::memory_order_release);
  if (!running_.exchange(false, std::memory_order_acq_rel))
    return;
  if (backend_ != nullptr)
    backend_->stop();
}

std::uint64_t ScreenVideoSource::generated_count() const noexcept {
  return generated_count_.load(std::memory_order_relaxed);
}

std::uint64_t ScreenVideoSource::dropped_count() const noexcept {
  const auto source_drops = dropped_count_.load(std::memory_order_relaxed);
  const auto backend_drops =
      backend_ == nullptr ? 0 : backend_->dropped_frame_count();
  return source_drops + backend_drops;
}

std::optional<std::int64_t> ScreenVideoSource::last_pts_ms() const noexcept {
  if (generated_count() == 0)
    return std::nullopt;
  return last_capture_timestamp_us_.load(std::memory_order_relaxed) / 1'000;
}

std::string ScreenVideoSource::error() const {
  {
    std::lock_guard lock(error_mutex_);
    if (!error_.empty())
      return error_;
  }
  return backend_ == nullptr ? std::string{} : backend_->error();
}

std::uint64_t ScreenVideoSource::pending_frame_count() const noexcept {
  return backend_ == nullptr ? 0 : backend_->pending_frame_count();
}

bool ScreenVideoSource::is_screencast() const { return true; }

std::optional<bool> ScreenVideoSource::needs_denoising() const { return false; }

ScreenVideoSource::SourceState ScreenVideoSource::state() const {
  return running_.load(std::memory_order_acquire) ? kLive : kEnded;
}

bool ScreenVideoSource::remote() const { return false; }

void ScreenVideoSource::deliver_frame(ScreenFrame frame) noexcept {
  if (!accepting_callbacks_.load(std::memory_order_acquire))
    return;

  if (!frame.valid()) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  const auto timestamp_us =
      static_cast<std::uint64_t>(frame.capture_timestamp_us);
  int output_width = 0;
  int output_height = 0;
  int crop_width = 0;
  int crop_height = 0;
  int crop_x = 0;
  int crop_y = 0;
  if (!AdaptFrame(frame.width, frame.height, frame.capture_timestamp_us,
                  &output_width, &output_height, &crop_width, &crop_height,
                  &crop_x, &crop_y)) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  auto output = frame.buffer;
  if (output_width != frame.width || output_height != frame.height ||
      crop_width != frame.width || crop_height != frame.height || crop_x != 0 ||
      crop_y != 0) {
    if (frame.backing == ScreenFrameBacking::native) {
      dropped_count_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const auto *i420 = frame.buffer->GetI420();
    if (i420 == nullptr) {
      dropped_count_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    auto scaled = webrtc::I420Buffer::Create(output_width, output_height);
    scaled->CropAndScaleFrom(*i420, crop_x, crop_y, crop_width, crop_height);
    output = std::move(scaled);
  }

  const auto rtp_timestamp = static_cast<std::uint32_t>(
      (timestamp_us / 1'000) * 90 + (timestamp_us % 1'000) * 90 / 1'000);
  const auto video_frame = webrtc::VideoFrame::Builder()
                               .set_video_frame_buffer(std::move(output))
                               .set_timestamp_us(frame.capture_timestamp_us)
                               .set_rtp_timestamp(rtp_timestamp)
                               .set_rotation(webrtc::kVideoRotation_0)
                               .build();
  OnFrame(video_frame);
  last_capture_timestamp_us_.store(frame.capture_timestamp_us,
                                   std::memory_order_relaxed);
  generated_count_.fetch_add(1, std::memory_order_relaxed);
}

void ScreenVideoSource::set_error(std::string value) {
  std::lock_guard lock(error_mutex_);
  error_ = std::move(value);
}

} // namespace shareme::rtc
