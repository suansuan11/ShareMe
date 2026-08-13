#include "shareme/rtc/macos_screen_capture_source.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "api/video/i420_buffer.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void rejects_late_events_from_a_replaced_native_stream() {
  shareme::rtc::MacScreenCaptureEventGate gate;
  const auto first = gate.begin();
  REQUIRE(gate.accepts(first));

  gate.end(first);
  const auto second = gate.begin();
  REQUIRE(second != first);
  REQUIRE(!gate.accepts(first));
  REQUIRE(gate.accepts(second));

  gate.end(first);
  REQUIRE(gate.accepts(second));
  gate.end(second);
  REQUIRE(!gate.accepts(second));
}

shareme::rtc::ScreenFrame make_frame(std::int64_t timestamp_us) {
  return {.buffer = webrtc::I420Buffer::Create(320, 180),
          .width = 320,
          .height = 180,
          .capture_timestamp_us = timestamp_us,
          .backing = shareme::rtc::ScreenFrameBacking::i420};
}

class FakeScreenCaptureStream final
    : public shareme::rtc::MacScreenCaptureStream {
public:
  bool start(FrameCallback callback) override {
    callback_ = std::move(callback);
    started = true;
    return true;
  }

  void stop() noexcept override { stopped = true; }

  std::string error() const override { return error_value; }

  bool inject_current_stream_stop_for_diagnostics() override {
    ++current_fault_calls;
    return current_fault_available;
  }

  bool inject_retired_stream_stop_for_diagnostics() override {
    ++retired_fault_calls;
    return retired_fault_available;
  }

  void emit(shareme::rtc::ScreenFrame frame) {
    if (callback_)
      callback_(std::move(frame));
  }

  bool started{false};
  bool stopped{false};
  std::string error_value;
  bool current_fault_available{false};
  bool retired_fault_available{false};
  int current_fault_calls{0};
  int retired_fault_calls{0};

private:
  FrameCallback callback_;
};

void forwards_native_fault_diagnostics_to_the_stream() {
  auto stream_owner = std::make_unique<FakeScreenCaptureStream>();
  auto *stream = stream_owner.get();
  shareme::rtc::MacScreenCaptureSource source({}, std::move(stream_owner));

  REQUIRE(!source.inject_current_stream_stop_for_diagnostics());
  REQUIRE(stream->current_fault_calls == 1);
  stream->current_fault_available = true;
  REQUIRE(source.inject_current_stream_stop_for_diagnostics());

  REQUIRE(!source.inject_retired_stream_stop_for_diagnostics());
  REQUIRE(stream->retired_fault_calls == 1);
  stream->retired_fault_available = true;
  REQUIRE(source.inject_retired_stream_stop_for_diagnostics());
}

void replaces_a_pending_frame_and_keeps_one_slot() {
  shareme::rtc::MacScreenCaptureFrameQueue queue;
  REQUIRE(queue.submit(make_frame(1)));
  REQUIRE(queue.submit(make_frame(2)));
  REQUIRE(queue.pending_frame_count() == 1);
  REQUIRE(queue.frames_captured() == 2);
  REQUIRE(queue.frames_dropped() == 1);

  const auto frame = queue.take();
  REQUIRE(frame.has_value());
  REQUIRE(frame->capture_timestamp_us == 2);
  REQUIRE(queue.pending_frame_count() == 0);
}

void delivers_on_worker_thread_and_rejects_late_callbacks() {
  auto stream_owner = std::make_unique<FakeScreenCaptureStream>();
  auto *stream = stream_owner.get();
  shareme::rtc::MacScreenCaptureSource source(
      {}, std::move(stream_owner));

  std::mutex mutex;
  std::condition_variable delivered;
  int delivery_count = 0;
  std::thread::id capture_thread;
  std::thread::id delivery_thread;
  REQUIRE(source.start([&](shareme::rtc::ScreenFrame frame) {
    static_cast<void>(frame);
    {
      std::lock_guard lock(mutex);
      delivery_thread = std::this_thread::get_id();
      ++delivery_count;
    }
    delivered.notify_all();
  }));

  REQUIRE(stream->started);
  capture_thread = std::this_thread::get_id();
  stream->emit(make_frame(3));
  {
    std::unique_lock lock(mutex);
    REQUIRE(delivered.wait_for(lock, std::chrono::seconds(1),
                               [&] { return delivery_count == 1; }));
  }
  REQUIRE(delivery_thread != capture_thread);
  REQUIRE(source.frames_captured() == 1);
  REQUIRE(source.frames_dropped() == 0);

  source.stop();
  REQUIRE(stream->stopped);
  stream->emit(make_frame(4));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE(delivery_count == 1);
  REQUIRE(source.pending_frame_count() == 0);
}

void reports_a_runtime_stream_error() {
  auto stream_owner = std::make_unique<FakeScreenCaptureStream>();
  auto *stream = stream_owner.get();
  shareme::rtc::MacScreenCaptureSource source({}, std::move(stream_owner));

  REQUIRE(source.start([](shareme::rtc::ScreenFrame) {}));
  stream->error_value = "screen-capture-stopped-42";
  REQUIRE(source.error() == "screen-capture-stopped-42");
  source.stop();
}

} // namespace

int main() {
  forwards_native_fault_diagnostics_to_the_stream();
  rejects_late_events_from_a_replaced_native_stream();
  replaces_a_pending_frame_and_keeps_one_slot();
  delivers_on_worker_thread_and_rejects_late_callbacks();
  reports_a_runtime_stream_error();
  return EXIT_SUCCESS;
}
