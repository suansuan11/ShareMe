#include "shareme/rtc/movie_audio_source.hpp"

#include "shareme/media/ffmpeg_media_source.hpp"
#include "shareme/media/pcm_chunker.hpp"
#include "shareme/media/playback_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <thread>
#include <utility>

#include "rtc_base/time_utils.h"

namespace shareme::rtc {
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

    if (!timeline_->initialize(info.start_time_ms, info.duration_ms)) {
      session->close();
      running_.store(false, std::memory_order_release);
      set_error("movie-timeline-mismatch");
      return false;
    }
    const auto timeline = timeline_->snapshot();
    if (!timeline) {
      session->close();
      running_.store(false, std::memory_order_release);
      set_error("movie-timeline-mismatch");
      return false;
    }
    if (timeline->media_pts_ms != info.start_time_ms)
      session->seek(timeline->media_pts_ms);
    chunker_ = std::make_unique<media::PcmChunker>();
    if (timeline->state == MovieTimelineState::playing)
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
    auto initial = timeline_->snapshot();
    if (!initial) {
      set_error("movie-timeline-mismatch");
      running_.store(false, std::memory_order_release);
      return;
    }
    auto applied_generation = initial->generation;
    auto minimum_pts_ms = initial->media_pts_ms;
    while (!stop_token.stop_requested()) {
      const auto timeline = timeline_->snapshot();
      if (!timeline) {
        set_error("movie-timeline-mismatch");
        break;
      }
      if (timeline->generation != applied_generation) {
        session_->seek(timeline->media_pts_ms);
        chunker_ = std::make_unique<media::PcmChunker>();
        applied_generation = timeline->generation;
        minimum_pts_ms = timeline->media_pts_ms;
        has_last_pts_.store(false, std::memory_order_release);
      }
      if (timeline->state == MovieTimelineState::paused) {
        session_->pause();
        if (timeline->media_pts_ms <
            timeline->start_pts_ms + timeline->duration_ms) {
          const auto wait_result = timeline_->wait_until(
              timeline->media_pts_ms + 1, applied_generation, stop_token);
          if (wait_result == MovieTimelineWaitResult::stopped)
            break;
          continue;
        }
      } else {
        session_->play();
      }
      session_->set_playhead_ms(timeline->media_pts_ms);

      bool emitted = false;
      bool invalid_frame = false;
      bool generation_changed = false;
      while (auto frame = session_->pop_audio()) {
        if (frame->pts_ms < minimum_pts_ms)
          continue;
        if (!chunker_->push(std::move(*frame))) {
          set_error("movie-audio-frame-invalid");
          invalid_frame = true;
          break;
        }
        while (auto chunk = chunker_->pop()) {
          if (chunk->pts_ms < minimum_pts_ms)
            continue;
          const auto wait_result = timeline_->wait_until(
              chunk->pts_ms, applied_generation, stop_token);
          if (wait_result == MovieTimelineWaitResult::generation_changed) {
            generation_changed = true;
            break;
          }
          if (wait_result == MovieTimelineWaitResult::stopped)
            break;
          if (!emit_chunk(*chunk)) {
            invalid_frame = true;
            break;
          }
          emitted = true;
        }
        if (invalid_frame || generation_changed || stop_token.stop_requested())
          break;
      }
      if (invalid_frame)
        break;
      if (generation_changed)
        continue;
      if (stop_token.stop_requested())
        break;

      const auto playback_state = session_->state();
      if (playback_state == media::PlaybackState::failed) {
        set_error("movie-audio-decode-failed");
        break;
      }
      if (playback_state == media::PlaybackState::ended && !emitted)
        break;

      std::this_thread::sleep_for(2ms);
    }
  } catch (const std::exception &) {
    set_error("movie-audio-decode-failed");
  }
  running_.store(false, std::memory_order_release);
}

bool MovieAudioSource::emit_chunk(const media::PcmChunk &chunk) {
  constexpr std::size_t expected_sample_count = 480U * 2U;
  if (chunk.interleaved_samples.size() != expected_sample_count) {
    set_error("movie-audio-frame-invalid");
    return false;
  }

  if (has_last_pts_.load(std::memory_order_acquire) &&
      chunk.pts_ms <= last_pts_ms_.load(std::memory_order_relaxed)) {
    set_error("movie-audio-frame-invalid");
    return false;
  }

  last_pts_ms_.store(chunk.pts_ms, std::memory_order_relaxed);
  has_last_pts_.store(true, std::memory_order_release);
  const auto capture_timestamp_ms = webrtc::TimeMillis();
  {
    std::lock_guard lock(sink_mutex_);
    for (auto *sink : sinks_) {
      sink->OnData(chunk.interleaved_samples.data(), 16, 48'000, 2, 480,
                   capture_timestamp_ms);
    }
  }

  generated_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void MovieAudioSource::set_error(std::string category) {
  std::lock_guard lock(error_mutex_);
  if (error_.empty())
    error_ = std::move(category);
}

} // namespace shareme::rtc
