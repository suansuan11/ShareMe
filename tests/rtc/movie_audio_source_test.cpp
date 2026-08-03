#include "counting_video_sink.hpp"
#include "shareme/rtc/movie_audio_source.hpp"
#include "shareme/rtc/movie_timeline.hpp"
#include "shareme/rtc/movie_video_source.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "api/media_stream_interface.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace {

struct TimedMediaSample {
  std::int64_t pts_ms;
  std::chrono::steady_clock::time_point emitted_at;
};

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

class CountingPcmSink final : public webrtc::AudioTrackSinkInterface {
public:
  explicit CountingPcmSink(
      const shareme::rtc::MovieAudioSource *source = nullptr)
      : source_(source) {}

  void OnData(const void *audio_data, int bits_per_sample, int sample_rate,
              std::size_t number_of_channels, std::size_t number_of_frames,
              std::optional<std::int64_t> capture_timestamp_ms) override {
    std::lock_guard lock(mutex_);
    if (callback_count_ == 0)
      first_callback_at_ = std::chrono::steady_clock::now();
    ++callback_count_;
    exact_format_ =
        exact_format_ && audio_data != nullptr && bits_per_sample == 16 &&
        sample_rate == 48'000 && number_of_channels == 2 &&
        number_of_frames == 480 && capture_timestamp_ms.has_value();
    if (last_capture_timestamp_ms_ && capture_timestamp_ms &&
        *capture_timestamp_ms < *last_capture_timestamp_ms_) {
      timestamps_monotonic_ = false;
    }
    last_capture_timestamp_ms_ = capture_timestamp_ms;
    if (source_) {
      if (const auto media_pts_ms = source_->last_pts_ms()) {
        media_pts_ms_.push_back(*media_pts_ms);
        timed_samples_.push_back(
            TimedMediaSample{*media_pts_ms, std::chrono::steady_clock::now()});
      }
    }

    if (audio_data != nullptr) {
      const auto *samples = static_cast<const std::int16_t *>(audio_data);
      non_silent_ =
          non_silent_ ||
          std::any_of(samples, samples + number_of_channels * number_of_frames,
                      [](std::int16_t sample) { return sample != 0; });
    }
  }

  [[nodiscard]] std::uint64_t callback_count() const {
    std::lock_guard lock(mutex_);
    return callback_count_;
  }

  [[nodiscard]] bool exact_format() const {
    std::lock_guard lock(mutex_);
    return exact_format_;
  }

  [[nodiscard]] bool non_silent() const {
    std::lock_guard lock(mutex_);
    return non_silent_;
  }

  [[nodiscard]] bool timestamps_monotonic() const {
    std::lock_guard lock(mutex_);
    return timestamps_monotonic_;
  }

  [[nodiscard]] std::vector<std::int64_t> media_pts_ms() const {
    std::lock_guard lock(mutex_);
    return media_pts_ms_;
  }

  [[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
  first_callback_at() const {
    std::lock_guard lock(mutex_);
    return first_callback_at_;
  }

  [[nodiscard]] std::vector<TimedMediaSample> timed_samples() const {
    std::lock_guard lock(mutex_);
    return timed_samples_;
  }

private:
  const shareme::rtc::MovieAudioSource *source_;
  mutable std::mutex mutex_;
  std::uint64_t callback_count_{0};
  bool exact_format_{true};
  bool non_silent_{false};
  bool timestamps_monotonic_{true};
  std::optional<std::int64_t> last_capture_timestamp_ms_;
  std::vector<std::int64_t> media_pts_ms_;
  std::vector<TimedMediaSample> timed_samples_;
  std::optional<std::chrono::steady_clock::time_point> first_callback_at_;
};

class TimedVideoSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  explicit TimedVideoSink(const shareme::rtc::MovieVideoSource *source)
      : source_(source) {}

  void OnFrame(const webrtc::VideoFrame &frame) override {
    {
      std::lock_guard lock(mutex_);
      if (!first_callback_at_)
        first_callback_at_ = std::chrono::steady_clock::now();
      timed_samples_.push_back(TimedMediaSample{
          source_->last_pts_ms().value_or(0),
          std::chrono::steady_clock::now()});
    }
    sink_.OnFrame(frame);
  }

  [[nodiscard]] std::uint64_t frame_count() const noexcept {
    return sink_.frame_count();
  }

  [[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
  first_callback_at() const {
    std::lock_guard lock(mutex_);
    return first_callback_at_;
  }

  [[nodiscard]] std::vector<TimedMediaSample> timed_samples() const {
    std::lock_guard lock(mutex_);
    return timed_samples_;
  }

private:
  const shareme::rtc::MovieVideoSource *source_;
  shareme::rtc::CountingVideoSink sink_;
  mutable std::mutex mutex_;
  std::optional<std::chrono::steady_clock::time_point> first_callback_at_;
  std::vector<TimedMediaSample> timed_samples_;
};

void require_overlapping_tracks_are_synchronized(
    const std::vector<TimedMediaSample> &audio_samples,
    const std::vector<TimedMediaSample> &video_samples) {
  using namespace std::chrono;
  REQUIRE(!audio_samples.empty());
  REQUIRE(!video_samples.empty());

  std::size_t matched_video_frames = 0;
  for (const auto &video : video_samples) {
    const auto audio = std::lower_bound(
        audio_samples.begin(), audio_samples.end(), video.pts_ms,
        [](const TimedMediaSample &sample, std::int64_t pts_ms) {
          return sample.pts_ms < pts_ms;
        });
    if (audio == audio_samples.end())
      break;

    const auto pts_delta_ms = video.pts_ms - audio->pts_ms;
    if (std::llabs(pts_delta_ms) > 10)
      continue;
    const auto callback_delta_ms =
        duration_cast<milliseconds>(video.emitted_at - audio->emitted_at)
            .count();
    REQUIRE(std::llabs(callback_delta_ms - pts_delta_ms) <= 50);
    ++matched_video_frames;
  }
  REQUIRE(matched_video_frames >= 20);
}

void source_is_live_and_disables_voice_processing(
    const std::filesystem::path &movie_path) {
  auto source = shareme::rtc::MovieAudioSource::create(
      movie_path, std::make_shared<shareme::rtc::MovieTimeline>());
  REQUIRE(source->state() == webrtc::MediaSourceInterface::kLive);
  REQUIRE(!source->remote());
  REQUIRE(source->options().echo_cancellation == std::optional<bool>{false});
  REQUIRE(source->options().noise_suppression == std::optional<bool>{false});
  REQUIRE(source->options().auto_gain_control == std::optional<bool>{false});
  source->stop();
  REQUIRE(source->state() == webrtc::MediaSourceInterface::kLive);
}

void emits_exact_pcm_chunks(const std::filesystem::path &movie_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieAudioSource::create(
      movie_path, std::make_shared<shareme::rtc::MovieTimeline>());
  CountingPcmSink primary_sink{source.get()};
  CountingPcmSink removable_sink;
  source->AddSink(&primary_sink);
  source->AddSink(&removable_sink);

  REQUIRE(source->start());
  const auto removal_deadline = std::chrono::steady_clock::now() + 1s;
  while (removable_sink.callback_count() < 10 &&
         source->error().empty() &&
         std::chrono::steady_clock::now() < removal_deadline) {
    std::this_thread::sleep_for(5ms);
  }
  REQUIRE(removable_sink.callback_count() >= 10);
  source->RemoveSink(&removable_sink);
  const auto removed_count = removable_sink.callback_count();

  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (primary_sink.callback_count() < 100 && source->error().empty() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  source->stop();
  source->stop();
  REQUIRE(removable_sink.callback_count() == removed_count);
  source->RemoveSink(&primary_sink);

  REQUIRE(source->error().empty());
  REQUIRE(primary_sink.callback_count() >= 100);
  REQUIRE(primary_sink.exact_format());
  REQUIRE(primary_sink.non_silent());
  REQUIRE(primary_sink.timestamps_monotonic());
  REQUIRE(source->generated_count() >= primary_sink.callback_count());
  REQUIRE(source->last_pts_ms().has_value());
  const auto media_pts_ms = primary_sink.media_pts_ms();
  REQUIRE(media_pts_ms.size() == primary_sink.callback_count());
  REQUIRE(std::adjacent_find(
              media_pts_ms.begin(), media_pts_ms.end(),
              [](std::int64_t previous, std::int64_t current) {
                return current <= previous || current - previous != 10;
              }) == media_pts_ms.end());
}

void shared_timeline_controls_audio(const std::filesystem::path &movie_path) {
  using namespace std::chrono_literals;
  auto timeline = std::make_shared<shareme::rtc::MovieTimeline>();
  auto source = shareme::rtc::MovieAudioSource::create(movie_path, timeline);
  CountingPcmSink sink{source.get()};
  source->AddSink(&sink);
  REQUIRE(source->start());

  const auto initial_deadline = std::chrono::steady_clock::now() + 1s;
  while (sink.callback_count() < 10 && source->error().empty() &&
         std::chrono::steady_clock::now() < initial_deadline)
    std::this_thread::sleep_for(5ms);
  REQUIRE(sink.callback_count() >= 10);

  REQUIRE(timeline->pause());
  const auto paused_count = sink.callback_count();
  std::this_thread::sleep_for(150ms);
  REQUIRE(sink.callback_count() <= paused_count + 1);
  REQUIRE(timeline->resume());
  const auto resumed_deadline = std::chrono::steady_clock::now() + 500ms;
  while (sink.callback_count() <= paused_count + 1 &&
         std::chrono::steady_clock::now() < resumed_deadline)
    std::this_thread::sleep_for(5ms);
  REQUIRE(sink.callback_count() > paused_count + 1);

  const auto snapshot = timeline->snapshot();
  REQUIRE(snapshot.has_value());
  const auto target_pts_ms = snapshot->start_pts_ms + 1'000;
  REQUIRE(timeline->seek(target_pts_ms));
  const auto seek_deadline = std::chrono::steady_clock::now() + 1s;
  while ((!source->last_pts_ms() ||
          *source->last_pts_ms() < target_pts_ms) &&
         source->error().empty() &&
         std::chrono::steady_clock::now() < seek_deadline)
    std::this_thread::sleep_for(5ms);
  REQUIRE(source->last_pts_ms().has_value());
  REQUIRE(*source->last_pts_ms() >= target_pts_ms);
  const auto first_post_seek_pts = *source->last_pts_ms();
  std::this_thread::sleep_for(100ms);
  REQUIRE(*source->last_pts_ms() >= first_post_seek_pts);

  REQUIRE(timeline->seek(snapshot->start_pts_ms + 500));
  const auto backward_deadline = std::chrono::steady_clock::now() + 1s;
  while ((!source->last_pts_ms() ||
          *source->last_pts_ms() >= snapshot->start_pts_ms + 900) &&
         source->error().empty() &&
         std::chrono::steady_clock::now() < backward_deadline)
    std::this_thread::sleep_for(5ms);
  REQUIRE(source->last_pts_ms().has_value());
  REQUIRE(*source->last_pts_ms() >= snapshot->start_pts_ms + 500);
  REQUIRE(*source->last_pts_ms() < snapshot->start_pts_ms + 900);
  REQUIRE(sink.timestamps_monotonic());

  source->stop();
  source->RemoveSink(&sink);
  REQUIRE(source->error().empty());
}

void audio_only_movie_is_supported(
    const std::filesystem::path &audio_only_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieAudioSource::create(
      audio_only_path, std::make_shared<shareme::rtc::MovieTimeline>());
  CountingPcmSink sink;
  source->AddSink(&sink);
  REQUIRE(source->start());
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (sink.callback_count() < 50 && source->error().empty() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  source->stop();
  source->RemoveSink(&sink);
  REQUIRE(source->error().empty());
  REQUIRE(sink.callback_count() >= 50);
}

void failures_are_sanitized(const std::filesystem::path &directory,
                            const std::filesystem::path &video_only_path) {
  auto missing = shareme::rtc::MovieAudioSource::create(
      directory / "does-not-exist.mp4",
      std::make_shared<shareme::rtc::MovieTimeline>());
  REQUIRE(!missing->start());
  REQUIRE(missing->generated_count() == 0);
  REQUIRE(missing->error() == "movie-audio-open-failed");
  missing->stop();

  auto video_only = shareme::rtc::MovieAudioSource::create(
      video_only_path, std::make_shared<shareme::rtc::MovieTimeline>());
  REQUIRE(!video_only->start());
  REQUIRE(video_only->generated_count() == 0);
  REQUIRE(video_only->error() == "movie-audio-unavailable");
  video_only->stop();
}

void stop_interrupts_future_audio(const std::filesystem::path &audio_gap_path) {
  using namespace std::chrono_literals;
  auto source = shareme::rtc::MovieAudioSource::create(
      audio_gap_path, std::make_shared<shareme::rtc::MovieTimeline>());
  CountingPcmSink sink;
  source->AddSink(&sink);
  REQUIRE(source->start());

  const auto first_chunk_deadline = std::chrono::steady_clock::now() + 1s;
  while (sink.callback_count() == 0 && source->error().empty() &&
         std::chrono::steady_clock::now() < first_chunk_deadline) {
    std::this_thread::sleep_for(5ms);
  }
  REQUIRE(sink.callback_count() > 0);
  std::this_thread::sleep_for(50ms);

  const auto stop_started_at = std::chrono::steady_clock::now();
  source->stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started_at;
  source->RemoveSink(&sink);
  REQUIRE(stop_elapsed < 500ms);
}

void shared_timeline_preserves_track_offset(
    const std::filesystem::path &staggered_path) {
  using namespace std::chrono_literals;
  auto timeline = std::make_shared<shareme::rtc::MovieTimeline>();
  auto audio =
      shareme::rtc::MovieAudioSource::create(staggered_path, timeline);
  auto video =
      shareme::rtc::MovieVideoSource::create(staggered_path, timeline);
  CountingPcmSink audio_sink{audio.get()};
  TimedVideoSink video_sink{video.get()};
  audio->AddSink(&audio_sink);
  webrtc::VideoSourceInterface<webrtc::VideoFrame> *video_interface =
      video.get();
  video_interface->AddOrUpdateSink(&video_sink, webrtc::VideoSinkWants{});

  REQUIRE(audio->start());
  REQUIRE(video->start());
  const auto first_video_deadline = std::chrono::steady_clock::now() + 2s;
  while (video_sink.frame_count() == 0 && audio->error().empty() &&
         video->error().empty() &&
         std::chrono::steady_clock::now() < first_video_deadline) {
    std::this_thread::sleep_for(5ms);
  }
  REQUIRE(video_sink.frame_count() > 0);
  REQUIRE(audio->generated_count() >= 95);
  REQUIRE(audio_sink.first_callback_at().has_value());
  REQUIRE(video_sink.first_callback_at().has_value());
  const auto first_track_offset =
      *video_sink.first_callback_at() - *audio_sink.first_callback_at();
  REQUIRE(first_track_offset >= 850ms);
  REQUIRE(first_track_offset <= 1'250ms);

  const auto skew_deadline = std::chrono::steady_clock::now() + 2s;
  while (video_sink.frame_count() < 30 && audio->error().empty() &&
         video->error().empty() &&
         std::chrono::steady_clock::now() < skew_deadline) {
    std::this_thread::sleep_for(5ms);
  }
  REQUIRE(video_sink.frame_count() >= 30);
  REQUIRE(audio->last_pts_ms().has_value());
  REQUIRE(video->last_pts_ms().has_value());
  require_overlapping_tracks_are_synchronized(audio_sink.timed_samples(),
                                              video_sink.timed_samples());

  audio->stop();
  video->stop();
  audio->RemoveSink(&audio_sink);
  video_interface->RemoveSink(&video_sink);
  REQUIRE(audio->error().empty());
  REQUIRE(video->error().empty());
}

} // namespace

int main(int argc, char **argv) {
  REQUIRE(argc == 7);
  const std::filesystem::path movie_path{argv[1]};
  source_is_live_and_disables_voice_processing(movie_path);
  emits_exact_pcm_chunks(movie_path);
  shared_timeline_controls_audio(movie_path);
  audio_only_movie_is_supported(std::filesystem::path{argv[2]});
  failures_are_sanitized(movie_path.parent_path(),
                         std::filesystem::path{argv[3]});
  stop_interrupts_future_audio(std::filesystem::path{argv[4]});
  shared_timeline_preserves_track_offset(std::filesystem::path{argv[5]});
  shared_timeline_preserves_track_offset(std::filesystem::path{argv[6]});
  return EXIT_SUCCESS;
}
