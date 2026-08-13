#include "shareme/rtc/macos_screen_capture_source.hpp"

#include <utility>

namespace shareme::rtc {

MacScreenCaptureEventGate::Generation MacScreenCaptureEventGate::begin() noexcept {
  std::lock_guard lock(mutex_);
  const auto generation = next_generation_++;
  active_generation_ = generation;
  return generation;
}

void MacScreenCaptureEventGate::end(Generation generation) noexcept {
  std::lock_guard lock(mutex_);
  if (active_generation_ == generation)
    active_generation_.reset();
}

bool MacScreenCaptureEventGate::accepts(Generation generation) const noexcept {
  std::lock_guard lock(mutex_);
  return active_generation_ == generation;
}

bool MacScreenCaptureFrameQueue::submit(ScreenFrame frame) {
  if (!frame.valid()) {
    std::lock_guard lock(mutex_);
    ++frames_dropped_;
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    if (closed_)
      return false;
    ++frames_captured_;
    if (pending_frame_.has_value())
      ++frames_dropped_;
    pending_frame_ = std::move(frame);
  }
  ready_.notify_one();
  return true;
}

std::optional<ScreenFrame> MacScreenCaptureFrameQueue::take() {
  std::lock_guard lock(mutex_);
  if (!pending_frame_.has_value())
    return std::nullopt;
  auto frame = std::move(pending_frame_);
  pending_frame_.reset();
  return frame;
}

std::optional<ScreenFrame> MacScreenCaptureFrameQueue::wait() {
  std::unique_lock lock(mutex_);
  ready_.wait(lock, [this] { return closed_ || pending_frame_.has_value(); });
  if (!pending_frame_.has_value())
    return std::nullopt;
  auto frame = std::move(pending_frame_);
  pending_frame_.reset();
  return frame;
}

void MacScreenCaptureFrameQueue::reset() {
  std::lock_guard lock(mutex_);
  pending_frame_.reset();
  frames_captured_ = 0;
  frames_dropped_ = 0;
  closed_ = false;
}

void MacScreenCaptureFrameQueue::close() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (pending_frame_.has_value()) {
      ++frames_dropped_;
      pending_frame_.reset();
    }
    closed_ = true;
  }
  ready_.notify_all();
}

std::uint64_t MacScreenCaptureFrameQueue::frames_captured() const noexcept {
  std::lock_guard lock(mutex_);
  return frames_captured_;
}

std::uint64_t MacScreenCaptureFrameQueue::frames_dropped() const noexcept {
  std::lock_guard lock(mutex_);
  return frames_dropped_;
}

std::uint64_t MacScreenCaptureFrameQueue::pending_frame_count() const noexcept {
  std::lock_guard lock(mutex_);
  return pending_frame_.has_value() ? 1 : 0;
}

MacScreenCaptureSource::MacScreenCaptureSource(
    MacScreenCaptureConfig config, std::unique_ptr<MacScreenCaptureStream> stream)
    : config_(config), stream_(std::move(stream)) {}

MacScreenCaptureSource::~MacScreenCaptureSource() { stop(); }

bool MacScreenCaptureSource::start(FrameCallback callback) {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return true;
  }

  if (stream_ == nullptr) {
    running_.store(false, std::memory_order_release);
    set_error("macos-screen-capture-stream-unavailable");
    return false;
  }

  set_error({});
  frame_queue_.reset();
  callback_ = std::move(callback);
  accepting_callbacks_.store(true, std::memory_order_release);
  worker_ = std::thread([this] { deliver_frames(); });

  const bool started = stream_->start([this](ScreenFrame frame) {
    receive_capture_frame(std::move(frame));
  });
  if (started)
    return true;

  accepting_callbacks_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  stream_->stop();
  frame_queue_.close();
  if (worker_.joinable())
    worker_.join();
  callback_ = {};
  auto stream_error = stream_->error();
  set_error(stream_error.empty() ? "macos-screen-capture-start-failed"
                                 : std::move(stream_error));
  return false;
}

void MacScreenCaptureSource::stop() noexcept {
  accepting_callbacks_.store(false, std::memory_order_release);
  if (!running_.exchange(false, std::memory_order_acq_rel))
    return;

  if (stream_ != nullptr)
    stream_->stop();
  frame_queue_.close();
  if (worker_.joinable())
    worker_.join();
  callback_ = {};
}

std::string MacScreenCaptureSource::error() const {
  {
    std::lock_guard lock(error_mutex_);
    if (!error_.empty())
      return error_;
  }
  return stream_ == nullptr ? std::string{} : stream_->error();
}

std::uint64_t MacScreenCaptureSource::frames_captured() const noexcept {
  return frame_queue_.frames_captured();
}

std::uint64_t MacScreenCaptureSource::frames_dropped() const noexcept {
  return frame_queue_.frames_dropped();
}

std::uint64_t MacScreenCaptureSource::pending_frame_count() const noexcept {
  return frame_queue_.pending_frame_count();
}

std::uint64_t MacScreenCaptureSource::dropped_frame_count() const noexcept {
  return frames_dropped();
}

bool MacScreenCaptureSource::inject_current_stream_stop_for_diagnostics() {
  return stream_ != nullptr &&
         stream_->inject_current_stream_stop_for_diagnostics();
}

bool MacScreenCaptureSource::inject_retired_stream_stop_for_diagnostics() {
  return stream_ != nullptr &&
         stream_->inject_retired_stream_stop_for_diagnostics();
}

bool MacScreenCaptureSource::native_stop_completed_for_diagnostics() const
    noexcept {
  return stream_ != nullptr &&
         stream_->native_stop_completed_for_diagnostics();
}

void MacScreenCaptureSource::clear_capture_fault_diagnostics() noexcept {
  if (stream_ != nullptr)
    stream_->clear_capture_fault_diagnostics();
}

void MacScreenCaptureSource::receive_capture_frame(ScreenFrame frame) {
  if (!accepting_callbacks_.load(std::memory_order_acquire))
    return;
  static_cast<void>(frame_queue_.submit(std::move(frame)));
}

void MacScreenCaptureSource::deliver_frames() {
  while (auto frame = frame_queue_.wait()) {
    if (!accepting_callbacks_.load(std::memory_order_acquire))
      continue;
    if (callback_ != nullptr)
      callback_(std::move(*frame));
  }
}

void MacScreenCaptureSource::set_error(std::string value) {
  std::lock_guard lock(error_mutex_);
  error_ = std::move(value);
}

} // namespace shareme::rtc
