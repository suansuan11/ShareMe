#include "shareme/media/playback_session.hpp"

#include "shareme/core/bounded_queue.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

namespace shareme::media {

class PlaybackSession::Impl {
public:
  explicit Impl(std::unique_ptr<IMediaSource> source)
      : source_{std::move(source)},
        video_queue_{3, core::OverflowPolicy::drop_oldest},
        audio_queue_{24, core::OverflowPolicy::reject_newest} {
    if (source_ == nullptr) {
      throw std::invalid_argument{"PlaybackSession requires a media source"};
    }
  }

  ~Impl() {
    close();
  }

  MediaInfo open(const std::filesystem::path& path) {
    close();

    try {
      std::scoped_lock source_lock{source_mutex_};
      auto info = source_->open(path);
      {
        std::scoped_lock state_lock{state_mutex_};
        source_is_open_ = true;
        generation_ = 0;
        state_ = PlaybackState::paused;
      }
      return info;
    } catch (...) {
      std::scoped_lock state_lock{state_mutex_};
      state_ = PlaybackState::failed;
      throw;
    }
  }

  void play() {
    std::scoped_lock lock{state_mutex_};
    if (state_ != PlaybackState::paused) {
      return;
    }
    if (!worker_.joinable()) {
      worker_ = std::jthread{
          [this](const std::stop_token& stop_token) { run(stop_token); }};
    }
    state_ = PlaybackState::playing;
    state_changed_.notify_all();
  }

  void pause() {
    std::scoped_lock lock{state_mutex_};
    if (state_ == PlaybackState::playing) {
      state_ = PlaybackState::paused;
    }
  }

  void seek(std::int64_t target_ms) {
    bool resume_after_seek = false;
    {
      std::scoped_lock state_lock{state_mutex_};
      if (!source_is_open_) {
        throw std::logic_error{"Cannot seek a closed playback session"};
      }
      resume_after_seek = state_ == PlaybackState::playing;
      state_ = PlaybackState::paused;
      ++generation_;
    }

    video_queue_.clear();
    audio_queue_.clear();

    try {
      std::scoped_lock source_lock{source_mutex_};
      source_->seek(target_ms);
    } catch (...) {
      std::scoped_lock state_lock{state_mutex_};
      state_ = PlaybackState::failed;
      throw;
    }

    {
      std::scoped_lock state_lock{state_mutex_};
      state_ =
          resume_after_seek ? PlaybackState::playing : PlaybackState::paused;
    }
    state_changed_.notify_all();
  }

  void close() noexcept {
    {
      std::scoped_lock state_lock{state_mutex_};
      state_ = PlaybackState::closed;
    }

    if (worker_.joinable()) {
      worker_.request_stop();
      state_changed_.notify_all();
      worker_.join();
    }

    bool should_close_source = false;
    {
      std::scoped_lock state_lock{state_mutex_};
      should_close_source = source_is_open_;
      source_is_open_ = false;
    }
    if (should_close_source) {
      std::scoped_lock source_lock{source_mutex_};
      source_->close();
    }

    video_queue_.clear();
    audio_queue_.clear();
  }

  [[nodiscard]] PlaybackState state() const {
    std::scoped_lock lock{state_mutex_};
    return state_;
  }

  [[nodiscard]] std::uint64_t generation() const {
    std::scoped_lock lock{state_mutex_};
    return generation_;
  }

  [[nodiscard]] std::optional<VideoFrame> pop_video() {
    return video_queue_.pop();
  }

  [[nodiscard]] std::optional<AudioFrame> pop_audio() {
    return audio_queue_.pop();
  }

  [[nodiscard]] std::uint64_t video_dropped_count() const {
    return video_queue_.dropped_count();
  }

  [[nodiscard]] std::uint64_t audio_dropped_count() const {
    return audio_queue_.dropped_count();
  }

private:
  void run(const std::stop_token& stop_token) {
    while (!stop_token.stop_requested()) {
      std::uint64_t requested_generation = 0;
      {
        std::unique_lock lock{state_mutex_};
        state_changed_.wait(lock, stop_token, [this] {
          return state_ == PlaybackState::playing;
        });
        if (stop_token.stop_requested()) {
          return;
        }
        requested_generation = generation_;
      }

      MediaEvent event;
      try {
        std::scoped_lock source_lock{source_mutex_};
        event = source_->read_next(requested_generation);
      } catch (...) {
        std::scoped_lock state_lock{state_mutex_};
        state_ = PlaybackState::failed;
        return;
      }

      if (std::holds_alternative<EndOfStream>(event)) {
        std::scoped_lock state_lock{state_mutex_};
        if (generation_ == requested_generation &&
            state_ == PlaybackState::playing) {
          state_ = PlaybackState::ended;
        }
        continue;
      }

      {
        std::scoped_lock state_lock{state_mutex_};
        if (generation_ != requested_generation ||
            state_ == PlaybackState::closed) {
          continue;
        }
      }

      if (auto* video = std::get_if<VideoFrame>(&event);
          video != nullptr && video->generation == requested_generation) {
        static_cast<void>(video_queue_.push(std::move(*video)));
      } else if (auto* audio = std::get_if<AudioFrame>(&event);
                 audio != nullptr &&
                 audio->generation == requested_generation) {
        static_cast<void>(audio_queue_.push(std::move(*audio)));
      }
    }
  }

  std::unique_ptr<IMediaSource> source_;
  mutable std::mutex state_mutex_;
  std::mutex source_mutex_;
  std::condition_variable_any state_changed_;
  std::jthread worker_;
  PlaybackState state_{PlaybackState::closed};
  std::uint64_t generation_{0};
  bool source_is_open_{false};
  core::BoundedQueue<VideoFrame> video_queue_;
  core::BoundedQueue<AudioFrame> audio_queue_;
};

PlaybackSession::PlaybackSession(std::unique_ptr<IMediaSource> source)
    : impl_{std::make_unique<Impl>(std::move(source))} {}

PlaybackSession::~PlaybackSession() = default;

MediaInfo PlaybackSession::open(const std::filesystem::path& path) {
  return impl_->open(path);
}

void PlaybackSession::play() {
  impl_->play();
}

void PlaybackSession::pause() {
  impl_->pause();
}

void PlaybackSession::seek(std::int64_t target_ms) {
  impl_->seek(target_ms);
}

void PlaybackSession::close() noexcept {
  impl_->close();
}

PlaybackState PlaybackSession::state() const {
  return impl_->state();
}

std::uint64_t PlaybackSession::generation() const {
  return impl_->generation();
}

std::optional<VideoFrame> PlaybackSession::pop_video() {
  return impl_->pop_video();
}

std::optional<AudioFrame> PlaybackSession::pop_audio() {
  return impl_->pop_audio();
}

std::uint64_t PlaybackSession::video_dropped_count() const {
  return impl_->video_dropped_count();
}

std::uint64_t PlaybackSession::audio_dropped_count() const {
  return impl_->audio_dropped_count();
}

}  // namespace shareme::media
