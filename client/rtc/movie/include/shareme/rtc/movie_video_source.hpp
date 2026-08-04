#pragma once

#include "shareme/rtc/local_video_source.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "api/ref_counted_base.h"
#include "api/scoped_refptr.h"

namespace shareme::media {
class PlaybackSession;
struct VideoFrame;
} // namespace shareme::media

namespace shareme::rtc {

class MovieTimeline;

struct MovieVideoFrameSample {
  std::int64_t media_pts_ms{};
  std::uint32_t rtp_timestamp{};
  std::uint64_t generation{};
};

class MovieVideoSource final : private webrtc::RefCountedBase,
                               public LocalVideoSource {
public:
  static webrtc::scoped_refptr<MovieVideoSource>
  create(std::filesystem::path movie_path);
  static webrtc::scoped_refptr<MovieVideoSource>
  create(std::filesystem::path movie_path,
         std::shared_ptr<MovieTimeline> timeline);

  explicit MovieVideoSource(std::filesystem::path movie_path);
  MovieVideoSource(std::filesystem::path movie_path,
                   std::shared_ptr<MovieTimeline> timeline);
  ~MovieVideoSource() override;

  MovieVideoSource(const MovieVideoSource &) = delete;
  MovieVideoSource &operator=(const MovieVideoSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;
  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept override;
  [[nodiscard]] std::optional<std::int64_t>
  last_pts_ms() const noexcept override;
  [[nodiscard]] std::optional<MovieVideoFrameSample>
  last_frame_sample() const noexcept;
  [[nodiscard]] std::string error() const override;

  [[nodiscard]] bool is_screencast() const override;
  [[nodiscard]] std::optional<bool> needs_denoising() const override;
  [[nodiscard]] SourceState state() const override;
  [[nodiscard]] bool remote() const override;

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  void run(std::stop_token stop_token);
  bool emit_frame(const media::VideoFrame &frame, std::uint64_t generation);
  void set_error(std::string category);

  const std::filesystem::path movie_path_;
  const std::shared_ptr<MovieTimeline> timeline_;
  std::unique_ptr<media::PlaybackSession> session_;
  std::jthread worker_;
  std::atomic_bool running_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic<std::uint64_t> dropped_count_{0};
  std::atomic_bool has_last_pts_{false};
  std::atomic<std::int64_t> last_pts_ms_{0};
  std::atomic<std::int64_t> last_timestamp_us_{0};
  mutable std::mutex sample_mutex_;
  std::optional<MovieVideoFrameSample> last_frame_sample_;
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
