#include "shareme/rtc/movie_video_source.hpp"

#include "shareme/media/ffmpeg_media_source.hpp"
#include "shareme/media/media_frame.hpp"
#include "shareme/media/playback_session.hpp"
#include "shareme/rtc/movie_timeline.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <thread>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "libyuv/convert.h"
#include "rtc_base/time_utils.h"

namespace shareme::rtc {
namespace {

[[nodiscard]] bool add_elapsed(std::int64_t start_time_ms,
                               std::int64_t elapsed_ms,
                               std::int64_t &result) noexcept {
  if (elapsed_ms < 0 ||
      start_time_ms > std::numeric_limits<std::int64_t>::max() - elapsed_ms) {
    return false;
  }
  result = start_time_ms + elapsed_ms;
  return true;
}

[[nodiscard]] bool calculate_due_at(MovieTimeline::TimePoint epoch,
                                    std::int64_t pts_ms,
                                    std::int64_t start_time_ms,
                                    MovieTimeline::TimePoint &due_at) noexcept {
  if (pts_ms < start_time_ms)
    return false;

  const auto delta_ms = static_cast<std::uint64_t>(pts_ms) -
                        static_cast<std::uint64_t>(start_time_ms);
  const auto since_epoch = epoch.time_since_epoch();
  const auto available =
      since_epoch < MovieTimeline::Clock::duration::zero()
          ? MovieTimeline::Clock::duration::max()
          : MovieTimeline::Clock::duration::max() - since_epoch;
  const auto maximum_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(available).count();
  if (maximum_ms < 0 ||
      delta_ms > static_cast<std::uint64_t>(maximum_ms)) {
    return false;
  }

  due_at =
      epoch + std::chrono::milliseconds(static_cast<std::int64_t>(delta_ms));
  return true;
}

} // namespace

webrtc::scoped_refptr<MovieVideoSource>
MovieVideoSource::create(std::filesystem::path movie_path) {
  return create(std::move(movie_path), std::make_shared<MovieTimeline>());
}

webrtc::scoped_refptr<MovieVideoSource>
MovieVideoSource::create(std::filesystem::path movie_path,
                         std::shared_ptr<MovieTimeline> timeline) {
  if (!timeline)
    timeline = std::make_shared<MovieTimeline>();
  return webrtc::scoped_refptr<MovieVideoSource>(
      new MovieVideoSource(std::move(movie_path), std::move(timeline)));
}

MovieVideoSource::MovieVideoSource(std::filesystem::path movie_path)
    : MovieVideoSource(std::move(movie_path),
                       std::make_shared<MovieTimeline>()) {}

MovieVideoSource::MovieVideoSource(std::filesystem::path movie_path,
                                   std::shared_ptr<MovieTimeline> timeline)
    : movie_path_(std::move(movie_path)), timeline_(std::move(timeline)) {}

MovieVideoSource::~MovieVideoSource() { stop(); }

bool MovieVideoSource::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel))
    return true;

  try {
    auto session = std::make_unique<media::PlaybackSession>(
        std::make_unique<media::FfmpegMediaSource>(
            media::FfmpegMediaSourceOptions{.decode_video = true,
                                            .decode_audio = false}));
    const auto info = session->open(movie_path_);
    if (!info.has_video) {
      session->close();
      running_.store(false, std::memory_order_release);
      set_error("movie-video-unavailable");
      return false;
    }
    media_start_time_ms_ = info.start_time_ms;
    epoch_ = timeline_->start();
    session->play();
    session_ = std::move(session);
    worker_ =
        std::jthread([this](std::stop_token stop_token) { run(stop_token); });
    return true;
  } catch (const media::VideoStreamUnavailable &) {
    running_.store(false, std::memory_order_release);
    set_error("movie-video-unavailable");
    return false;
  } catch (const std::exception &) {
    running_.store(false, std::memory_order_release);
    set_error("movie-open-failed");
    return false;
  }
}

void MovieVideoSource::stop() noexcept {
  running_.store(false, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.request_stop();
    pacing_changed_.notify_all();
    worker_.join();
  }
  if (session_)
    session_->close();
}

std::uint64_t MovieVideoSource::generated_count() const noexcept {
  return generated_count_.load(std::memory_order_relaxed);
}

std::uint64_t MovieVideoSource::dropped_count() const noexcept {
  return dropped_count_.load(std::memory_order_relaxed);
}

std::optional<std::int64_t> MovieVideoSource::last_pts_ms() const noexcept {
  if (!has_last_pts_.load(std::memory_order_acquire))
    return std::nullopt;
  return last_pts_ms_.load(std::memory_order_relaxed);
}

std::string MovieVideoSource::error() const {
  std::lock_guard lock(error_mutex_);
  return error_;
}

bool MovieVideoSource::is_screencast() const { return false; }

std::optional<bool> MovieVideoSource::needs_denoising() const { return false; }

webrtc::MediaSourceInterface::SourceState MovieVideoSource::state() const {
  return running_.load(std::memory_order_acquire) ? kLive : kEnded;
}

bool MovieVideoSource::remote() const { return false; }

void MovieVideoSource::run(std::stop_token stop_token) {
  using namespace std::chrono_literals;

  try {
    while (!stop_token.stop_requested()) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              MovieTimeline::Clock::now() - epoch_);
      std::int64_t playhead_ms = 0;
      if (!add_elapsed(media_start_time_ms_, elapsed.count(), playhead_ms)) {
        set_error("movie-frame-invalid");
        break;
      }
      session_->set_playhead_ms(playhead_ms);

      bool emitted = false;
      bool invalid_frame = false;
      while (auto frame = session_->pop_video()) {
        MovieTimeline::TimePoint due_at;
        if (!calculate_due_at(epoch_, frame->pts_ms, media_start_time_ms_,
                              due_at)) {
          set_error("movie-frame-invalid");
          invalid_frame = true;
          break;
        }
        if (MovieTimeline::Clock::now() < due_at) {
          std::unique_lock lock(pacing_mutex_);
          pacing_changed_.wait_until(lock, stop_token, due_at,
                                     [] { return false; });
        }
        if (stop_token.stop_requested())
          break;
        emitted = emit_frame(*frame) || emitted;
      }
      if (invalid_frame)
        break;

      const auto playback_state = session_->state();
      if (playback_state == media::PlaybackState::failed) {
        set_error("movie-decode-failed");
        break;
      }
      if (playback_state == media::PlaybackState::ended && !emitted)
        break;
      std::unique_lock lock(pacing_mutex_);
      pacing_changed_.wait_for(lock, stop_token, 2ms, [] { return false; });
    }
  } catch (const std::exception &) {
    set_error("movie-decode-failed");
  }
  running_.store(false, std::memory_order_release);
}

bool MovieVideoSource::emit_frame(const media::VideoFrame &frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
      frame.rgba.size() < static_cast<std::size_t>(frame.stride) *
                              static_cast<std::size_t>(frame.height)) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    set_error("movie-frame-invalid");
    return false;
  }

  auto input = webrtc::I420Buffer::Create(frame.width, frame.height);
  const auto conversion = libyuv::ABGRToI420(
      reinterpret_cast<const std::uint8_t *>(frame.rgba.data()), frame.stride,
      input->MutableDataY(), input->StrideY(), input->MutableDataU(),
      input->StrideU(), input->MutableDataV(), input->StrideV(), frame.width,
      frame.height);
  if (conversion != 0) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    set_error("movie-frame-conversion-failed");
    return false;
  }

  auto timestamp_us = webrtc::TimeMicros();
  const auto previous = last_timestamp_us_.load(std::memory_order_relaxed);
  timestamp_us = std::max(timestamp_us, previous + 1);

  int output_width = 0;
  int output_height = 0;
  int crop_width = 0;
  int crop_height = 0;
  int crop_x = 0;
  int crop_y = 0;
  if (!AdaptFrame(frame.width, frame.height, timestamp_us, &output_width,
                  &output_height, &crop_width, &crop_height, &crop_x,
                  &crop_y)) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  webrtc::scoped_refptr<webrtc::I420Buffer> output = input;
  if (output_width != frame.width || output_height != frame.height ||
      crop_width != frame.width || crop_height != frame.height || crop_x != 0 ||
      crop_y != 0) {
    output = webrtc::I420Buffer::Create(output_width, output_height);
    output->CropAndScaleFrom(*input, crop_x, crop_y, crop_width, crop_height);
  }

  const auto rtp_timestamp = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(std::max<std::int64_t>(0, frame.pts_ms)) *
      90U);
  const auto video_frame = webrtc::VideoFrame::Builder()
                               .set_video_frame_buffer(output)
                               .set_timestamp_us(timestamp_us)
                               .set_rtp_timestamp(rtp_timestamp)
                               .set_rotation(webrtc::kVideoRotation_0)
                               .build();
  OnFrame(video_frame);
  last_timestamp_us_.store(timestamp_us, std::memory_order_relaxed);
  last_pts_ms_.store(frame.pts_ms, std::memory_order_relaxed);
  has_last_pts_.store(true, std::memory_order_release);
  generated_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void MovieVideoSource::set_error(std::string category) {
  std::lock_guard lock(error_mutex_);
  if (error_.empty())
    error_ = std::move(category);
}

} // namespace shareme::rtc
