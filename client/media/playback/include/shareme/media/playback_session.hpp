#pragma once

#include "shareme/media/media_source.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace shareme::media {

enum class PlaybackState {
  closed,
  paused,
  playing,
  ended,
  failed,
};

struct PlaybackSessionMetrics {
  MediaSourceMetrics source;
  std::size_t video_queue_size{0};
  std::size_t video_queue_capacity{0};
  std::size_t video_queue_bytes{0};
  std::size_t video_queue_peak_bytes{0};
  std::uint64_t video_dropped_count{0};
  std::size_t audio_queue_size{0};
  std::size_t audio_queue_capacity{0};
  std::size_t audio_queue_bytes{0};
  std::size_t audio_queue_peak_bytes{0};
  std::uint64_t audio_dropped_count{0};
};

class PlaybackSession {
public:
  explicit PlaybackSession(std::unique_ptr<IMediaSource> source);
  ~PlaybackSession();

  PlaybackSession(const PlaybackSession&) = delete;
  PlaybackSession& operator=(const PlaybackSession&) = delete;
  PlaybackSession(PlaybackSession&&) = delete;
  PlaybackSession& operator=(PlaybackSession&&) = delete;

  MediaInfo open(const std::filesystem::path& path);
  void play();
  void pause();
  void seek(std::int64_t target_ms);
  void set_playhead_ms(std::int64_t playhead_ms);
  void close() noexcept;

  [[nodiscard]] PlaybackState state() const;
  [[nodiscard]] std::uint64_t generation() const;
  [[nodiscard]] std::optional<VideoFrame> pop_video();
  [[nodiscard]] std::optional<AudioFrame> pop_audio();
  [[nodiscard]] std::uint64_t video_dropped_count() const;
  [[nodiscard]] std::uint64_t audio_dropped_count() const;
  [[nodiscard]] PlaybackSessionMetrics metrics() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shareme::media
