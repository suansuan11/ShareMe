#include "counting_video_sink.hpp"
#include "shareme/rtc/local_video_source.hpp"
#include "test_pattern_source.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "api/task_queue/default_task_queue_factory.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

void generates_real_i420_frames_without_queueing_them() {
  using namespace std::chrono_literals;
  using shareme::rtc::CountingVideoSink;
  using shareme::rtc::TestPatternSource;

  auto task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  auto source = TestPatternSource::create(*task_queue_factory, 640, 360, 30);
  webrtc::scoped_refptr<shareme::rtc::LocalVideoSource> local_source = source;
  CountingVideoSink sink;

  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_source =
      local_source.get();
  video_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants{});

  REQUIRE(local_source->start());
  REQUIRE(local_source->start());
  std::this_thread::sleep_for(250ms);
  local_source->stop();
  local_source->stop();

  video_source->RemoveSink(&sink);

  REQUIRE(sink.frame_count() >= 5);
  REQUIRE(sink.last_width() == 640);
  REQUIRE(sink.last_height() == 360);
  REQUIRE(sink.timestamps_increase());
  REQUIRE(source->generated_count() >= sink.frame_count());
  REQUIRE(source->pending_frame_count() == 0);
  REQUIRE(local_source->error().empty());
  REQUIRE(sink.last_luma_min() < sink.last_luma_max());
}

} // namespace

int main() {
  generates_real_i420_frames_without_queueing_them();
  return EXIT_SUCCESS;
}
