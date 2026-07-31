#include "counting_video_sink.hpp"
#include "shareme/rtc/desktop_capture_source.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <windows.h>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

class VisibleTestWindow final {
public:
  VisibleTestWindow() {
    window_ = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"ShareMe capture",
                              WS_POPUP | WS_VISIBLE | WS_BORDER, 20, 20, 160,
                              90, nullptr, nullptr, GetModuleHandleW(nullptr),
                              nullptr);
    if (window_ != nullptr) {
      ShowWindow(window_, SW_SHOW);
      UpdateWindow(window_);
    }
  }

  ~VisibleTestWindow() {
    if (window_ != nullptr)
      DestroyWindow(window_);
  }

  void move(int x) const {
    if (window_ != nullptr) {
      SetWindowPos(window_, HWND_TOPMOST, x, 20, 160, 90, SWP_SHOWWINDOW);
      UpdateWindow(window_);
    }
  }

private:
  HWND window_{nullptr};
};

void validates_capture_configuration() {
  using shareme::rtc::DesktopCaptureConfig;
  using shareme::rtc::valid_desktop_capture_config;
  REQUIRE(valid_desktop_capture_config(DesktopCaptureConfig{}));
  REQUIRE(!valid_desktop_capture_config(
      DesktopCaptureConfig{.max_frames_per_second = 0}));
  REQUIRE(!valid_desktop_capture_config(
      DesktopCaptureConfig{.max_frames_per_second = 241}));
}

int captures_real_primary_display_frames() {
  using namespace std::chrono_literals;
  VisibleTestWindow window;
  auto source = shareme::rtc::DesktopCaptureSource::create();
  REQUIRE(source != nullptr);
  shareme::rtc::CountingVideoSink sink;
  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_source = source.get();
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});

  if (!source->start()) {
    const auto category = source->error();
    video_source->RemoveSink(&sink);
    if (category == "desktop-output-unavailable" ||
        category == "desktop-duplication-unavailable") {
      std::cerr << "SKIP: " << category << '\n';
      return 77;
    }
    REQUIRE(false);
  }

  const auto animation_deadline = std::chrono::steady_clock::now() + 1s;
  int animation_frame = 0;
  while (std::chrono::steady_clock::now() < animation_deadline &&
         source->error().empty()) {
    window.move(animation_frame++ % 2 == 0 ? 20 : 220);
    std::this_thread::sleep_for(16ms);
  }
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (sink.frame_count() < 2 && source->error().empty() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }

  const auto stop_started = std::chrono::steady_clock::now();
  source->stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  video_source->RemoveSink(&sink);

  if (!source->error().empty())
    std::cerr << "Capture error: " << source->error() << '\n';
  REQUIRE(source->error().empty());
  REQUIRE(sink.frame_count() >= 30);
  REQUIRE(sink.timestamps_increase());
  REQUIRE(source->last_width() > 0);
  REQUIRE(source->last_height() > 0);
  REQUIRE(source->generated_count() >= sink.frame_count());
  REQUIRE(source->is_screencast());
  REQUIRE(source->needs_denoising() == std::optional<bool>{false});
  REQUIRE(source->state() == webrtc::MediaSourceInterface::kEnded);
  REQUIRE(stop_elapsed < 500ms);
  return EXIT_SUCCESS;
}

} // namespace

int main() {
  validates_capture_configuration();
  return captures_real_primary_display_frames();
}
