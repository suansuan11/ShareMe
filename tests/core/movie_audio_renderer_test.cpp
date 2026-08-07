#include "shareme/core/movie_audio_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

using namespace shareme::core;

constexpr AudioOutputFormat kTestFormat{
    .sample_rate = 1'000,
    .channel_count = 2,
    .sample_format = AudioSampleFormat::signed_int16,
    .interleaving = AudioInterleaving::interleaved,
};

MovieAudioRendererConfig test_config(
    std::size_t ready_capacity = 4, std::size_t in_flight_capacity = 4) {
  return MovieAudioRendererConfig{
      .ready_capacity = ready_capacity,
      .in_flight_capacity = in_flight_capacity,
      .maximum_block_bytes = 4'096,
      .output_format = kTestFormat,
  };
}

struct CapturedWrite {
  std::uint64_t frame_count = 0;
  std::vector<std::byte> bytes;
};

class FakeOutputDevice final : public AudioOutputDevice {
 public:
  static inline std::uint64_t total_stop_calls = 0;

  enum class WriteMode {
    positive,
    would_block,
    failed,
  };

  explicit FakeOutputDevice(std::uint64_t instance_id) : instance_id{instance_id} {}

  WriteMode write_mode = WriteMode::positive;
  std::uint64_t maximum_accepted_frames = std::numeric_limits<std::uint64_t>::max();
  std::optional<PlaybackCategory> write_failure =
      PlaybackCategory::audio_output_failure;
  bool open_succeeds = true;
  bool start_succeeds = true;
  bool reset_counters_on_open = false;
  bool clear_writes_on_open = false;
  bool quiescence_confirmed = true;
  bool backend_active_after_unconfirmed_quiesce = false;
  std::optional<bool> snapshot_active_override = std::nullopt;
  bool final_exact_consumption = true;
  std::optional<FinalDeviceSnapshot> final_override = std::nullopt;
  std::optional<AudioDeviceSnapshot> snapshot_override = std::nullopt;
  std::optional<WriteResult> write_override = std::nullopt;
  std::optional<bool> snapshot_active_after_first = std::nullopt;
  std::atomic<bool> block_quiesce{false};
  std::atomic<bool> quiesce_entered{false};
  std::uint64_t consumed_frames = 0;
  std::uint64_t underrun_count = 0;
  std::uint64_t discontinuity_count = 0;
  std::uint64_t instance_id;
  std::uint64_t accepted_frames = 0;
  std::uint64_t next_snapshot_sequence = 1;
  std::uint64_t open_calls = 0;
  std::uint64_t start_calls = 0;
  std::uint64_t pause_calls = 0;
  std::uint64_t stop_calls = 0;
  std::uint64_t snapshot_calls = 0;
  std::uint64_t quiesce_calls = 0;
  bool opened = false;
  bool active = false;
  bool throw_on_write = false;
  std::vector<CapturedWrite> writes;
  std::vector<std::string> lifecycle;
  std::vector<std::string>* shared_lifecycle = nullptr;
  std::function<void()> start_observer;

  OpenResult open(AudioOutputFormat format) override {
    ++open_calls;
    lifecycle.emplace_back("open");
    if (shared_lifecycle != nullptr)
      shared_lifecycle->emplace_back("open");
    if (!open_succeeds || format.sample_rate == 0) {
      opened = false;
      return OpenResult{
          .status = OpenStatus::failed,
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }

    opened = true;
    active = false;
    if (reset_counters_on_open) {
      accepted_frames = 0;
      consumed_frames = 0;
    }
    if (clear_writes_on_open)
      writes.clear();
    return OpenResult{
        .status = OpenStatus::opened,
        .failure_category = std::nullopt,
    };
  }

  bool start() override {
    ++start_calls;
    lifecycle.emplace_back("start");
    if (shared_lifecycle != nullptr)
      shared_lifecycle->emplace_back("start");
    if (!opened || !start_succeeds) {
      active = false;
      return false;
    }
    active = true;
    if (start_observer)
      start_observer();
    return true;
  }

  WriteResult try_write(AudioPcmBlockView pcm) override {
    ++write_calls;
    if (!active) {
      return WriteResult{
          .status = WriteStatus::failed,
          .accepted_frames = 0,
          .failure_category = PlaybackCategory::audio_output_device_lost,
      };
    }
    if (throw_on_write) {
      throw 1;
    }

    writes.push_back(CapturedWrite{
        .frame_count = pcm.frame_count,
        .bytes = std::vector<std::byte>{pcm.payload.bytes.begin(),
                                        pcm.payload.bytes.end()},
    });

    if (write_override.has_value()) {
      return *write_override;
    }

    if (write_mode == WriteMode::would_block) {
      return WriteResult{
          .status = WriteStatus::would_block,
          .accepted_frames = 0,
          .failure_category = std::nullopt,
      };
    }
    if (write_mode == WriteMode::failed) {
      return WriteResult{
          .status = WriteStatus::failed,
          .accepted_frames = 0,
          .failure_category = write_failure,
      };
    }

    const auto accepted =
        std::min(pcm.frame_count, maximum_accepted_frames);
    if (accepted == 0) {
      return WriteResult{
          .status = WriteStatus::would_block,
          .accepted_frames = 0,
          .failure_category = std::nullopt,
      };
    }
    accepted_frames += accepted;
    return WriteResult{
        .status = WriteStatus::accepted,
        .accepted_frames = accepted,
        .failure_category = std::nullopt,
    };
  }

  AudioDeviceSnapshot snapshot() override {
    ++snapshot_calls;
    if (snapshot_override.has_value()) {
      return *snapshot_override;
    }
    return make_snapshot(next_snapshot_sequence++);
  }

  FinalDeviceSnapshot quiesce_and_snapshot() override {
    ++quiesce_calls;
    lifecycle.emplace_back("quiesce");
    if (shared_lifecycle != nullptr)
      shared_lifecycle->emplace_back("quiesce");
    active = quiescence_confirmed
        ? false
        : backend_active_after_unconfirmed_quiesce;
    if (block_quiesce.load(std::memory_order_acquire)) {
      quiesce_entered.store(true, std::memory_order_release);
      while (block_quiesce.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }
    if (final_override.has_value()) {
      return *final_override;
    }

    const auto ordinary = make_snapshot(next_snapshot_sequence++);
    return FinalDeviceSnapshot{
        .device_instance_id = ordinary.device_instance_id,
        .snapshot_sequence = ordinary.snapshot_sequence,
        .accepted_frames_total = ordinary.accepted_frames_total,
        .device_consumed_frames_total = ordinary.device_consumed_frames_total,
        .device_queue_frames = ordinary.device_queue_frames,
        .output_latency_frames = ordinary.output_latency_frames,
        .underrun_count = ordinary.underrun_count,
        .discontinuity_count = ordinary.discontinuity_count,
        .last_discontinuity_reason = ordinary.last_discontinuity_reason,
        .active = false,
        .quiesced = quiescence_confirmed,
        .exact_consumption = final_exact_consumption,
    };
  }

  void pause() override {
    ++pause_calls;
    lifecycle.emplace_back("pause");
    if (shared_lifecycle != nullptr)
      shared_lifecycle->emplace_back("pause");
    active = false;
  }

  void stop() override {
    ++stop_calls;
    ++total_stop_calls;
    lifecycle.emplace_back("stop");
    if (shared_lifecycle != nullptr)
      shared_lifecycle->emplace_back("stop");
    active = false;
  }

  std::uint64_t write_calls = 0;

 private:
  [[nodiscard]] AudioDeviceSnapshot make_snapshot(
      std::uint64_t snapshot_sequence) const {
    auto snapshot_active = snapshot_active_override.value_or(active);
    if (snapshot_calls > 1 && snapshot_active_after_first.has_value()) {
      snapshot_active = *snapshot_active_after_first;
    }
    return AudioDeviceSnapshot{
        .device_instance_id = instance_id,
        .snapshot_sequence = snapshot_sequence,
        .accepted_frames_total = accepted_frames,
        .device_consumed_frames_total = consumed_frames,
        .device_queue_frames = accepted_frames -
            std::min(accepted_frames, consumed_frames),
        .output_latency_frames = std::nullopt,
        .underrun_count = underrun_count,
        .discontinuity_count = discontinuity_count,
        .last_discontinuity_reason = std::nullopt,
        .active = snapshot_active,
    };
  }
};

std::vector<std::byte> pcm_bytes(std::uint64_t frame_count,
                                 std::byte seed = std::byte{0}) {
  std::vector<std::byte> bytes(static_cast<std::size_t>(frame_count * 4));
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = std::byte{
        static_cast<unsigned char>(std::to_integer<unsigned int>(seed) +
                                   static_cast<unsigned int>(index))};
  }
  return bytes;
}

AudioPcmBlockView view_for(std::vector<std::byte>& bytes,
                           std::uint64_t frame_count) {
  return AudioPcmBlockView{
      .receiver_sequence = 0,
      .frame_count = frame_count,
      .sample_rate = kTestFormat.sample_rate,
      .channel_count = kTestFormat.channel_count,
      .sample_format = kTestFormat.sample_format,
      .interleaving = kTestFormat.interleaving,
      .payload = AudioPcmPayloadView{
          .bytes = std::span<const std::byte>{bytes},
          .frame_stride_bytes = 4,
      },
  };
}

EnqueueResult enqueue(MovieAudioRenderer& renderer, std::uint64_t sequence,
                       std::uint64_t frame_count,
                       std::byte seed = std::byte{0}) {
  auto bytes = pcm_bytes(frame_count, seed);
  return renderer.try_enqueue(view_for(bytes, frame_count), sequence);
}

void activates(MovieAudioRenderer& renderer,
               std::unique_ptr<FakeOutputDevice> device) {
  const auto result = renderer.activate_output(std::move(device));
  REQUIRE(result.status == ActivationStatus::activated);
}

void rejects_invalid_pcm_and_bounds_ready_ring() {
  MovieAudioRenderer renderer{test_config(2, 2)};

  auto invalid_bytes = pcm_bytes(10);
  auto invalid = view_for(invalid_bytes, 10);
  invalid.payload.frame_stride_bytes = 8;
  const auto invalid_result = renderer.try_enqueue(invalid, 1);
  REQUIRE(invalid_result.status == EnqueueStatus::invalid_pcm);

  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  REQUIRE(enqueue(renderer, 2, 10).status == EnqueueStatus::accepted);
  const auto overflow = enqueue(renderer, 3, 10);
  REQUIRE(overflow.status == EnqueueStatus::overflow);
  REQUIRE(overflow.failure_category == PlaybackCategory::audio_queue_overflow);

  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_block_count == 2);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(snapshot.ready_frame_count == 20);
  REQUIRE(snapshot.discontinuity_count == 1);
  REQUIRE(snapshot.last_discontinuity_reason ==
          PlaybackCategory::audio_queue_overflow);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
  REQUIRE(snapshot.media_frames_enqueued_total == 20);
}

void distinguishes_would_block_and_failure() {
  MovieAudioRenderer would_block_renderer{test_config()};
  auto would_block_device = std::make_unique<FakeOutputDevice>(11);
  would_block_device->write_mode = FakeOutputDevice::WriteMode::would_block;
  activates(would_block_renderer, std::move(would_block_device));
  REQUIRE(enqueue(would_block_renderer, 1, 10).status ==
          EnqueueStatus::accepted);
  would_block_renderer.pump(MonotonicTime{});
  auto snapshot = would_block_renderer.snapshot();
  REQUIRE(snapshot.ready_frame_count == 10);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(snapshot.backend_frames_written_total == 0);

  MovieAudioRenderer failure_renderer{test_config()};
  auto failure_device = std::make_unique<FakeOutputDevice>(12);
  failure_device->write_mode = FakeOutputDevice::WriteMode::failed;
  activates(failure_renderer, std::move(failure_device));
  REQUIRE(enqueue(failure_renderer, 1, 10).status == EnqueueStatus::accepted);
  failure_renderer.pump(MonotonicTime{});
  snapshot = failure_renderer.snapshot();
  REQUIRE(snapshot.ready_frame_count == 10);
  REQUIRE(snapshot.backend_frames_written_total == 0);
  REQUIRE(snapshot.discontinuity_count == 1);
  REQUIRE(snapshot.last_discontinuity_reason ==
          PlaybackCategory::audio_output_failure);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::degraded);
}

void separates_renderer_and_device_queue_durations() {
  MovieAudioRenderer renderer{test_config(2, 2)};
  auto device = std::make_unique<FakeOutputDevice>(13);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));

  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});

  device_ptr->write_mode = FakeOutputDevice::WriteMode::would_block;
  device_ptr->snapshot_override = AudioDeviceSnapshot{
      .device_instance_id = 13,
      .snapshot_sequence = 9,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 6,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = true,
  };
  REQUIRE(enqueue(renderer, 2, 4).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});

  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_frame_count == 4);
  REQUIRE(snapshot.in_flight_frame_count == 6);
  REQUIRE(snapshot.renderer_queue_duration == 4);
  REQUIRE(snapshot.device_queue_duration == 6);
}

void trims_exact_suffix_and_counts_replay_separately() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(21);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));

  auto source = pcm_bytes(10, std::byte{3});
  const auto source_view = view_for(source, 10);
  REQUIRE(renderer.try_enqueue(source_view, 77).status ==
          EnqueueStatus::accepted);
  source[0] = std::byte{99};
  renderer.pump(MonotonicTime{});
  REQUIRE(old_device_ptr->writes.front().bytes.front() == std::byte{3});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);

  const auto snapshot_calls_before_quiesce = old_device_ptr->snapshot_calls;
  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 21,
      .snapshot_sequence = 7,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 6,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };
  const auto quiesce = renderer.quiesce_output();
  REQUIRE(quiesce.status == QuiesceStatus::quiesced);
  REQUIRE(old_device_ptr->quiesce_calls == 1);
  REQUIRE(old_device_ptr->pause_calls == 0);
  REQUIRE(old_device_ptr->snapshot_calls == snapshot_calls_before_quiesce);
  auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_frame_count == 6);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(snapshot.released_pcm_block_count == 0);

  auto replacement = std::make_unique<FakeOutputDevice>(22);
  auto* replacement_ptr = replacement.get();
  const auto stop_calls_before_activation = FakeOutputDevice::total_stop_calls;
  const auto activation = renderer.activate_output(std::move(replacement));
  REQUIRE(activation.status == ActivationStatus::activated);
  REQUIRE(FakeOutputDevice::total_stop_calls ==
          stop_calls_before_activation + 1);
  renderer.pump(MonotonicTime{});
  REQUIRE(replacement_ptr->writes.size() == 1);
  REQUIRE(replacement_ptr->writes.front().frame_count == 6);
  REQUIRE(replacement_ptr->writes.front().bytes.size() == 24);
  REQUIRE(replacement_ptr->writes.front().bytes.front() == source[16]);

  replacement_ptr->consumed_frames = 6;
  renderer.pump(MonotonicTime{});
  snapshot = renderer.snapshot();
  REQUIRE(snapshot.media_frames_enqueued_total == 10);
  REQUIRE(snapshot.backend_frames_written_total == 16);
  REQUIRE(snapshot.replayed_frames_total == 6);
  REQUIRE(snapshot.device_consumed_frames_total == 6);
  REQUIRE(snapshot.logical_consumed_frames == 10);
  REQUIRE(snapshot.released_pcm_block_count == 1);
  REQUIRE(snapshot.ready_block_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 0);
}

void retains_all_bounded_in_flight_suffixes_across_handoff() {
  MovieAudioRenderer renderer{test_config(1, 3)};
  auto old_device = std::make_unique<FakeOutputDevice>(25);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));

  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    REQUIRE(enqueue(renderer, sequence, 10).status == EnqueueStatus::accepted);
    renderer.pump(MonotonicTime{});
  }
  auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_frame_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 3);
  REQUIRE(snapshot.in_flight_frame_count == 30);

  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 25,
      .snapshot_sequence = 20,
      .accepted_frames_total = 30,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 30,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };
  REQUIRE(renderer.quiesce_output().status == QuiesceStatus::quiesced);
  snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_block_count == 3);
  REQUIRE(snapshot.replay_block_count == 3);

  auto replacement = std::make_unique<FakeOutputDevice>(26);
  auto* replacement_ptr = replacement.get();
  REQUIRE(renderer.activate_output(std::move(replacement)).status ==
          ActivationStatus::activated);
  renderer.pump(MonotonicTime{});
  REQUIRE(replacement_ptr->writes.size() == 3);
  REQUIRE(renderer.snapshot().replayed_frames_total == 30);
  replacement_ptr->consumed_frames = 30;
  renderer.pump(MonotonicTime{});
  snapshot = renderer.snapshot();
  REQUIRE(snapshot.released_pcm_block_count == 3);
  REQUIRE(snapshot.in_flight_block_count == 0);
}

void unknown_consumption_freezes_clock_and_does_not_replay() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(31);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});

  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 31,
      .snapshot_sequence = 7,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 8,
      .device_queue_frames = 2,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = false,
  };
  REQUIRE(renderer.quiesce_output().status ==
          QuiesceStatus::unknown_consumption);
  auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.logical_consumed_frames == 4);
  REQUIRE(snapshot.renderer_clock_epoch == 1);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
  REQUIRE(snapshot.last_discontinuity_reason ==
          PlaybackCategory::route_handoff_unknown_consumption);
  REQUIRE(snapshot.released_pcm_block_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 1);
  REQUIRE(snapshot.replayed_frames_total == 0);

  auto replacement = std::make_unique<FakeOutputDevice>(32);
  auto* replacement_ptr = replacement.get();
  REQUIRE(renderer.activate_output(std::move(replacement)).status ==
          ActivationStatus::activated);
  renderer.pump(MonotonicTime{});
  REQUIRE(replacement_ptr->writes.empty());
  REQUIRE(renderer.snapshot().route_generation == 2);
  REQUIRE(renderer.snapshot().clock_confidence == ClockConfidence::invalid);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
}

void ignores_delayed_snapshots_without_regressing_logical_consumption() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(41);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);

  device_ptr->snapshot_override = AudioDeviceSnapshot{
      .device_instance_id = 41,
      .snapshot_sequence = 2,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 10,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = true,
  };
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);

  device_ptr->snapshot_override.reset();
  device_ptr->consumed_frames = 8;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().logical_consumed_frames == 8);
}

void failed_candidate_retains_old_output_identity() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(51);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));
  const auto route_generation = renderer.snapshot().route_generation;

  auto candidate = std::make_unique<FakeOutputDevice>(52);
  candidate->snapshot_active_override = false;
  const auto result = renderer.activate_output(std::move(candidate));
  REQUIRE(result.status == ActivationStatus::candidate_stale);
  REQUIRE(old_device_ptr->quiesce_calls == 1);
  REQUIRE(old_device_ptr->stop_calls == 0);
  REQUIRE(old_device_ptr->start_calls == 2);
  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.route_generation == route_generation);
  REQUIRE(snapshot.output_active);
}

void failed_candidate_does_not_replay_into_old_output() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(52);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));

  REQUIRE(enqueue(renderer, 1, 10, std::byte{7}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  REQUIRE(old_device_ptr->writes.size() == 1);

  auto candidate = std::make_unique<FakeOutputDevice>(53);
  candidate->snapshot_active_override = false;
  const auto result = renderer.activate_output(std::move(candidate));

  REQUIRE(result.status == ActivationStatus::candidate_stale);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().ready_frame_count == 0);
  renderer.pump(MonotonicTime{});
  REQUIRE(old_device_ptr->writes.size() == 1);
}

void candidate_loss_after_old_quiesce_restores_old_route() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(54);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  const auto route_generation = renderer.snapshot().route_generation;

  auto candidate = std::make_unique<FakeOutputDevice>(55);
  candidate->snapshot_active_after_first = false;
  const auto result = renderer.activate_output(std::move(candidate));

  REQUIRE(result.status == ActivationStatus::candidate_stale);
  REQUIRE(old_device_ptr->quiesce_calls == 1);
  REQUIRE(old_device_ptr->active);
  REQUIRE(old_device_ptr->writes.size() == 1);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().route_generation == route_generation);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 0);
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
  old_device_ptr->consumed_frames = 10;
  renderer.pump(MonotonicTime{});
  REQUIRE(old_device_ptr->writes.size() == 1);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 10);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
}

void handoff_quiesces_old_output_before_starting_candidate() {
  MovieAudioRenderer renderer{test_config()};
  std::vector<std::string> lifecycle;
  auto old_device = std::make_unique<FakeOutputDevice>(541);
  auto* old_device_ptr = old_device.get();
  old_device_ptr->shared_lifecycle = &lifecycle;
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});

  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 541,
      .snapshot_sequence = 5,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 6,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  auto candidate = std::make_unique<FakeOutputDevice>(542);
  candidate->shared_lifecycle = &lifecycle;
  std::optional<std::size_t> ready_frames_at_candidate_start;
  candidate->start_observer = [&] {
    ready_frames_at_candidate_start = renderer.snapshot().ready_frame_count;
  };
  REQUIRE(renderer.activate_output(std::move(candidate)).status ==
          ActivationStatus::activated);

  const auto quiesce = std::find(lifecycle.begin(), lifecycle.end(), "quiesce");
  REQUIRE(quiesce != lifecycle.end());
  const auto candidate_open = std::find(
      std::next(quiesce), lifecycle.end(), "open");
  REQUIRE(candidate_open != lifecycle.end());
  const auto candidate_start = std::find(
      std::next(candidate_open), lifecycle.end(), "start");
  REQUIRE(candidate_start != lifecycle.end());
  REQUIRE(quiesce < candidate_open);
  REQUIRE(candidate_open < candidate_start);
  REQUIRE(ready_frames_at_candidate_start.has_value());
  REQUIRE(*ready_frames_at_candidate_start == 0);
  REQUIRE(renderer.snapshot().ready_frame_count == 6);
}

void unconfirmed_quiescence_blocks_candidate_activation() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(547);
  auto* old_device_ptr = old_device.get();
  old_device_ptr->quiescence_confirmed = false;
  old_device_ptr->backend_active_after_unconfirmed_quiesce = true;
  activates(renderer, std::move(old_device));

  std::vector<std::string> candidate_lifecycle;
  auto candidate = std::make_unique<FakeOutputDevice>(548);
  candidate->shared_lifecycle = &candidate_lifecycle;
  const auto result = renderer.activate_output(std::move(candidate));

  REQUIRE(result.status == ActivationStatus::failed);
  REQUIRE(std::find(candidate_lifecycle.begin(), candidate_lifecycle.end(),
                    "open") == candidate_lifecycle.end());
  REQUIRE(old_device_ptr->quiesce_calls == 1);
  REQUIRE(old_device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
}

void failed_candidate_open_resumes_old_route_without_generation_change() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(543);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  const auto route_generation = renderer.snapshot().route_generation;

  auto candidate = std::make_unique<FakeOutputDevice>(544);
  candidate->open_succeeds = false;
  const auto result = renderer.activate_output(std::move(candidate));

  REQUIRE(result.status == ActivationStatus::failed);
  REQUIRE(old_device_ptr->quiesce_calls == 1);
  REQUIRE(old_device_ptr->start_calls == 2);
  REQUIRE(old_device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().route_generation == route_generation);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
}

void failed_candidate_resumes_old_route_after_unknown_handoff() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(545);
  auto* old_device_ptr = old_device.get();
  old_device_ptr->final_exact_consumption = false;
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  const auto route_generation = renderer.snapshot().route_generation;

  auto candidate = std::make_unique<FakeOutputDevice>(546);
  candidate->open_succeeds = false;
  const auto result = renderer.activate_output(std::move(candidate));

  REQUIRE(result.status == ActivationStatus::failed);
  REQUIRE(old_device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().route_generation == route_generation);
  REQUIRE(renderer.snapshot().renderer_clock_epoch == 1);
  REQUIRE(renderer.snapshot().clock_confidence == ClockConfidence::invalid);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 0);
  REQUIRE(renderer.snapshot().replay_block_count == 0);

  old_device_ptr->consumed_frames = 10;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 10);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
}

void failed_candidate_after_public_unknown_quiesce_preserves_old_ownership() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(549);
  auto* old_device_ptr = old_device.get();
  old_device_ptr->final_exact_consumption = false;
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});

  REQUIRE(renderer.quiesce_output().status ==
          QuiesceStatus::unknown_consumption);

  auto candidate = std::make_unique<FakeOutputDevice>(550);
  candidate->open_succeeds = false;
  REQUIRE(renderer.activate_output(std::move(candidate)).status ==
          ActivationStatus::failed);
  REQUIRE(old_device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);

  old_device_ptr->consumed_frames = 10;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 10);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
}

void public_unknown_quiesce_preserves_last_proven_consumption_baseline() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(551);
  auto* old_device_ptr = old_device.get();
  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 551,
      .snapshot_sequence = 7,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 8,
      .device_queue_frames = 2,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = false,
  };
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  old_device_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);

  REQUIRE(renderer.quiesce_output().status ==
          QuiesceStatus::unknown_consumption);
  old_device_ptr->next_snapshot_sequence = 8;

  auto candidate = std::make_unique<FakeOutputDevice>(552);
  candidate->open_succeeds = false;
  REQUIRE(renderer.activate_output(std::move(candidate)).status ==
          ActivationStatus::failed);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 4);
  REQUIRE(renderer.snapshot().device_consumed_frames_total == 4);
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 0);

  old_device_ptr->consumed_frames = 10;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 10);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
}

void output_loss_releases_in_flight_and_marks_unknown() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(71);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));

  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->snapshot_override = AudioDeviceSnapshot{
      .device_instance_id = 71,
      .snapshot_sequence = 5,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 6,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
  };

  renderer.pump(MonotonicTime{});
  const auto snapshot = renderer.snapshot();
  REQUIRE(!snapshot.output_active);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(snapshot.released_pcm_block_count == 1);
  REQUIRE(snapshot.logical_consumed_frames == 0);
  REQUIRE(snapshot.renderer_clock_epoch == 1);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
}

void stale_quiesce_resumes_the_same_output() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(72);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));

  device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 72,
      .snapshot_sequence = 1,
      .accepted_frames_total = 0,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 0,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  const auto result = renderer.quiesce_output();
  REQUIRE(result.status == QuiesceStatus::stale_snapshot);
  REQUIRE(device_ptr->start_calls == 2);
  REQUIRE(device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
}

void malformed_final_identity_is_rejected_as_invalid() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(721);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));

  device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 0,
      .snapshot_sequence = 2,
      .accepted_frames_total = 0,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 0,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  const auto result = renderer.quiesce_output();
  REQUIRE(result.status == QuiesceStatus::invalid_snapshot);
  REQUIRE(device_ptr->start_calls == 2);
  REQUIRE(device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
}

void rejects_exact_snapshot_with_inconsistent_queue_facts() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(722);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->next_snapshot_sequence = 6;
  device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 722,
      .snapshot_sequence = 5,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 0,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  const auto result = renderer.quiesce_output();
  REQUIRE(result.status == QuiesceStatus::invalid_snapshot);
  REQUIRE(device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().logical_consumed_frames == 0);
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
}

void exact_handoff_recovers_after_unknown_transition() {
  MovieAudioRenderer renderer{test_config()};
  auto old_device = std::make_unique<FakeOutputDevice>(73);
  auto* old_device_ptr = old_device.get();
  activates(renderer, std::move(old_device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});

  old_device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 73,
      .snapshot_sequence = 5,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 10,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = false,
  };
  REQUIRE(renderer.quiesce_output().status ==
          QuiesceStatus::unknown_consumption);

  auto replacement = std::make_unique<FakeOutputDevice>(74);
  auto* replacement_ptr = replacement.get();
  activates(renderer, std::move(replacement));
  REQUIRE(enqueue(renderer, 2, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  replacement_ptr->consumed_frames = 4;
  renderer.pump(MonotonicTime{});
  replacement_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 74,
      .snapshot_sequence = 9,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 4,
      .device_queue_frames = 6,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  const auto result = renderer.quiesce_output();
  REQUIRE(result.status == QuiesceStatus::quiesced);
  REQUIRE(renderer.snapshot().ready_frame_count == 6);
}

void rejects_device_accepted_frames_not_written_by_renderer() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(75);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->snapshot_override = AudioDeviceSnapshot{
      .device_instance_id = 75,
      .snapshot_sequence = 3,
      .accepted_frames_total = 100,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 100,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = true,
  };

  renderer.pump(MonotonicTime{});
  const auto snapshot = renderer.snapshot();
  REQUIRE(!snapshot.output_active);
  REQUIRE(snapshot.renderer_clock_epoch == 1);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
}

void rejects_regressing_device_counters() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(755);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->underrun_count = 1;
  renderer.pump(MonotonicTime{});

  device_ptr->snapshot_override = AudioDeviceSnapshot{
      .device_instance_id = 755,
      .snapshot_sequence = 5,
      .accepted_frames_total = 0,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 0,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = true,
  };
  renderer.pump(MonotonicTime{});
  REQUIRE(!renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().renderer_clock_epoch == 1);
}

void partial_backend_writes_preserve_in_flight_suffix() {
  MovieAudioRenderer renderer{test_config(2, 2)};
  auto device = std::make_unique<FakeOutputDevice>(756);
  auto* device_ptr = device.get();
  device_ptr->maximum_accepted_frames = 4;
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);

  renderer.pump(MonotonicTime{});
  REQUIRE(device_ptr->writes.size() == 3);
  REQUIRE(device_ptr->writes[0].frame_count == 10);
  REQUIRE(device_ptr->writes[1].frame_count == 6);
  REQUIRE(device_ptr->writes[2].frame_count == 2);
  REQUIRE(renderer.snapshot().backend_frames_written_total == 10);

  device_ptr->consumed_frames = 5;
  renderer.pump(MonotonicTime{});
  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.logical_consumed_frames == 5);
  REQUIRE(snapshot.in_flight_frame_count == 5);
}

void pure_underrun_degrades_clock_confidence() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(76);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->underrun_count = 1;

  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().clock_confidence == ClockConfidence::degraded);
}

void invalid_deactivation_marks_unknown_consumption() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(77);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 77,
      .snapshot_sequence = 1,
      .accepted_frames_total = 10,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 10,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = false,
      .quiesced = true,
      .exact_consumption = true,
  };

  renderer.deactivate_output(PlaybackCategory::route_no_active_output);
  const auto snapshot = renderer.snapshot();
  REQUIRE(!snapshot.output_active);
  REQUIRE(snapshot.released_pcm_block_count == 1);
  REQUIRE(snapshot.renderer_clock_epoch == 1);
  REQUIRE(snapshot.logical_consumed_frames == 0);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
}

void clear_anchor_removes_clock_estimate() {
  MovieAudioRenderer renderer{test_config()};
  renderer.set_playback_anchor(AudioClockAnchor{
      .control_sequence = 1,
      .host_source_sequence = 1,
      .playback_generation = 0,
      .audio_epoch = 0,
      .media_pts_ms = 123,
      .consumed_frames = 0,
      .sample_rate = kTestFormat.sample_rate,
      .channel_count = kTestFormat.channel_count,
  });
  activates(renderer, std::make_unique<FakeOutputDevice>(78));
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().estimated_playout_pts_ms == 123);

  renderer.clear_playback_anchor();
  renderer.pump(MonotonicTime{} + std::chrono::milliseconds{1});
  REQUIRE(renderer.snapshot().estimated_playout_pts_ms == 0);
}

void accepted_scope_change_restarts_output_and_discards_stale_pcm() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(785);
  auto* device_ptr = device.get();
  device_ptr->reset_counters_on_open = true;
  device_ptr->clear_writes_on_open = true;
  activates(renderer, std::move(device));
  const auto route_generation = renderer.snapshot().route_generation;

  REQUIRE(enqueue(renderer, 1, 10, std::byte{1}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->write_mode = FakeOutputDevice::WriteMode::would_block;
  REQUIRE(enqueue(renderer, 2, 10, std::byte{2}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
  REQUIRE(renderer.snapshot().ready_block_count == 1);

  const auto lifecycle_start = device_ptr->lifecycle.size();
  renderer.set_playback_anchor(AudioClockAnchor{
      .control_sequence = 2,
      .host_source_sequence = 2,
      .playback_generation = 1,
      .audio_epoch = 1,
      .media_pts_ms = 500,
      .consumed_frames = renderer.snapshot().logical_consumed_frames,
      .sample_rate = kTestFormat.sample_rate,
      .channel_count = kTestFormat.channel_count,
  });

  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.released_pcm_block_count == 2);
  REQUIRE(snapshot.ready_block_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(snapshot.playback_generation == 1);
  REQUIRE(snapshot.host_audio_epoch == 1);
  REQUIRE(snapshot.route_generation == route_generation);
  REQUIRE(device_ptr->lifecycle.size() == lifecycle_start + 3);
  REQUIRE(device_ptr->lifecycle[lifecycle_start] == "stop");
  REQUIRE(device_ptr->lifecycle[lifecycle_start + 1] == "open");
  REQUIRE(device_ptr->lifecycle[lifecycle_start + 2] == "start");

  device_ptr->write_mode = FakeOutputDevice::WriteMode::positive;
  REQUIRE(enqueue(renderer, 3, 10, std::byte{99}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  REQUIRE(device_ptr->writes.size() == 1);
  REQUIRE(device_ptr->writes.front().bytes.front() == std::byte{99});
}

void pause_and_resume_preserve_queued_pcm_without_route_change() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(786);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  const auto route_generation = renderer.snapshot().route_generation;
  device_ptr->write_mode = FakeOutputDevice::WriteMode::would_block;
  REQUIRE(enqueue(renderer, 1, 10, std::byte{7}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  const auto writes_before_pause = device_ptr->writes.size();

  renderer.pause_output();
  REQUIRE(device_ptr->pause_calls == 1);
  REQUIRE(!renderer.snapshot().output_active);
  renderer.pump(MonotonicTime{});
  REQUIRE(device_ptr->writes.size() == writes_before_pause);

  device_ptr->write_mode = FakeOutputDevice::WriteMode::positive;
  renderer.resume_output();
  REQUIRE(device_ptr->start_calls == 2);
  REQUIRE(renderer.snapshot().route_generation == route_generation);
  REQUIRE(renderer.snapshot().output_active);
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().ready_block_count == 0);
  REQUIRE(renderer.snapshot().in_flight_block_count == 1);
}

void failed_scope_restart_invalidates_local_output_after_releasing_pcm() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(787);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  device_ptr->open_succeeds = false;

  renderer.set_playback_anchor(AudioClockAnchor{
      .control_sequence = 2,
      .host_source_sequence = 2,
      .playback_generation = 1,
      .audio_epoch = 1,
      .media_pts_ms = 500,
      .consumed_frames = renderer.snapshot().logical_consumed_frames,
      .sample_rate = kTestFormat.sample_rate,
      .channel_count = kTestFormat.channel_count,
  });

  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.released_pcm_block_count == 1);
  REQUIRE(snapshot.ready_block_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(!snapshot.output_active);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::invalid);
  REQUIRE(snapshot.route_generation == 1);
}

void scope_change_while_paused_reopens_without_resuming_playback() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(788);
  auto* device_ptr = device.get();
  device_ptr->reset_counters_on_open = true;
  device_ptr->clear_writes_on_open = true;
  activates(renderer, std::move(device));
  device_ptr->write_mode = FakeOutputDevice::WriteMode::would_block;
  REQUIRE(enqueue(renderer, 1, 10, std::byte{4}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  renderer.pause_output();
  const auto route_generation = renderer.snapshot().route_generation;
  const auto lifecycle_start = device_ptr->lifecycle.size();

  renderer.set_playback_anchor(AudioClockAnchor{
      .control_sequence = 2,
      .host_source_sequence = 2,
      .playback_generation = 1,
      .audio_epoch = 1,
      .media_pts_ms = 500,
      .consumed_frames = renderer.snapshot().logical_consumed_frames,
      .sample_rate = kTestFormat.sample_rate,
      .channel_count = kTestFormat.channel_count,
  });

  REQUIRE(renderer.snapshot().released_pcm_block_count == 1);
  REQUIRE(!renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().route_generation == route_generation);
  REQUIRE(device_ptr->lifecycle.size() == lifecycle_start + 4);
  REQUIRE(device_ptr->lifecycle[lifecycle_start] == "stop");
  REQUIRE(device_ptr->lifecycle[lifecycle_start + 1] == "open");
  REQUIRE(device_ptr->lifecycle[lifecycle_start + 2] == "start");
  REQUIRE(device_ptr->lifecycle[lifecycle_start + 3] == "pause");

  device_ptr->write_mode = FakeOutputDevice::WriteMode::positive;
  REQUIRE(enqueue(renderer, 2, 10, std::byte{88}).status ==
          EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  REQUIRE(device_ptr->writes.empty());
  renderer.resume_output();
  renderer.pump(MonotonicTime{});
  REQUIRE(device_ptr->writes.size() == 1);
  REQUIRE(device_ptr->writes.front().bytes.front() == std::byte{88});
}

void rejects_untagged_candidate_snapshots() {
  MovieAudioRenderer renderer{test_config()};
  auto candidate = std::make_unique<FakeOutputDevice>(0);
  const auto result = renderer.activate_output(std::move(candidate));
  REQUIRE(result.status == ActivationStatus::candidate_stale);
  REQUIRE(renderer.snapshot().route_generation == 0);
}

void invalid_write_result_is_fail_closed() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(79);
  auto* device_ptr = device.get();
  device_ptr->write_override = WriteResult{
      .status = WriteStatus::accepted,
      .accepted_frames = 11,
      .failure_category = std::nullopt,
  };
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().last_write_status == WriteStatus::accepted);
  REQUIRE(renderer.snapshot().discontinuity_count == 1);
  REQUIRE(renderer.snapshot().ready_frame_count == 10);
  REQUIRE(!renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().renderer_clock_epoch == 1);
}

void device_loss_write_rebuilds_unknown_ownership() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(80);
  auto* device_ptr = device.get();
  device_ptr->write_override = WriteResult{
      .status = WriteStatus::failed,
      .accepted_frames = 0,
      .failure_category = PlaybackCategory::audio_output_device_lost,
  };
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  const auto snapshot = renderer.snapshot();
  REQUIRE(!snapshot.output_active);
  REQUIRE(snapshot.replay_block_count == 1);
  REQUIRE(snapshot.renderer_clock_epoch == 1);
}

void zero_frame_write_result_rebuilds_unknown_ownership() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(800);
  auto* device_ptr = device.get();
  device_ptr->write_override = WriteResult{
      .status = WriteStatus::accepted,
      .accepted_frames = 0,
      .failure_category = std::nullopt,
  };
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  renderer.pump(MonotonicTime{});
  REQUIRE(!renderer.snapshot().output_active);
  REQUIRE(renderer.snapshot().renderer_clock_epoch == 1);
}

void malformed_write_protocols_rebuild_unknown_ownership() {
  const auto assert_unknown = [](std::optional<WriteResult> override_result,
                                 bool throw_on_write, std::uint64_t instance) {
    MovieAudioRenderer renderer{test_config()};
    auto device = std::make_unique<FakeOutputDevice>(instance);
    device->write_override = std::move(override_result);
    device->throw_on_write = throw_on_write;
    activates(renderer, std::move(device));
    REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
    renderer.pump(MonotonicTime{});
    REQUIRE(!renderer.snapshot().output_active);
    REQUIRE(renderer.snapshot().renderer_clock_epoch == 1);
  };

  assert_unknown(
      WriteResult{
          .status = WriteStatus::failed,
          .accepted_frames = 0,
          .failure_category = std::nullopt,
      },
      false, 801);
  assert_unknown(
      WriteResult{
          .status = WriteStatus::would_block,
          .accepted_frames = 1,
          .failure_category = std::nullopt,
      },
      false, 802);
  assert_unknown(std::nullopt, true, 803);
}

void invalid_final_lifecycle_state_resumes_old_output() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(804);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->final_override = FinalDeviceSnapshot{
      .device_instance_id = 804,
      .snapshot_sequence = 3,
      .accepted_frames_total = 0,
      .device_consumed_frames_total = 0,
      .device_queue_frames = 0,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .last_discontinuity_reason = std::nullopt,
      .active = true,
      .quiesced = false,
      .exact_consumption = true,
  };

  const auto result = renderer.quiesce_output();
  REQUIRE(result.status == QuiesceStatus::invalid_snapshot);
  REQUIRE(device_ptr->active);
  REQUIRE(renderer.snapshot().output_active);
}

void confidence_override_recovers_after_valid_observation() {
  MovieAudioRenderer renderer{test_config(2, 2)};
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  REQUIRE(enqueue(renderer, 2, 10).status == EnqueueStatus::accepted);
  REQUIRE(enqueue(renderer, 3, 10).status == EnqueueStatus::overflow);

  activates(renderer, std::make_unique<FakeOutputDevice>(81));
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().clock_confidence != ClockConfidence::invalid);
}

void backend_discontinuity_delta_is_counted() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(82);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->discontinuity_count = 3;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().discontinuity_count == 3);
  device_ptr->discontinuity_count = 5;
  renderer.pump(MonotonicTime{});
  REQUIRE(renderer.snapshot().discontinuity_count == 5);
}

void shutdown_waits_for_prior_shutdown_completion() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(83);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  device_ptr->block_quiesce.store(true, std::memory_order_release);

  std::thread first_shutdown([&] { renderer.shutdown(); });
  while (!device_ptr->quiesce_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::atomic<bool> second_finished{false};
  std::atomic<bool> second_started{false};
  std::thread second_shutdown([&] {
    second_started.store(true, std::memory_order_release);
    renderer.shutdown();
    second_finished.store(true, std::memory_order_release);
  });
  while (!second_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  for (int attempt = 0; attempt < 10'000; ++attempt) {
    std::this_thread::yield();
  }
  REQUIRE(!second_finished.load(std::memory_order_acquire));

  device_ptr->block_quiesce.store(false, std::memory_order_release);
  first_shutdown.join();
  second_shutdown.join();
  REQUIRE(second_finished.load(std::memory_order_acquire));
}

void concurrent_shutdown_leaves_no_post_close_blocks() {
  MovieAudioRenderer renderer{test_config(8, 8)};
  auto bytes = pcm_bytes(10);
  const auto view = view_for(bytes, 10);
  std::atomic<bool> stop_callback{false};
  std::thread callback([&] {
    std::uint64_t sequence = 1;
    while (!stop_callback.load(std::memory_order_relaxed)) {
      const auto result = renderer.try_enqueue(view, sequence++);
      (void)result;
    }
  });

  std::this_thread::yield();
  renderer.shutdown();
  stop_callback.store(true, std::memory_order_relaxed);
  callback.join();

  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.ready_block_count == 0);
  REQUIRE(snapshot.replay_block_count == 0);
  REQUIRE(snapshot.in_flight_block_count == 0);
  REQUIRE(!snapshot.accepting_callbacks);
}

void underrun_facts_degrade_renderer_confidence() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(61);
  auto* device_ptr = device.get();
  activates(renderer, std::move(device));
  REQUIRE(enqueue(renderer, 1, 10).status == EnqueueStatus::accepted);
  device_ptr->underrun_count = 1;
  device_ptr->discontinuity_count = 1;
  renderer.pump(MonotonicTime{});
  const auto snapshot = renderer.snapshot();
  REQUIRE(snapshot.underrun_count == 1);
  REQUIRE(snapshot.discontinuity_count == 1);
  REQUIRE(snapshot.clock_confidence == ClockConfidence::degraded);
}

void teardown_ingress_is_bounded_and_rejects_without_copying() {
  MovieAudioRenderer renderer{test_config()};
  renderer.shutdown();

  auto bytes = pcm_bytes(10);
  const auto view = view_for(bytes, 10);
  const auto start = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < 10'000; ++iteration) {
    const auto result = renderer.try_enqueue(view, 100 + iteration);
    REQUIRE(result.status == EnqueueStatus::renderer_stopped);
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  REQUIRE(elapsed < std::chrono::milliseconds{250});
  REQUIRE(renderer.snapshot().media_frames_enqueued_total == 0);
}

void explicitly_closed_ingress_stays_closed_through_quiesce() {
  MovieAudioRenderer renderer{test_config()};
  auto device = std::make_unique<FakeOutputDevice>(84);
  activates(renderer, std::move(device));

  renderer.close_ingress();
  auto bytes = pcm_bytes(10);
  const auto result = renderer.try_enqueue(view_for(bytes, 10), 1);
  REQUIRE(result.status == EnqueueStatus::not_accepting);
  static_cast<void>(renderer.quiesce_output());
  REQUIRE(!renderer.snapshot().accepting_callbacks);
  renderer.shutdown();
  REQUIRE(renderer.snapshot().ready_block_count == 0);
  REQUIRE(renderer.snapshot().in_flight_block_count == 0);
}

}  // namespace

int main() {
  rejects_invalid_pcm_and_bounds_ready_ring();
  distinguishes_would_block_and_failure();
  separates_renderer_and_device_queue_durations();
  trims_exact_suffix_and_counts_replay_separately();
  retains_all_bounded_in_flight_suffixes_across_handoff();
  unknown_consumption_freezes_clock_and_does_not_replay();
  ignores_delayed_snapshots_without_regressing_logical_consumption();
  failed_candidate_retains_old_output_identity();
  failed_candidate_does_not_replay_into_old_output();
  candidate_loss_after_old_quiesce_restores_old_route();
  handoff_quiesces_old_output_before_starting_candidate();
  unconfirmed_quiescence_blocks_candidate_activation();
  failed_candidate_open_resumes_old_route_without_generation_change();
  failed_candidate_resumes_old_route_after_unknown_handoff();
  failed_candidate_after_public_unknown_quiesce_preserves_old_ownership();
  public_unknown_quiesce_preserves_last_proven_consumption_baseline();
  output_loss_releases_in_flight_and_marks_unknown();
  stale_quiesce_resumes_the_same_output();
  malformed_final_identity_is_rejected_as_invalid();
  rejects_exact_snapshot_with_inconsistent_queue_facts();
  exact_handoff_recovers_after_unknown_transition();
  rejects_device_accepted_frames_not_written_by_renderer();
  rejects_regressing_device_counters();
  partial_backend_writes_preserve_in_flight_suffix();
  pure_underrun_degrades_clock_confidence();
  invalid_deactivation_marks_unknown_consumption();
  clear_anchor_removes_clock_estimate();
  accepted_scope_change_restarts_output_and_discards_stale_pcm();
  pause_and_resume_preserve_queued_pcm_without_route_change();
  failed_scope_restart_invalidates_local_output_after_releasing_pcm();
  scope_change_while_paused_reopens_without_resuming_playback();
  rejects_untagged_candidate_snapshots();
  invalid_write_result_is_fail_closed();
  device_loss_write_rebuilds_unknown_ownership();
  zero_frame_write_result_rebuilds_unknown_ownership();
  malformed_write_protocols_rebuild_unknown_ownership();
  invalid_final_lifecycle_state_resumes_old_output();
  confidence_override_recovers_after_valid_observation();
  backend_discontinuity_delta_is_counted();
  shutdown_waits_for_prior_shutdown_completion();
  underrun_facts_degrade_renderer_confidence();
  teardown_ingress_is_bounded_and_rejects_without_copying();
  explicitly_closed_ingress_stays_closed_through_quiesce();
  concurrent_shutdown_leaves_no_post_close_blocks();
  return EXIT_SUCCESS;
}
