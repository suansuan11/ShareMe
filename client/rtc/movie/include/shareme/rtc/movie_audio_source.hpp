#pragma once

#include "shareme/rtc/local_audio_source.hpp"
#include "shareme/rtc/movie_timeline.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"

namespace shareme::media {
class PcmChunker;
class PlaybackSession;
struct PcmChunk;
} // namespace shareme::media

namespace shareme::rtc {

class MovieAudioSource final : private webrtc::RefCountedBase,
                               public LocalAudioSource {
public:
  static webrtc::scoped_refptr<MovieAudioSource>
  create(std::filesystem::path movie_path,
         std::shared_ptr<MovieTimeline> timeline);

  MovieAudioSource(std::filesystem::path movie_path,
                   std::shared_ptr<MovieTimeline> timeline);
  ~MovieAudioSource() override;

  MovieAudioSource(const MovieAudioSource &) = delete;
  MovieAudioSource &operator=(const MovieAudioSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;
  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::optional<std::int64_t>
  last_pts_ms() const noexcept override;
  [[nodiscard]] std::string error() const override;

  void AddSink(webrtc::AudioTrackSinkInterface *sink) override;
  void RemoveSink(webrtc::AudioTrackSinkInterface *sink) override;

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  void run(std::stop_token stop_token);
  bool emit_chunk(const media::PcmChunk &chunk, std::stop_token stop_token);
  void set_error(std::string category);

  const std::filesystem::path movie_path_;
  const std::shared_ptr<MovieTimeline> timeline_;
  std::unique_ptr<media::PlaybackSession> session_;
  std::unique_ptr<media::PcmChunker> chunker_;
  MovieTimeline::TimePoint epoch_{};
  std::int64_t media_start_time_ms_{0};
  std::jthread worker_;
  std::mutex pacing_mutex_;
  std::condition_variable_any pacing_changed_;
  std::atomic_bool running_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic_bool has_last_pts_{false};
  std::atomic<std::int64_t> last_pts_ms_{0};
  mutable std::mutex sink_mutex_;
  std::vector<webrtc::AudioTrackSinkInterface *> sinks_;
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
