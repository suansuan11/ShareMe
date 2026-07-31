#include "test_pattern_source.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "rtc_base/time_utils.h"

namespace shareme::rtc {
namespace {

constexpr std::array<std::uint8_t, 8> kLumaBars{
    32, 64, 96, 128, 160, 192, 224, 240,
};

void fill_test_pattern(webrtc::I420Buffer &buffer,
                       std::uint64_t frame_sequence) {
  const auto width = static_cast<std::size_t>(buffer.width());
  const auto bar_width = std::max<std::size_t>(1, width / kLumaBars.size());
  const auto offset =
      static_cast<std::size_t>(frame_sequence % kLumaBars.size());

  for (int y = 0; y < buffer.height(); ++y) {
    auto *row =
        buffer.MutableDataY() + static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(buffer.StrideY());
    for (std::size_t x = 0; x < width; ++x) {
      const auto bar = ((x / bar_width) + offset) % kLumaBars.size();
      row[x] = kLumaBars[bar];
    }
  }

  for (int y = 0; y < buffer.ChromaHeight(); ++y) {
    auto *u_row =
        buffer.MutableDataU() + static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(buffer.StrideU());
    auto *v_row =
        buffer.MutableDataV() + static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(buffer.StrideV());
    const auto chroma_width = static_cast<std::size_t>(buffer.ChromaWidth());
    std::fill_n(u_row, chroma_width, std::uint8_t{128});
    std::fill_n(v_row, chroma_width, std::uint8_t{128});
  }
}

} // namespace

webrtc::scoped_refptr<TestPatternSource>
TestPatternSource::create(webrtc::TaskQueueFactory &task_queue_factory,
                          int width, int height, int frames_per_second) {
  return webrtc::scoped_refptr<TestPatternSource>(new TestPatternSource(
      task_queue_factory, width, height, frames_per_second));
}

TestPatternSource::TestPatternSource(
    webrtc::TaskQueueFactory &task_queue_factory, int width, int height,
    int frames_per_second)
    : width_(width), height_(height), frames_per_second_(frames_per_second),
      frame_interval_(webrtc::TimeDelta::Micros(1'000'000 / frames_per_second)),
      task_queue_(task_queue_factory.CreateTaskQueue(
          "ShareMeTestPattern", webrtc::TaskQueueFactory::Priority::kVideo)) {}

TestPatternSource::~TestPatternSource() { stop(); }

bool TestPatternSource::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_relaxed)) {
    return true;
  }

  std::promise<void> ready;
  auto ready_future = ready.get_future();
  task_queue_->PostTask([this, ready = std::move(ready)]() mutable {
    repeating_task_ = webrtc::RepeatingTaskHandle::Start(
        task_queue_.get(),
        [this] {
          generate_frame();
          return frame_interval_;
        },
        webrtc::TaskQueueBase::DelayPrecision::kHigh);
    ready.set_value();
  });
  ready_future.wait();
  return true;
}

void TestPatternSource::stop() noexcept {
  if (!running_.exchange(false, std::memory_order_relaxed)) {
    return;
  }

  if (task_queue_->IsCurrent()) {
    stop_on_queue();
    return;
  }

  std::promise<void> stopped;
  auto stopped_future = stopped.get_future();
  task_queue_->PostTask([this, stopped = std::move(stopped)]() mutable {
    stop_on_queue();
    stopped.set_value();
  });
  stopped_future.wait();
}

std::uint64_t TestPatternSource::generated_count() const noexcept {
  return generated_count_.load(std::memory_order_relaxed);
}

std::uint64_t TestPatternSource::dropped_count() const noexcept {
  return dropped_count_.load(std::memory_order_relaxed);
}

bool TestPatternSource::is_screencast() const { return false; }

std::optional<bool> TestPatternSource::needs_denoising() const { return false; }

webrtc::MediaSourceInterface::SourceState TestPatternSource::state() const {
  return running_.load(std::memory_order_relaxed) ? kLive : kEnded;
}

bool TestPatternSource::remote() const { return false; }

void TestPatternSource::generate_frame() {
  const auto timestamp_us = webrtc::TimeMicros();
  int output_width = 0;
  int output_height = 0;
  int crop_width = 0;
  int crop_height = 0;
  int crop_x = 0;
  int crop_y = 0;
  if (!AdaptFrame(width_, height_, timestamp_us, &output_width, &output_height,
                  &crop_width, &crop_height, &crop_x, &crop_y)) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  auto buffer = webrtc::I420Buffer::Create(output_width, output_height);
  fill_test_pattern(*buffer, frame_sequence_);

  const auto rtp_timestamp = static_cast<std::uint32_t>(
      frame_sequence_ *
      static_cast<std::uint64_t>(90'000 / frames_per_second_));
  ++frame_sequence_;

  const auto frame = webrtc::VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_timestamp_us(timestamp_us)
                         .set_rtp_timestamp(rtp_timestamp)
                         .set_rotation(webrtc::kVideoRotation_0)
                         .build();
  OnFrame(frame);
  generated_count_.fetch_add(1, std::memory_order_relaxed);
}

void TestPatternSource::stop_on_queue() noexcept {
  if (repeating_task_.Running()) {
    repeating_task_.Stop();
  }
}

} // namespace shareme::rtc
