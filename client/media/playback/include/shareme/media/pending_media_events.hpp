#pragma once

#include "shareme/media/media_source.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace shareme::media {

struct PendingMediaMetrics {
  std::size_t size{0};
  std::size_t video_size{0};
  std::size_t audio_size{0};
  std::size_t bytes{0};
  std::size_t peak_size{0};
  std::size_t peak_bytes{0};
  std::uint64_t backpressure_events{0};
};

class PendingMediaEvents final {
 public:
  static constexpr std::size_t video_capacity = 3;
  static constexpr std::size_t audio_capacity = 24;

  [[nodiscard]] bool can_push_video() const noexcept {
    std::scoped_lock lock{mutex_};
    return can_push_video_unlocked();
  }

  [[nodiscard]] bool can_push_audio() const noexcept {
    std::scoped_lock lock{mutex_};
    return can_push_audio_unlocked();
  }

  void note_backpressure() noexcept {
    std::scoped_lock lock{mutex_};
    ++backpressure_events_;
  }

  [[nodiscard]] bool push(MediaEvent&& event) {
    std::scoped_lock lock{mutex_};
    const auto* const video = std::get_if<VideoFrame>(&event);
    const auto* const audio = std::get_if<AudioFrame>(&event);
    if (video == nullptr && audio == nullptr) {
      return false;
    }
    if ((video != nullptr && !can_push_video_unlocked()) ||
        (audio != nullptr && !can_push_audio_unlocked())) {
      ++backpressure_events_;
      return false;
    }

    const auto event_bytes = bytes_for(event);
    if (video != nullptr) {
      ++video_size_;
    } else {
      ++audio_size_;
    }
    items_.push_back(std::move(event));
    bytes_ += event_bytes;
    if (items_.size() > peak_size_) {
      peak_size_ = items_.size();
    }
    if (bytes_ > peak_bytes_) {
      peak_bytes_ = bytes_;
    }
    return true;
  }

  [[nodiscard]] std::optional<MediaEvent> pop() {
    std::scoped_lock lock{mutex_};
    if (items_.empty()) {
      return std::nullopt;
    }

    const auto event_bytes = bytes_for(items_.front());
    if (std::holds_alternative<VideoFrame>(items_.front())) {
      --video_size_;
    } else {
      --audio_size_;
    }
    bytes_ -= event_bytes;
    auto event = std::move(items_.front());
    items_.pop_front();
    return event;
  }

  void clear() noexcept {
    std::scoped_lock lock{mutex_};
    items_.clear();
    video_size_ = 0;
    audio_size_ = 0;
    bytes_ = 0;
  }

  void reset() noexcept {
    std::scoped_lock lock{mutex_};
    items_.clear();
    video_size_ = 0;
    audio_size_ = 0;
    bytes_ = 0;
    peak_size_ = 0;
    peak_bytes_ = 0;
    backpressure_events_ = 0;
  }

  [[nodiscard]] bool empty() const noexcept {
    std::scoped_lock lock{mutex_};
    return items_.empty();
  }

  [[nodiscard]] PendingMediaMetrics metrics() const noexcept {
    std::scoped_lock lock{mutex_};
    return {
        .size = items_.size(),
        .video_size = video_size_,
        .audio_size = audio_size_,
        .bytes = bytes_,
        .peak_size = peak_size_,
        .peak_bytes = peak_bytes_,
        .backpressure_events = backpressure_events_,
    };
  }

 private:
  [[nodiscard]] bool can_push_video_unlocked() const noexcept {
    return video_size_ < video_capacity;
  }

  [[nodiscard]] bool can_push_audio_unlocked() const noexcept {
    return audio_size_ < audio_capacity;
  }

  [[nodiscard]] static std::size_t bytes_for(
      const MediaEvent& event) noexcept {
    if (const auto* const video = std::get_if<VideoFrame>(&event)) {
      return video_frame_capacity_bytes(*video);
    }
    if (const auto* const audio = std::get_if<AudioFrame>(&event)) {
      return audio_frame_capacity_bytes(*audio);
    }
    return 0;
  }

  mutable std::mutex mutex_;
  std::deque<MediaEvent> items_;
  std::size_t video_size_{0};
  std::size_t audio_size_{0};
  std::size_t bytes_{0};
  std::size_t peak_size_{0};
  std::size_t peak_bytes_{0};
  std::uint64_t backpressure_events_{0};
};

}  // namespace shareme::media
