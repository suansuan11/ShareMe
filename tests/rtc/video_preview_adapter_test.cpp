#include "video_preview_adapter.hpp"

#include <QGuiApplication>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cstdlib>
#include <iostream>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

webrtc::VideoFrame frame() {
  auto buffer = webrtc::I420Buffer::Create(4, 4);
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      buffer->MutableDataY()[row * buffer->StrideY() + column] =
          static_cast<std::uint8_t>(32 + row * 4 + column);
  return webrtc::VideoFrame::Builder()
      .set_video_frame_buffer(buffer)
      .set_timestamp_us(42)
      .set_rtp_timestamp(90)
      .build();
}

void submits_planar_frame_and_keeps_one_in_flight(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  adapter.set_sink(&sink);
  const auto first = adapter.submit(frame());
  const auto second = adapter.submit(frame());
  REQUIRE(first.submitted);
  REQUIRE(first.path == shareme::tools::PreviewPath::planar_yuv);
  REQUIRE(second.path == shareme::tools::PreviewPath::coalesced);
  app.processEvents();
  REQUIRE(adapter.counters().submissions == 1);
  REQUIRE(adapter.counters().coalesced == 1);
  REQUIRE(adapter.counters().max_pending_depth == 1);
  REQUIRE(sink.videoFrame().isValid());
  REQUIRE(sink.videoFrame().startTime() == 42);
  REQUIRE(sink.videoFrame().pixelFormat() ==
          QVideoFrameFormat::Format_YUV420P);
}

void rejects_without_sink(QGuiApplication& app) {
  Q_UNUSED(app);
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  const auto result = adapter.submit(frame());
  REQUIRE(!result.submitted);
  REQUIRE(result.path == shareme::tools::PreviewPath::no_sink);
}

void keeps_the_i420_buffer_alive_without_copying_planes(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  adapter.set_sink(&sink);
  auto source_buffer = webrtc::I420Buffer::Create(4, 4);
  source_buffer->MutableDataY()[0] = 32;
  const auto source = webrtc::VideoFrame::Builder()
                          .set_video_frame_buffer(source_buffer)
                          .set_timestamp_us(42)
                          .set_rtp_timestamp(90)
                          .build();
  const auto result = adapter.submit(source);
  REQUIRE(result.path == shareme::tools::PreviewPath::planar_yuv);
  source_buffer->MutableDataY()[0] = 220;
  app.processEvents();
  auto delivered = sink.videoFrame();
  REQUIRE(delivered.map(QVideoFrame::ReadOnly));
  REQUIRE(delivered.bits(0)[0] == 220);
  delivered.unmap();
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  submits_planar_frame_and_keeps_one_in_flight(app);
  rejects_without_sink(app);
  keeps_the_i420_buffer_alive_without_copying_planes(app);
  return EXIT_SUCCESS;
}
