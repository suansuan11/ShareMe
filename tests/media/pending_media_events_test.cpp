#include "shareme/media/pending_media_events.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)
#define REQUIRE_FALSE(expression) require(!(expression), "!(" #expression ")", __LINE__)

shareme::media::MediaEvent video_event(std::int64_t pts_ms) {
  shareme::media::VideoFrame frame;
  frame.pixel_format = shareme::media::VideoPixelFormat::i420;
  frame.width = 2;
  frame.height = 2;
  frame.stride_y = 2;
  frame.stride_u = 1;
  frame.stride_v = 1;
  frame.i420_y.resize(4);
  frame.i420_u.resize(1);
  frame.i420_v.resize(1);
  frame.pts_ms = pts_ms;
  return frame;
}

shareme::media::MediaEvent audio_event(std::int64_t pts_ms) {
  shareme::media::AudioFrame frame;
  frame.sample_rate = 48'000;
  frame.channels = 2;
  frame.interleaved_samples.resize(4);
  frame.pts_ms = pts_ms;
  return frame;
}

void rejects_video_without_consuming_when_full() {
  shareme::media::PendingMediaEvents events;
  for (int value = 0; value < 3; ++value) {
    REQUIRE(events.push(video_event(value)));
  }

  auto fourth = video_event(3);
  REQUIRE_FALSE(events.push(std::move(fourth)));
  REQUIRE(std::get<shareme::media::VideoFrame>(fourth).pts_ms == 3);
  REQUIRE(events.metrics().video_size == 3);
  REQUIRE(events.metrics().backpressure_events == 1);
}

void preserves_fifo_and_tracks_bytes() {
  shareme::media::PendingMediaEvents events;
  REQUIRE(events.push(video_event(10)));
  REQUIRE(events.push(audio_event(20)));

  const auto metrics = events.metrics();
  REQUIRE(metrics.size == 2);
  REQUIRE(metrics.video_size == 1);
  REQUIRE(metrics.audio_size == 1);
  REQUIRE(metrics.bytes == 14);
  REQUIRE(metrics.peak_bytes == 14);

  const auto first = events.pop();
  REQUIRE(first.has_value());
  REQUIRE(std::get<shareme::media::VideoFrame>(*first).pts_ms == 10);
  const auto second = events.pop();
  REQUIRE(second.has_value());
  REQUIRE(std::get<shareme::media::AudioFrame>(*second).pts_ms == 20);
  REQUIRE(events.empty());
  REQUIRE(events.metrics().bytes == 0);
  REQUIRE(events.metrics().peak_bytes == 14);
}

void clears_current_state_without_resetting_peak() {
  shareme::media::PendingMediaEvents events;
  REQUIRE(events.push(video_event(1)));
  REQUIRE(events.push(audio_event(2)));
  events.clear();

  const auto metrics = events.metrics();
  REQUIRE(metrics.size == 0);
  REQUIRE(metrics.video_size == 0);
  REQUIRE(metrics.audio_size == 0);
  REQUIRE(metrics.bytes == 0);
  REQUIRE(metrics.peak_size == 2);
  REQUIRE(metrics.peak_bytes == 14);
  REQUIRE(metrics.backpressure_events == 0);
}

}  // namespace

int main() {
  rejects_video_without_consuming_when_full();
  preserves_fifo_and_tracks_bytes();
  clears_current_state_without_resetting_peak();
  return EXIT_SUCCESS;
}
