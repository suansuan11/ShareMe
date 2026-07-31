#pragma once

#include "shareme/rtc/local_video_source.hpp"

#include <atomic>
#include <condition_variable>
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

class MovieVideoSource final : private webrtc::RefCountedBase,
                               public LocalVideoSource {
public:
  static webrtc::scoped_refptr<MovieVideoSource>
  create(std::filesystem::path movie_path);

  explicit MovieVideoSource(std::filesystem::path movie_path);
  ~MovieVideoSource() override;

  MovieVideoSource(const MovieVideoSource &) = delete;
  MovieVideoSource &operator=(const MovieVideoSource &) = delete;

  [[nodiscard]] bool start() override;
  void stop() noexcept override;
  [[nodiscard]] std::uint64_t generated_count() const noexcept override;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept override;
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
  bool emit_frame(const media::VideoFrame &frame);
  void set_error(std::string category);

  const std::filesystem::path movie_path_;
  std::unique_ptr<media::PlaybackSession> session_;
  std::jthread worker_;
  std::mutex pacing_mutex_;
  std::condition_variable_any pacing_changed_;
  std::atomic_bool running_{false};
  std::atomic<std::uint64_t> generated_count_{0};
  std::atomic<std::uint64_t> dropped_count_{0};
  std::atomic<std::int64_t> last_timestamp_us_{0};
  mutable std::mutex error_mutex_;
  std::string error_;
};

} // namespace shareme::rtc
