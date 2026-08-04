#pragma once

#include <QPointer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "api/video/video_frame.h"

class QObject;
class QVideoSink;

namespace shareme::tools {

enum class PreviewPath {
  planar_yuv,
  argb_fallback,
  coalesced,
  no_sink,
  rejected,
};

struct VideoPreviewResult {
  bool submitted{false};
  std::uint32_t rtp_timestamp{0};
  PreviewPath path{PreviewPath::rejected};
};

struct VideoPreviewCounters {
  std::uint64_t submissions{0};
  std::uint64_t coalesced{0};
  std::uint64_t fallback_copies{0};
  std::uint64_t mapping_failures{0};
  std::uint64_t max_pending_depth{0};
};

class VideoPreviewAdapter final {
public:
  explicit VideoPreviewAdapter(QObject* queue_target);
  ~VideoPreviewAdapter();

  VideoPreviewAdapter(const VideoPreviewAdapter&) = delete;
  VideoPreviewAdapter& operator=(const VideoPreviewAdapter&) = delete;

  void set_sink(QVideoSink* sink) noexcept;
  void set_submitted_callback(
      std::function<void(std::uint32_t)> callback);
  [[nodiscard]] VideoPreviewResult submit(const webrtc::VideoFrame& frame);
  [[nodiscard]] VideoPreviewCounters counters() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
};

}  // namespace shareme::tools
