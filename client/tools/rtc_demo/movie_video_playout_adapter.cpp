#include "movie_video_playout_adapter.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace shareme::tools {

MovieVideoPlayoutAdapter::MovieVideoPlayoutAdapter(
    QObject *queue_target,
    shareme::core::MovieVideoPlayoutSchedulerConfig scheduler_config)
    : preview_(queue_target), scheduler_(scheduler_config) {}

MovieVideoPlayoutAdapter::~MovieVideoPlayoutAdapter() {
  shutdown();
}

void MovieVideoPlayoutAdapter::set_sink(QVideoSink *sink) noexcept {
  std::lock_guard lock(mutex_);
  preview_.set_sink(sink);
}

void MovieVideoPlayoutAdapter::set_submitted_callback(
    std::function<void(std::uint32_t)> callback) {
  std::lock_guard lock(mutex_);
  preview_.set_submitted_callback(std::move(callback));
}

void MovieVideoPlayoutAdapter::close_ingress() noexcept {
  std::lock_guard lock(mutex_);
  ingress_closed_ = true;
  pending_frames_.clear();
  preview_.close_ingress();
}

void MovieVideoPlayoutAdapter::reopen_ingress(QVideoSink *sink) noexcept {
  std::lock_guard lock(mutex_);
  if (shutdown_complete_ || sink == nullptr)
    return;
  pending_frames_.clear();
  preview_.reopen_ingress(sink);
  ingress_closed_ = false;
}

void MovieVideoPlayoutAdapter::shutdown() noexcept {
  std::lock_guard lock(mutex_);
  if (shutdown_complete_)
    return;
  ingress_closed_ = true;
  const auto update = scheduler_.shutdown();
  for (const auto &release : update.released)
    pending_frames_.erase(release.token);
  pending_frames_.clear();
  preview_.set_submitted_callback({});
  shutdown_complete_ = true;
}

MovieVideoPlayoutResult MovieVideoPlayoutAdapter::submit(
    const webrtc::VideoFrame &frame,
    std::optional<shareme::core::VideoFrameTiming> timing) {
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock())
    return {};
  if (ingress_closed_)
    return {.preview = {.path = PreviewPath::no_sink}};
  if (!timing) {
    return {.preview = preview_.submit(frame),
            .disposition =
                shareme::core::VideoFrameDisposition::pass_through};
  }

  if (timing->token == 0 || pending_frames_.contains(timing->token)) {
    if (next_token_ == std::numeric_limits<std::uint64_t>::max())
      next_token_ = 1;
    while (pending_frames_.contains(next_token_)) {
      if (next_token_ == std::numeric_limits<std::uint64_t>::max())
        next_token_ = 1;
      else
        ++next_token_;
    }
    timing->token = next_token_++;
  }
  const auto token = timing->token;
  pending_frames_[token] = std::make_shared<webrtc::VideoFrame>(frame);
  const auto update = scheduler_.submit(*timing);
  MovieVideoPlayoutResult result{.disposition = update.disposition,
                                 .token = token};
  const auto current_was_released = std::any_of(
      update.released.begin(), update.released.end(),
      [token](const auto &release) { return release.token == token; });
  deliver_released(update);
  if (update.disposition != shareme::core::VideoFrameDisposition::hold &&
      !current_was_released) {
    result.preview = deliver_token(token, update.disposition);
  }
  return result;
}

shareme::core::VideoSchedulerUpdate MovieVideoPlayoutAdapter::advance(
    shareme::core::VideoClockInput input) {
  std::lock_guard lock(mutex_);
  if (ingress_closed_)
    return {};
  const auto update = scheduler_.advance(std::move(input));
  deliver_released(update);
  return update;
}

shareme::core::VideoSchedulerSnapshot
MovieVideoPlayoutAdapter::scheduler_snapshot() const noexcept {
  std::lock_guard lock(mutex_);
  return scheduler_.snapshot();
}

VideoPreviewCounters MovieVideoPlayoutAdapter::counters() const noexcept {
  return preview_.counters();
}

void MovieVideoPlayoutAdapter::deliver_released(
    const shareme::core::VideoSchedulerUpdate &update) {
  for (const auto &release : update.released)
    static_cast<void>(deliver_token(release.token, release.disposition));
}

VideoPreviewResult MovieVideoPlayoutAdapter::deliver_token(
    std::uint64_t token,
    shareme::core::VideoFrameDisposition disposition) {
  const auto found = pending_frames_.find(token);
  if (found == pending_frames_.end())
    return {};
  VideoPreviewResult result;
  if (disposition == shareme::core::VideoFrameDisposition::pass_through ||
      disposition == shareme::core::VideoFrameDisposition::present)
    result = preview_.submit(*found->second);
  pending_frames_.erase(found);
  return result;
}

} // namespace shareme::tools
