#include "shareme/rtc/screen_frame.hpp"
#include "shareme/rtc/screen_video_source.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "api/make_ref_counted.h"
#include "api/video/i420_buffer.h"
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

class NativeBuffer : public webrtc::VideoFrameBuffer {
public:
  NativeBuffer(int width, int height) : width_(width), height_(height) {}

  Type type() const override { return Type::kNative; }
  int width() const override { return width_; }
  int height() const override { return height_; }

  static void reset_to_i420_calls() { to_i420_calls_.store(0); }
  static int to_i420_calls() { return to_i420_calls_.load(); }

private:
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
    to_i420_calls_.fetch_add(1);
    return nullptr;
  }

  const int width_;
  const int height_;
  static std::atomic<int> to_i420_calls_;
};

std::atomic<int> NativeBuffer::to_i420_calls_{0};

class RecordingSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  void OnFrame(const webrtc::VideoFrame &frame) override {
    ++frame_count;
    last_buffer = frame.video_frame_buffer();
    last_timestamp_us = frame.timestamp_us();
  }

  std::atomic<int> frame_count{0};
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> last_buffer;
  std::int64_t last_timestamp_us{0};
};

class FakeScreenCaptureBackend final :
    public shareme::rtc::ScreenCaptureBackend {
public:
  bool start(FrameCallback callback) override {
    callback_ = std::move(callback);
    started = true;
    return true;
  }

  void stop() noexcept override { stopped = true; }

  std::string error() const override { return error_value; }

  std::uint64_t pending_frame_count() const noexcept override {
    return pending_count;
  }

  std::uint64_t dropped_frame_count() const noexcept override {
    return dropped_count;
  }

  void emit(shareme::rtc::ScreenFrame frame) {
    if (callback_)
      callback_(std::move(frame));
  }

  bool started{false};
  bool stopped{false};
  std::string error_value;
  std::uint64_t pending_count{0};
  std::uint64_t dropped_count{0};

private:
  FrameCallback callback_;
};

webrtc::scoped_refptr<shareme::rtc::ScreenVideoSource> make_source(
    FakeScreenCaptureBackend **backend) {
  auto backend_owner = std::make_unique<FakeScreenCaptureBackend>();
  *backend = backend_owner.get();
  return webrtc::scoped_refptr<shareme::rtc::ScreenVideoSource>(
      new shareme::rtc::ScreenVideoSource({}, std::move(backend_owner)));
}

void forwards_native_frames_without_i420_conversion() {
  using shareme::rtc::ScreenFrame;
  using shareme::rtc::ScreenFrameBacking;

  NativeBuffer::reset_to_i420_calls();
  FakeScreenCaptureBackend *backend = nullptr;
  auto source = make_source(&backend);
  RecordingSink sink;
  auto *video_source =
      static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
          source.get());
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});

  REQUIRE(source->start());
  auto buffer = webrtc::make_ref_counted<NativeBuffer>(640, 360);
  backend->emit(ScreenFrame{buffer, 640, 360, 123'456, ScreenFrameBacking::native});
  source->stop();
  video_source->RemoveSink(&sink);

  REQUIRE(backend->started);
  REQUIRE(backend->stopped);
  REQUIRE(sink.frame_count == 1);
  REQUIRE(sink.last_buffer != nullptr);
  REQUIRE(sink.last_buffer->type() == webrtc::VideoFrameBuffer::Type::kNative);
  REQUIRE(sink.last_timestamp_us == 123'456);
  REQUIRE(NativeBuffer::to_i420_calls() == 0);
  REQUIRE(source->generated_count() == 1);
  REQUIRE(source->dropped_count() == 0);
}

void rejects_frames_after_stop_and_rejects_mismatched_dimensions() {
  using shareme::rtc::ScreenFrame;
  using shareme::rtc::ScreenFrameBacking;

  FakeScreenCaptureBackend *backend = nullptr;
  auto source = make_source(&backend);
  RecordingSink sink;
  auto *video_source =
      static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
          source.get());
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});
  REQUIRE(source->start());

  auto buffer = webrtc::make_ref_counted<NativeBuffer>(640, 360);
  backend->emit(ScreenFrame{buffer, 640, 359, 1, ScreenFrameBacking::native});
  REQUIRE(sink.frame_count == 0);
  REQUIRE(source->dropped_count() == 1);

  source->stop();
  backend->emit(ScreenFrame{buffer, 640, 360, 2, ScreenFrameBacking::native});
  video_source->RemoveSink(&sink);

  REQUIRE(sink.frame_count == 0);
  REQUIRE(source->generated_count() == 0);
  REQUIRE(source->dropped_count() == 1);
}

void forwards_i420_frames_as_the_software_fallback() {
  using shareme::rtc::ScreenFrame;
  using shareme::rtc::ScreenFrameBacking;

  FakeScreenCaptureBackend *backend = nullptr;
  auto source = make_source(&backend);
  RecordingSink sink;
  auto *video_source =
      static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
          source.get());
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});

  REQUIRE(source->start());
  auto buffer = webrtc::I420Buffer::Create(320, 180);
  backend->emit(ScreenFrame{buffer, 320, 180, 456'789, ScreenFrameBacking::i420});
  source->stop();
  video_source->RemoveSink(&sink);

  REQUIRE(sink.frame_count == 1);
  REQUIRE(sink.last_buffer->type() == webrtc::VideoFrameBuffer::Type::kI420);
  REQUIRE(sink.last_timestamp_us == 456'789);
  REQUIRE(source->generated_count() == 1);
  REQUIRE(source->dropped_count() == 0);
}

void reports_a_runtime_backend_error() {
  auto backend_owner = std::make_unique<FakeScreenCaptureBackend>();
  auto *backend = backend_owner.get();
  auto source = webrtc::scoped_refptr<shareme::rtc::ScreenVideoSource>(
      new shareme::rtc::ScreenVideoSource({}, std::move(backend_owner)));

  REQUIRE(source->start());
  backend->error_value = "screen-capture-stopped-42";
  REQUIRE(source->error() == "screen-capture-stopped-42");
  source->stop();
}

void honors_sink_adaptation_for_i420_frames() {
  using shareme::rtc::ScreenFrame;

  FakeScreenCaptureBackend *backend = nullptr;
  auto source = make_source(&backend);
  RecordingSink sink;
  auto *video_source =
      static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
          source.get());
  webrtc::VideoSinkWants wants;
  wants.max_pixel_count = 320 * 180;
  video_source->AddOrUpdateSink(&sink, wants);
  REQUIRE(source->start());

  auto buffer = webrtc::I420Buffer::Create(640, 360);
  backend->emit(ScreenFrame{buffer, 640, 360, 789'000,
                            shareme::rtc::ScreenFrameBacking::i420});
  source->stop();
  video_source->RemoveSink(&sink);

  REQUIRE(sink.frame_count == 1);
  REQUIRE(sink.last_buffer->width() == 320);
  REQUIRE(sink.last_buffer->height() == 180);
}

void reports_backend_queue_metrics() {
  auto backend_owner = std::make_unique<FakeScreenCaptureBackend>();
  auto *backend = backend_owner.get();
  auto source = webrtc::scoped_refptr<shareme::rtc::ScreenVideoSource>(
      new shareme::rtc::ScreenVideoSource({}, std::move(backend_owner)));
  backend->pending_count = 1;
  backend->dropped_count = 4;

  REQUIRE(source->pending_frame_count() == 1);
  REQUIRE(source->dropped_count() == 4);
}

void platform_factory_constructs_without_starting_capture() {
  auto source = shareme::rtc::ScreenVideoSource::create({
      .profile = shareme::core::ScreenStreamProfile::standard,
      .display_id = std::nullopt,
      .show_cursor = true});
  REQUIRE(source != nullptr);
  REQUIRE(source->state() ==
          webrtc::MediaSourceInterface::SourceState::kEnded);
  REQUIRE(source->generated_count() == 0);
  REQUIRE(source->dropped_count() == 0);
}

#if defined(_WIN32)
int windows_platform_factory_delivers_desktop_duplication_frames() {
  auto source = shareme::rtc::ScreenVideoSource::create({
      .profile = shareme::core::ScreenStreamProfile::standard,
      .display_id = std::nullopt,
      .show_cursor = true});
  RecordingSink sink;
  auto *video_source =
      static_cast<webrtc::VideoSourceInterface<webrtc::VideoFrame> *>(
          source.get());
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
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{2};
  while (sink.frame_count == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  source->stop();
  video_source->RemoveSink(&sink);

  REQUIRE(sink.frame_count > 0);
  REQUIRE(sink.last_buffer != nullptr);
  REQUIRE(sink.last_buffer->type() == webrtc::VideoFrameBuffer::Type::kI420);
  REQUIRE(sink.last_buffer->width() <= 1'920);
  REQUIRE(sink.last_buffer->height() <= 1'080);
  REQUIRE(source->generated_count() > 0);
  REQUIRE(source->error().empty());
  return EXIT_SUCCESS;
}
#endif

} // namespace

int main() {
  forwards_native_frames_without_i420_conversion();
  rejects_frames_after_stop_and_rejects_mismatched_dimensions();
  forwards_i420_frames_as_the_software_fallback();
  reports_a_runtime_backend_error();
  honors_sink_adaptation_for_i420_frames();
  reports_backend_queue_metrics();
  platform_factory_constructs_without_starting_capture();
#if defined(_WIN32)
  const auto windows_result =
      windows_platform_factory_delivers_desktop_duplication_frames();
  if (windows_result != EXIT_SUCCESS)
    return windows_result;
#endif
  return EXIT_SUCCESS;
}
