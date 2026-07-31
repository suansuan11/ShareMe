#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "shareme/rtc/local_video_source.hpp"

namespace shareme::rtc {

class TestPatternSource final : private webrtc::RefCountedBase,
                                public LocalVideoSource {
public:
  static webrtc::scoped_refptr<TestPatternSource>
  create(webrtc::TaskQueueFactory &task_queue_factory, int width, int height,
         int frames_per_second);

  TestPatternSource(webrtc::TaskQueueFactory &task_queue_factory, int width,
                    int height, int frames_per_second);
  ~TestPatternSource() override;

  TestPatternSource(const TestPatternSource &) = delete;
  TestPatternSource &operator=(const TestPatternSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;

  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept override;
  [[nodiscard]] std::string error() const override { return {}; }
  [[nodiscard]] constexpr std::uint64_t pending_frame_count() const noexcept {
    return 0;
  }

  [[nodiscard]] bool is_screencast() const override;
  [[nodiscard]] std::optional<bool> needs_denoising() const override;
  [[nodiscard]] SourceState state() const override;
  [[nodiscard]] bool remote() const override;

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }

  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  void generate_frame();
  void stop_on_queue() noexcept;

  const int width_;
  const int height_;
  const int frames_per_second_;
  const webrtc::TimeDelta frame_interval_;
  std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> task_queue_;
  webrtc::RepeatingTaskHandle repeating_task_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic<std::uint64_t> dropped_count_{0};
  std::uint64_t frame_sequence_{0};
};

} // namespace shareme::rtc
