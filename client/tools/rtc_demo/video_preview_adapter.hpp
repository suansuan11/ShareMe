#pragma once

#include <QPointer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "api/video/video_frame.h"

class QObject;
class QVideoSink;

namespace shareme::tools {

struct VideoPreviewState;

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
  std::uint64_t remote_callbacks{0};
  std::uint64_t sink_submissions{0};
  std::uint64_t presentation_coalesced{0};
  std::uint64_t presentation_callback_delay_p95{0};
  std::uint64_t presentation_callback_delay_max{0};
  std::uint64_t fallback_copies{0};
  std::uint64_t mapping_failures{0};
  std::uint64_t max_pending_depth{0};
  std::uint64_t pending_callbacks{0};
  std::uint64_t pending_callback_bytes{0};
  std::uint64_t peak_pending_callback_bytes{0};
  std::uint64_t presentation_epoch{0};
  std::uint64_t presentation_recovery_count{0};
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
  void close_ingress() noexcept;
  void reopen_ingress(QVideoSink* sink) noexcept;
  [[nodiscard]] VideoPreviewResult submit(const webrtc::VideoFrame& frame);
  [[nodiscard]] VideoPreviewCounters counters() const noexcept;

private:
  std::shared_ptr<VideoPreviewState> state_;
};

}  // namespace shareme::tools
