#pragma once

#include "shareme/core/audio_output_contract.hpp"
#include "shareme/core/movie_audio_clock.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace shareme::core {

struct MovieAudioRendererConfig {
  std::size_t ready_capacity = 8;
  std::size_t in_flight_capacity = 8;
  std::size_t maximum_block_bytes = 38'400;
  AudioOutputFormat output_format{
      .sample_rate = 48'000,
      .channel_count = 2,
      .sample_format = AudioSampleFormat::signed_int16,
      .interleaving = AudioInterleaving::interleaved,
  };
};

using MovieAudioRendererOptions = MovieAudioRendererConfig;

enum class EnqueueStatus {
  accepted,
  invalid_pcm,
  overflow,
  not_accepting,
  renderer_stopped,
};

struct EnqueueResult {
  EnqueueStatus status = EnqueueStatus::renderer_stopped;
  std::uint64_t accepted_frames = 0;
  std::optional<AudioPcmValidationError> validation_error = std::nullopt;
  std::optional<PlaybackCategory> failure_category = std::nullopt;
};

enum class ActivationStatus {
  activated,
  no_candidate,
  failed,
  candidate_stale,
  renderer_stopped,
};

struct ActivationResult {
  ActivationStatus status = ActivationStatus::failed;
  std::optional<PlaybackCategory> failure_category = std::nullopt;
};

enum class QuiesceStatus {
  quiesced,
  unknown_consumption,
  no_output,
  stale_snapshot,
  invalid_snapshot,
  renderer_stopped,
};

struct QuiesceResult {
  QuiesceStatus status = QuiesceStatus::no_output;
  FinalDeviceSnapshot final_snapshot{};
  std::optional<PlaybackCategory> failure_category = std::nullopt;
};

struct MovieAudioRendererSnapshot {
  std::uint64_t media_frames_enqueued_total = 0;
  std::uint64_t backend_frames_written_total = 0;
  std::uint64_t replayed_frames_total = 0;
  std::uint64_t device_consumed_frames_total = 0;
  std::uint64_t logical_consumed_frames = 0;

  // Durations are floored milliseconds derived from frame counts.
  std::uint64_t renderer_queue_duration = 0;
  std::uint64_t device_queue_duration = 0;
  std::int64_t estimated_playout_pts_ms = 0;
  ClockConfidence confidence = ClockConfidence::unavailable;
  ClockConfidence clock_confidence = ClockConfidence::unavailable;

  std::uint64_t playback_generation = 0;
  std::uint64_t host_audio_epoch = 0;
  std::uint64_t renderer_clock_epoch = 0;
  std::uint64_t route_generation = 0;
  std::uint64_t underrun_count = 0;
  std::uint64_t discontinuity_count = 0;
  std::optional<PlaybackCategory> last_discontinuity_reason = std::nullopt;
  std::optional<WriteStatus> last_write_status = std::nullopt;

  std::size_t ready_block_count = 0;
  std::size_t replay_block_count = 0;
  std::size_t in_flight_block_count = 0;
  std::size_t ready_capacity = 0;
  std::size_t in_flight_capacity = 0;
  std::uint64_t ready_frame_count = 0;
  std::uint64_t in_flight_frame_count = 0;
  std::uint64_t released_pcm_block_count = 0;
  std::uint64_t overflow_count = 0;
  std::uint64_t device_queue_frames = 0;
  bool output_active = false;
  bool accepting_callbacks = false;
};

class MovieAudioRenderer final {
 public:
  explicit MovieAudioRenderer(MovieAudioRendererConfig config = {});
  MovieAudioRenderer(std::size_t ready_capacity,
                     std::size_t in_flight_capacity);
  ~MovieAudioRenderer();

  MovieAudioRenderer(const MovieAudioRenderer&) = delete;
  MovieAudioRenderer& operator=(const MovieAudioRenderer&) = delete;
  MovieAudioRenderer(MovieAudioRenderer&&) = delete;
  MovieAudioRenderer& operator=(MovieAudioRenderer&&) = delete;

  [[nodiscard]] EnqueueResult try_enqueue(
      AudioPcmBlockView pcm, std::uint64_t receiver_sequence) noexcept;

  void set_playback_anchor(AudioClockAnchor anchor) noexcept;
  void clear_playback_anchor() noexcept;

  void pump(MonotonicTime monotonic_now);

  [[nodiscard]] ActivationResult activate_output(
      std::unique_ptr<AudioOutputDevice> device);
  [[nodiscard]] QuiesceResult quiesce_output();
  void deactivate_output(PlaybackCategory reason);
  void shutdown() noexcept;

  [[nodiscard]] MovieAudioRendererSnapshot snapshot() const noexcept;
  [[nodiscard]] std::size_t ready_capacity() const noexcept;
  [[nodiscard]] std::size_t in_flight_capacity() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shareme::core
