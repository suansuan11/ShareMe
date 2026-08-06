#pragma once

#include "shareme/core/movie_video_scheduler.hpp"
#include "video_preview_adapter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace shareme::tools {

struct MovieVideoPlayoutResult {
  VideoPreviewResult preview;
  shareme::core::VideoFrameDisposition disposition{
      shareme::core::VideoFrameDisposition::pass_through};
  std::uint64_t token{};
};

class MovieVideoPlayoutAdapter final {
public:
  explicit MovieVideoPlayoutAdapter(
      QObject *queue_target,
      shareme::core::MovieVideoPlayoutSchedulerConfig scheduler_config = {});
  ~MovieVideoPlayoutAdapter();

  MovieVideoPlayoutAdapter(const MovieVideoPlayoutAdapter &) = delete;
  MovieVideoPlayoutAdapter &operator=(const MovieVideoPlayoutAdapter &) = delete;

  void set_sink(QVideoSink *sink) noexcept;
  void set_submitted_callback(std::function<void(std::uint32_t)> callback);
  void close_ingress() noexcept;
  void shutdown() noexcept;
  [[nodiscard]] MovieVideoPlayoutResult submit(
      const webrtc::VideoFrame &frame,
      std::optional<shareme::core::VideoFrameTiming> timing = std::nullopt);
  [[nodiscard]] shareme::core::VideoSchedulerUpdate advance(
      shareme::core::VideoClockInput input);
  [[nodiscard]] shareme::core::VideoSchedulerSnapshot
  scheduler_snapshot() const noexcept;
  [[nodiscard]] VideoPreviewCounters counters() const noexcept;

private:
  void deliver_released(
      const shareme::core::VideoSchedulerUpdate &update);
  [[nodiscard]] VideoPreviewResult deliver_token(
      std::uint64_t token,
      shareme::core::VideoFrameDisposition disposition);

  VideoPreviewAdapter preview_;
  shareme::core::MovieVideoPlayoutScheduler scheduler_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<webrtc::VideoFrame>>
      pending_frames_;
  std::uint64_t next_token_{1};
  bool ingress_closed_{false};
  bool shutdown_complete_{false};
};

} // namespace shareme::tools
