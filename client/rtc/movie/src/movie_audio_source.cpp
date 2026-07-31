#include "shareme/rtc/movie_audio_source.hpp"

#include "shareme/media/ffmpeg_media_source.hpp"
#include "shareme/media/pcm_chunker.hpp"
#include "shareme/media/playback_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <thread>
#include <utility>

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

webrtc::scoped_refptr<MovieAudioSource>
MovieAudioSource::create(std::filesystem::path movie_path,
                         std::shared_ptr<MovieTimeline> timeline) {
  if (!timeline)
    timeline = std::make_shared<MovieTimeline>();
  return webrtc::scoped_refptr<MovieAudioSource>(
      new MovieAudioSource(std::move(movie_path), std::move(timeline)));
}

MovieAudioSource::MovieAudioSource(std::filesystem::path movie_path,
                                   std::shared_ptr<MovieTimeline> timeline)
    : movie_path_(std::move(movie_path)), timeline_(std::move(timeline)) {}

MovieAudioSource::~MovieAudioSource() { stop(); }

bool MovieAudioSource::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return true;
  }

  try {
    auto session = std::make_unique<media::PlaybackSession>(
        std::make_unique<media::FfmpegMediaSource>(
            media::FfmpegMediaSourceOptions{.decode_video = false,
                                            .decode_audio = true}));
    const auto info = session->open(movie_path_);
    if (!info.has_audio) {
      session->close();
      running_.store(false, std::memory_order_release);
      set_error("movie-audio-unavailable");
      return false;
    }

    media_start_time_ms_ = info.start_time_ms;
    epoch_ = timeline_->start();
    chunker_ = std::make_unique<media::PcmChunker>();
    session->play();
    session_ = std::move(session);
    worker_ =
        std::jthread([this](std::stop_token stop_token) { run(stop_token); });
    return true;
  } catch (const media::AudioStreamUnavailable &) {
    running_.store(false, std::memory_order_release);
    set_error("movie-audio-unavailable");
    return false;
  } catch (const std::exception &) {
    running_.store(false, std::memory_order_release);
    set_error("movie-audio-open-failed");
    return false;
  }
}

void MovieAudioSource::stop() noexcept {
  running_.store(false, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.request_stop();
    pacing_changed_.notify_all();
    worker_.join();
  }
  if (session_)
    session_->close();
}

std::uint64_t MovieAudioSource::generated_count() const noexcept {
  return generated_count_.load(std::memory_order_relaxed);
}

std::optional<std::int64_t> MovieAudioSource::last_pts_ms() const noexcept {
  if (!has_last_pts_.load(std::memory_order_acquire))
    return std::nullopt;
  return last_pts_ms_.load(std::memory_order_relaxed);
}

std::string MovieAudioSource::error() const {
  std::lock_guard lock(error_mutex_);
  return error_;
}

void MovieAudioSource::AddSink(webrtc::AudioTrackSinkInterface *sink) {
  if (sink == nullptr)
    return;
  std::lock_guard lock(sink_mutex_);
  if (std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end())
    sinks_.push_back(sink);
}

void MovieAudioSource::RemoveSink(webrtc::AudioTrackSinkInterface *sink) {
  std::lock_guard lock(sink_mutex_);
  std::erase(sinks_, sink);
}

void MovieAudioSource::run(std::stop_token stop_token) {
  using namespace std::chrono_literals;
  try {
    while (!stop_token.stop_requested()) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              MovieTimeline::Clock::now() - epoch_);
      std::int64_t playhead_ms = 0;
      if (!add_elapsed(media_start_time_ms_, elapsed.count(), playhead_ms)) {
        set_error("movie-audio-frame-invalid");
        break;
      }
      session_->set_playhead_ms(playhead_ms);

      bool emitted = false;
      bool invalid_frame = false;
      while (auto frame = session_->pop_audio()) {
        if (!chunker_->push(std::move(*frame))) {
          set_error("movie-audio-frame-invalid");
          invalid_frame = true;
          break;
        }
        while (auto chunk = chunker_->pop()) {
          if (!emit_chunk(*chunk, stop_token)) {
            invalid_frame = !stop_token.stop_requested();
            break;
          }
          emitted = true;
        }
        if (invalid_frame || stop_token.stop_requested())
          break;
      }
      if (invalid_frame)
        break;

      const auto playback_state = session_->state();
      if (playback_state == media::PlaybackState::failed) {
        set_error("movie-audio-decode-failed");
        break;
      }
      if (playback_state == media::PlaybackState::ended && !emitted)
        break;

      std::unique_lock lock(pacing_mutex_);
      pacing_changed_.wait_for(lock, stop_token, 2ms, [] { return false; });
    }
  } catch (const std::exception &) {
    set_error("movie-audio-decode-failed");
  }
  running_.store(false, std::memory_order_release);
}

bool MovieAudioSource::emit_chunk(const media::PcmChunk &chunk,
                                  std::stop_token stop_token) {
  constexpr std::size_t expected_sample_count = 480U * 2U;
  if (chunk.interleaved_samples.size() != expected_sample_count) {
    set_error("movie-audio-frame-invalid");
    return false;
  }

  MovieTimeline::TimePoint due_at;
  if (!calculate_due_at(epoch_, chunk.pts_ms, media_start_time_ms_, due_at)) {
    set_error("movie-audio-frame-invalid");
    return false;
  }
  if (MovieTimeline::Clock::now() < due_at) {
    std::unique_lock lock(pacing_mutex_);
    pacing_changed_.wait_until(lock, stop_token, due_at, [] { return false; });
  }
  if (stop_token.stop_requested())
    return false;

  if (has_last_pts_.load(std::memory_order_acquire) &&
      chunk.pts_ms <= last_pts_ms_.load(std::memory_order_relaxed)) {
    set_error("movie-audio-frame-invalid");
    return false;
  }

  const auto capture_timestamp_ms = webrtc::TimeMillis();
  {
    std::lock_guard lock(sink_mutex_);
    for (auto *sink : sinks_) {
      sink->OnData(chunk.interleaved_samples.data(), 16, 48'000, 2, 480,
                   capture_timestamp_ms);
    }
  }

  last_pts_ms_.store(chunk.pts_ms, std::memory_order_relaxed);
  has_last_pts_.store(true, std::memory_order_release);
  generated_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void MovieAudioSource::set_error(std::string category) {
  std::lock_guard lock(error_mutex_);
  if (error_.empty())
    error_ = std::move(category);
}

} // namespace shareme::rtc
