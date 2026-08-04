#include "video_preview_adapter.hpp"

#include <QImage>
#include <QMetaObject>
#include <QAbstractVideoBuffer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cstddef>
#include <new>
#include <utility>

#include "api/video/i420_buffer.h"
#include "libyuv/convert_argb.h"

namespace shareme::tools {
namespace {

struct PreparedFrame {
  QVideoFrame frame;
  PreviewPath path{PreviewPath::rejected};
};

class I420VideoBuffer final : public QAbstractVideoBuffer {
public:
  I420VideoBuffer(
      const webrtc::scoped_refptr<webrtc::I420BufferInterface>& source,
                  QVideoFrameFormat format)
      : source_(source), format_(std::move(format)),
        strides_{source_->StrideY(), source_->StrideU(), source_->StrideV()},
        heights_{source_->height(), (source_->height() + 1) / 2,
                 (source_->height() + 1) / 2} {}

  MapData map(QVideoFrame::MapMode) override {
    MapData data;
    data.planeCount = 3;
    const std::uint8_t* planes[] = {
        source_->DataY(), source_->DataU(), source_->DataV()};
    for (int plane = 0; plane < 3; ++plane) {
      data.bytesPerLine[plane] = strides_[plane];
      data.data[plane] = const_cast<uchar*>(planes[plane]);
      data.dataSize[plane] = strides_[plane] * heights_[plane];
    }
    return data;
  }

  QVideoFrameFormat format() const override { return format_; }

private:
  webrtc::scoped_refptr<webrtc::I420BufferInterface> source_;
  QVideoFrameFormat format_;
  int strides_[3];
  int heights_[3];
};

PreparedFrame make_planar_frame(
    const webrtc::scoped_refptr<webrtc::I420BufferInterface>& buffer,
    std::atomic<std::uint64_t>& mapping_failures,
    std::atomic<std::uint64_t>& fallback_copies) {
  const auto width = buffer->width();
  const auto height = buffer->height();
  try {
    const QVideoFrameFormat format{
        QSize(width, height), QVideoFrameFormat::Format_YUV420P};
    QVideoFrame planar{new I420VideoBuffer(buffer, format), format};
    return {std::move(planar), PreviewPath::planar_yuv};
  } catch (const std::bad_alloc&) {
    mapping_failures.fetch_add(1, std::memory_order_relaxed);
  }

  QImage image(width, height, QImage::Format_ARGB32);
  if (image.isNull() ||
      libyuv::I420ToARGB(
          buffer->DataY(), buffer->StrideY(), buffer->DataU(),
          buffer->StrideU(), buffer->DataV(), buffer->StrideV(), image.bits(),
          image.bytesPerLine(),
          width, height) != 0) {
    return {};
  }
  fallback_copies.fetch_add(1, std::memory_order_relaxed);
  return {QVideoFrame(image.copy()), PreviewPath::argb_fallback};
}

}  // namespace

struct VideoPreviewAdapter::State {
  explicit State(QObject* target) : queue_target(target) {}

  QPointer<QObject> queue_target;
  QPointer<QVideoSink> sink;
  std::function<void(std::uint32_t)> submitted_callback;
  std::atomic_bool pending{false};
  std::atomic<std::uint64_t> submissions{0};
  std::atomic<std::uint64_t> coalesced{0};
  std::atomic<std::uint64_t> fallback_copies{0};
  std::atomic<std::uint64_t> mapping_failures{0};
  std::atomic<std::uint64_t> max_pending_depth{0};
};

VideoPreviewAdapter::VideoPreviewAdapter(QObject* queue_target)
    : state_(std::make_shared<State>(queue_target)) {}

VideoPreviewAdapter::~VideoPreviewAdapter() {
  state_->submitted_callback = {};
  state_->queue_target = nullptr;
}

void VideoPreviewAdapter::set_sink(QVideoSink* sink) noexcept {
  state_->sink = sink;
}

void VideoPreviewAdapter::set_submitted_callback(
    std::function<void(std::uint32_t)> callback) {
  state_->submitted_callback = std::move(callback);
}

VideoPreviewResult VideoPreviewAdapter::submit(const webrtc::VideoFrame& source) {
  VideoPreviewResult result{.rtp_timestamp = source.rtp_timestamp()};
  if (state_->sink.isNull() || state_->queue_target.isNull()) {
    result.path = PreviewPath::no_sink;
    return result;
  }
  if (state_->pending.exchange(true, std::memory_order_acq_rel)) {
    state_->coalesced.fetch_add(1, std::memory_order_relaxed);
    result.path = PreviewPath::coalesced;
    return result;
  }
  state_->max_pending_depth.store(1, std::memory_order_relaxed);

  const auto source_buffer = source.video_frame_buffer();
  const auto i420 = source_buffer ? source_buffer->ToI420() : nullptr;
  if (!i420) {
    state_->pending.store(false, std::memory_order_release);
    return result;
  }
  auto prepared = make_planar_frame(
      i420, state_->mapping_failures, state_->fallback_copies);
  if (!prepared.frame.isValid()) {
    state_->pending.store(false, std::memory_order_release);
    return result;
  }
  result.submitted = true;
  result.path = prepared.path;
  const auto timestamp = result.rtp_timestamp;
  const auto start_time_us = source.timestamp_us();
  const auto state = state_;
  if (!QMetaObject::invokeMethod(
          state->queue_target,
          [state, queued_frame = std::move(prepared.frame), timestamp,
           start_time_us]() mutable {
            if (!state->sink.isNull()) {
              auto submitted = std::move(queued_frame);
              submitted.setStartTime(start_time_us);
              state->sink->setVideoFrame(std::move(submitted));
              state->submissions.fetch_add(1, std::memory_order_relaxed);
              if (state->submitted_callback)
                state->submitted_callback(timestamp);
            }
            state->pending.store(false, std::memory_order_release);
          },
          Qt::QueuedConnection)) {
    state->pending.store(false, std::memory_order_release);
    result.submitted = false;
    result.path = PreviewPath::rejected;
  }
  return result;
}

VideoPreviewCounters VideoPreviewAdapter::counters() const noexcept {
  return {
      .submissions = state_->submissions.load(std::memory_order_relaxed),
      .coalesced = state_->coalesced.load(std::memory_order_relaxed),
      .fallback_copies =
          state_->fallback_copies.load(std::memory_order_relaxed),
      .mapping_failures =
          state_->mapping_failures.load(std::memory_order_relaxed),
      .max_pending_depth =
          state_->max_pending_depth.load(std::memory_order_relaxed),
  };
}

}  // namespace shareme::tools
