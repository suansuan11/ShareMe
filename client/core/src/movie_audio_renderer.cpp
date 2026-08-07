#include "shareme/core/movie_audio_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace shareme::core {
namespace {

enum class SlotState : std::uint8_t {
  free,
  filling,
  ready,
  in_flight,
};

struct OwnedPcmBlock {
  std::vector<std::byte> bytes;
  AudioPcmBlockView metadata{};
  std::uint64_t accepted_frames = 0;
  std::uint64_t consumed_frames = 0;
  std::uint64_t replay_pending_frames = 0;
  std::uint64_t enqueue_order = 0;
  std::atomic<SlotState> state{SlotState::free};
};

[[nodiscard]] std::size_t bytes_per_sample(
    AudioSampleFormat format) noexcept {
  switch (format) {
    case AudioSampleFormat::signed_int16:
      return sizeof(std::int16_t);
    case AudioSampleFormat::float32:
      return sizeof(float);
  }
  return 0;
}

[[nodiscard]] bool checked_add(
    std::uint64_t left, std::uint64_t right,
    std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool saturating_add(
    std::uint64_t& value, std::uint64_t amount) noexcept {
  if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
    value = std::numeric_limits<std::uint64_t>::max();
    return false;
  }
  value += amount;
  return true;
}

void saturating_atomic_add(
    std::atomic<std::uint64_t>& value, std::uint64_t amount) noexcept {
  auto current = value.load(std::memory_order_relaxed);
  for (;;) {
    const auto next = amount > std::numeric_limits<std::uint64_t>::max() -
            current
        ? std::numeric_limits<std::uint64_t>::max()
        : current + amount;
    if (value.compare_exchange_weak(current, next,
                                     std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
      return;
    }
  }
}

[[nodiscard]] bool is_same_format(
    AudioPcmBlockView pcm, AudioOutputFormat format) noexcept {
  return pcm.sample_rate == format.sample_rate &&
      pcm.channel_count == format.channel_count &&
      pcm.sample_format == format.sample_format &&
      pcm.interleaving == format.interleaving;
}

[[nodiscard]] std::optional<std::size_t> expected_frame_stride(
    AudioOutputFormat format) noexcept {
  const auto scalar_bytes = bytes_per_sample(format.sample_format);
  if (scalar_bytes == 0 || format.channel_count == 0 ||
      static_cast<std::size_t>(format.channel_count) >
          std::numeric_limits<std::size_t>::max() / scalar_bytes) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(format.channel_count) * scalar_bytes;
}

[[nodiscard]] bool is_valid_renderer_pcm(
    AudioPcmBlockView pcm, const MovieAudioRendererConfig& config,
    AudioPcmValidationError& validation_error,
    bool& format_mismatch) noexcept {
  validation_error = validate_audio_pcm_block(pcm);
  format_mismatch = false;
  if (validation_error != AudioPcmValidationError::none) {
    return false;
  }
  if (pcm.frame_count == 0 || pcm.payload.bytes.empty()) {
    validation_error = AudioPcmValidationError::payload_shape_invalid;
    return false;
  }
  if (!is_same_format(pcm, config.output_format)) {
    format_mismatch = true;
    return false;
  }
  const auto stride = expected_frame_stride(config.output_format);
  if (!stride.has_value() ||
      pcm.payload.frame_stride_bytes != *stride ||
      pcm.payload.bytes.size_bytes() > config.maximum_block_bytes) {
    validation_error = AudioPcmValidationError::payload_shape_invalid;
    return false;
  }
  return true;
}

[[nodiscard]] AudioDeviceSnapshot as_ordinary_snapshot(
    FinalDeviceSnapshot final_snapshot) noexcept {
  return AudioDeviceSnapshot{
      .device_instance_id = final_snapshot.device_instance_id,
      .snapshot_sequence = final_snapshot.snapshot_sequence,
      .accepted_frames_total = final_snapshot.accepted_frames_total,
      .device_consumed_frames_total =
          final_snapshot.device_consumed_frames_total,
      .device_queue_frames = final_snapshot.device_queue_frames,
      .output_latency_frames = final_snapshot.output_latency_frames,
      .underrun_count = final_snapshot.underrun_count,
      .discontinuity_count = final_snapshot.discontinuity_count,
      .last_discontinuity_reason = final_snapshot.last_discontinuity_reason,
      .active = final_snapshot.active,
  };
}

}  // namespace

struct MovieAudioRenderer::Impl {
  static constexpr std::uint64_t kCallbackIngressClosed =
      std::uint64_t{1} << 63;
  static constexpr std::uint64_t kCallbackCountMask =
      ~kCallbackIngressClosed;

  enum class CallbackAdmission {
    admitted,
    not_accepting,
    renderer_stopped,
  };

  struct CallbackScope {
    Impl* owner = nullptr;

    ~CallbackScope() {
      if (owner != nullptr) {
        owner->leave_callback();
      }
    }
  };

  struct IngressBarrier {
    Impl& owner;
    bool reopen = true;

    explicit IngressBarrier(Impl& owner) : owner{owner} {
      owner.close_callback_ingress();
    }

    ~IngressBarrier() {
      if (reopen) {
        owner.reopen_callback_ingress();
      }
    }

    void keep_closed() noexcept {
      reopen = false;
    }
  };

  explicit Impl(MovieAudioRendererConfig config) : config_{config} {
    if (config_.ready_capacity == 0 || config_.in_flight_capacity == 0 ||
        config_.maximum_block_bytes == 0 ||
        !is_valid_audio_output_format(config_.output_format)) {
      throw std::invalid_argument{"invalid movie audio renderer config"};
    }

    const auto total_capacity = config_.ready_capacity +
        config_.in_flight_capacity;
    if (total_capacity < config_.ready_capacity || total_capacity == 0) {
      throw std::invalid_argument{"movie audio renderer capacity overflow"};
    }
    if (!expected_frame_stride(config_.output_format).has_value()) {
      throw std::invalid_argument{"invalid movie audio renderer frame stride"};
    }

    slots_ = std::make_unique<OwnedPcmBlock[]>(total_capacity);
    for (std::size_t index = 0; index < total_capacity; ++index) {
      slots_[index].bytes.resize(config_.maximum_block_bytes);
    }
    ready_indices_.resize(config_.ready_capacity);
    in_flight_indices_.resize(config_.in_flight_capacity);
    replay_indices_.resize(total_capacity);
    rebuild_order_.resize(total_capacity);
  }

  [[nodiscard]] EnqueueResult try_enqueue(
      AudioPcmBlockView pcm, std::uint64_t receiver_sequence) noexcept {
    const auto admission = enter_callback();
    if (admission == CallbackAdmission::renderer_stopped) {
      return EnqueueResult{
          .status = EnqueueStatus::renderer_stopped,
          .accepted_frames = 0,
          .validation_error = std::nullopt,
          .failure_category = std::nullopt,
      };
    }
    if (admission == CallbackAdmission::not_accepting) {
      return EnqueueResult{
          .status = EnqueueStatus::not_accepting,
          .accepted_frames = 0,
          .validation_error = std::nullopt,
          .failure_category = std::nullopt,
      };
    }
    const CallbackScope callback_scope{this};

    AudioPcmValidationError validation_error =
        AudioPcmValidationError::none;
    bool format_mismatch = false;
    if (!is_valid_renderer_pcm(pcm, config_, validation_error,
                               format_mismatch)) {
      return EnqueueResult{
          .status = EnqueueStatus::invalid_pcm,
          .accepted_frames = 0,
          .validation_error = validation_error ==
                  AudioPcmValidationError::none
              ? std::nullopt
              : std::optional<AudioPcmValidationError>{validation_error},
          .failure_category = format_mismatch
              ? std::optional<PlaybackCategory>{
                    PlaybackCategory::audio_format_change}
              : std::nullopt,
      };
    }

    const auto head = ready_head_.load(std::memory_order_relaxed);
    const auto tail = ready_tail_.load(std::memory_order_acquire);
    if (head - tail >= config_.ready_capacity) {
      return overflow_result();
    }
    if (next_enqueue_order_ == std::numeric_limits<std::uint64_t>::max()) {
      return overflow_result();
    }

    const auto slot_index = claim_free_slot();
    if (!slot_index.has_value()) {
      return overflow_result();
    }

    auto& block = slots_[*slot_index];
    block.metadata = pcm;
    block.metadata.receiver_sequence = receiver_sequence;
    block.metadata.payload.bytes = {};
    block.accepted_frames = 0;
    block.consumed_frames = 0;
    block.replay_pending_frames = 0;
    block.enqueue_order = next_enqueue_order_;
    ++next_enqueue_order_;
    std::memcpy(block.bytes.data(), pcm.payload.bytes.data(),
                pcm.payload.bytes.size_bytes());

    ready_indices_[static_cast<std::size_t>(head % config_.ready_capacity)] =
        *slot_index;
    block.state.store(SlotState::ready, std::memory_order_release);
    ready_head_.store(head + 1, std::memory_order_release);
    saturating_atomic_add(media_frames_enqueued_total_, pcm.frame_count);
    return EnqueueResult{
        .status = EnqueueStatus::accepted,
        .accepted_frames = pcm.frame_count,
        .validation_error = std::nullopt,
        .failure_category = std::nullopt,
    };
  }

  void set_playback_anchor(AudioClockAnchor anchor) noexcept {
    if (anchor.sample_rate != config_.output_format.sample_rate ||
        anchor.channel_count != config_.output_format.channel_count ||
        anchor.sample_rate == 0 || anchor.channel_count == 0) {
      record_discontinuity(PlaybackCategory::audio_format_change);
      return;
    }
    if (anchor.playback_generation < playback_generation_ ||
        anchor.audio_epoch < host_audio_epoch_ ||
        anchor.consumed_frames > logical_consumed_frames_) {
      record_discontinuity(PlaybackCategory::audio_correlation_unavailable);
      return;
    }
    const auto media_scope_changed =
        anchor.playback_generation > playback_generation_ ||
        anchor.audio_epoch > host_audio_epoch_;
    playback_generation_ = anchor.playback_generation;
    host_audio_epoch_ = anchor.audio_epoch;
    anchor_ = anchor;
    if (!media_scope_changed) {
      return;
    }

    const auto was_output_active = output_active_;
    const auto was_output_paused = output_paused_;
    IngressBarrier ingress{*this};
    discard_pending_pcm();
    if (output_ == nullptr || (!was_output_active && !was_output_paused)) {
      return;
    }
    const auto fail_scope_restart = [this]() noexcept {
      if (output_ != nullptr) {
        try {
          output_->stop();
        } catch (...) {
        }
      }
      output_active_ = false;
      output_quiesced_ = false;
      output_paused_ = false;
      last_write_status_ = WriteStatus::failed;
      mark_unknown_consumption(PlaybackCategory::audio_output_failure);
    };

    bool stopped = false;
    try {
      output_->stop();
      stopped = true;
    } catch (...) {
    }
    output_active_ = false;
    output_quiesced_ = false;
    output_paused_ = false;
    if (!stopped) {
      fail_scope_restart();
      return;
    }

    OpenResult open_result;
    try {
      open_result = output_->open(config_.output_format);
    } catch (...) {
      open_result = OpenResult{
          .status = OpenStatus::failed,
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }
    if (!is_valid_open_result(open_result) ||
        open_result.status != OpenStatus::opened) {
      fail_scope_restart();
      return;
    }

    bool started = false;
    try {
      started = output_->start();
    } catch (...) {
    }
    if (!started) {
      fail_scope_restart();
      return;
    }

    AudioDeviceSnapshot fresh_snapshot;
    try {
      fresh_snapshot = output_->snapshot();
    } catch (...) {
      fresh_snapshot.active = false;
    }
    if (!is_valid_candidate_snapshot(fresh_snapshot)) {
      fail_scope_restart();
      return;
    }

    set_device_baseline(fresh_snapshot, true);
    output_quiesced_ = false;
    last_write_status_.reset();
    if (was_output_active) {
      output_active_ = true;
      return;
    }

    try {
      output_->pause();
    } catch (...) {
      output_active_ = false;
      last_write_status_ = WriteStatus::failed;
      mark_unknown_consumption(PlaybackCategory::audio_output_failure);
      return;
    }
    output_active_ = false;
    output_paused_ = true;
  }

  void clear_playback_anchor() noexcept {
    anchor_.reset();
    clock_.clear_playback_anchor();
  }

  void pause_output() noexcept {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    IngressBarrier ingress{*this};
    if (output_ == nullptr || !output_active_) {
      return;
    }
    try {
      output_->pause();
    } catch (...) {
      output_active_ = false;
      last_write_status_ = WriteStatus::failed;
      mark_unknown_consumption(PlaybackCategory::audio_output_failure);
      return;
    }
    output_active_ = false;
    output_quiesced_ = false;
    output_paused_ = true;
  }

  void resume_output() noexcept {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    IngressBarrier ingress{*this};
    if (output_ == nullptr || output_active_ || !output_paused_) {
      return;
    }
    if (!resume_old_output(device_instance_id_)) {
      output_paused_ = false;
      last_write_status_ = WriteStatus::failed;
      mark_unknown_consumption(PlaybackCategory::audio_output_failure);
    }
  }

  void pump(MonotonicTime monotonic_now) {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    last_now_ = monotonic_now;
    if (!output_active_ || output_ == nullptr) {
      return;
    }

    observe_output_snapshot(monotonic_now);
    if (!output_active_ || output_ == nullptr) {
      return;
    }

    const auto maximum_attempts = config_.ready_capacity +
        config_.in_flight_capacity + 1;
    std::size_t attempts = 0;
    while (!pending_ready_empty() && attempts < maximum_attempts &&
           output_active_) {
      const bool from_replay = replay_count_ != 0;
      const auto slot_index = pending_front_index(from_replay);
      auto& block = slots_[slot_index];
      const auto remaining_frames = block.metadata.frame_count -
          block.accepted_frames;
      if (remaining_frames == 0) {
        advance_pending_front(from_replay);
        continue;
      }
      if (block.accepted_frames == block.consumed_frames &&
          in_flight_count_ == config_.in_flight_capacity) {
        break;
      }

      const auto write_view = make_write_view(block, remaining_frames);
      WriteResult result;
      bool write_threw = false;
      try {
        result = output_->try_write(write_view);
      } catch (...) {
        write_threw = true;
        result = WriteResult{
            .status = WriteStatus::failed,
            .accepted_frames = 0,
            .failure_category = PlaybackCategory::audio_output_failure,
        };
      }
      last_write_status_ = result.status;
      if (write_threw || !is_valid_write_result(result) ||
          (result.status == WriteStatus::accepted &&
           result.accepted_frames > remaining_frames)) {
        handle_unknown_output(PlaybackCategory::audio_output_failure);
        break;
      }
      if (result.status == WriteStatus::would_block) {
        break;
      }
      if (result.status == WriteStatus::failed) {
        if (result.failure_category ==
            PlaybackCategory::audio_output_device_lost) {
          handle_unknown_output(PlaybackCategory::audio_output_device_lost);
          break;
        }
        record_discontinuity(result.failure_category.value_or(
            PlaybackCategory::audio_output_failure));
        break;
      }

      if (block.accepted_frames == block.consumed_frames) {
        append_in_flight(slot_index);
      }
      block.accepted_frames += result.accepted_frames;
      (void)saturating_add(backend_frames_written_total_,
                           result.accepted_frames);
      const auto replayed = std::min(
          result.accepted_frames, block.replay_pending_frames);
      block.replay_pending_frames -= replayed;
      (void)saturating_add(replayed_frames_total_, replayed);
      if (block.accepted_frames == block.metadata.frame_count) {
        advance_pending_front(from_replay);
      }
      ++attempts;
    }

    if (output_active_ && output_ != nullptr) {
      observe_output_snapshot(monotonic_now);
    }
  }

  [[nodiscard]] ActivationResult activate_output(
      std::unique_ptr<AudioOutputDevice> candidate) {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return ActivationResult{
          .status = ActivationStatus::renderer_stopped,
          .failure_category = std::nullopt,
      };
    }
    if (candidate == nullptr) {
      return ActivationResult{
          .status = ActivationStatus::no_candidate,
          .failure_category = PlaybackCategory::route_activation_failed,
      };
    }

    IngressBarrier ingress{*this};
    const bool had_old_output = output_ != nullptr;
    const auto old_device_instance_id = device_instance_id_;
    bool old_handoff_prepared = false;
    bool old_handoff_exact = false;
    bool old_ownership_rebuilt = false;

    if (had_old_output && (output_active_ || output_paused_)) {
      const auto quiesce = quiesce_impl(false);
      if (quiesce.status == QuiesceStatus::stale_snapshot ||
          quiesce.status == QuiesceStatus::invalid_snapshot) {
        if (output_ != nullptr && output_active_ &&
            device_instance_id_ == old_device_instance_id) {
          return ActivationResult{
              .status = ActivationStatus::failed,
              .failure_category = quiesce.failure_category.value_or(
                  PlaybackCategory::route_candidate_stale),
          };
        }
        return ActivationResult{
            .status = ActivationStatus::failed,
            .failure_category = PlaybackCategory::audio_output_device_lost,
        };
      }
      old_handoff_prepared = true;
      old_handoff_exact = quiesce.status == QuiesceStatus::quiesced;
    } else if (had_old_output && output_quiesced_) {
      old_handoff_prepared = true;
      old_handoff_exact = !last_quiesce_unknown_;
      old_ownership_rebuilt = !last_quiesce_unknown_;
    }

    const auto fail_candidate =
        [&](ActivationStatus status,
            PlaybackCategory failure_category) -> ActivationResult {
      try {
        candidate->stop();
      } catch (...) {
      }

      if (old_handoff_prepared) {
        if (old_handoff_exact) {
          if (!old_ownership_rebuilt) {
            rebuild_after_handoff(true);
            old_ownership_rebuilt = true;
          }
          restore_exact_handoff_ownership();
        }
        // Unknown consumption still owns the old device queue. Keep the
        // in-flight blocks intact until old-route resume succeeds or fails.
        if (output_quiesced_ &&
            resume_old_output(old_device_instance_id)) {
          return ActivationResult{
              .status = status,
              .failure_category = failure_category,
          };
        }
        if (output_ != nullptr) {
          handle_unknown_output(PlaybackCategory::audio_output_device_lost);
        }
      }

      return ActivationResult{
          .status = status,
          .failure_category = failure_category,
      };
    };

    OpenResult open_result;
    try {
      open_result = candidate->open(config_.output_format);
    } catch (...) {
      open_result = OpenResult{
          .status = OpenStatus::failed,
          .failure_category = PlaybackCategory::route_activation_failed,
      };
    }
    if (!is_valid_open_result(open_result) ||
        open_result.status != OpenStatus::opened) {
      return fail_candidate(
          ActivationStatus::failed,
          open_result.failure_category.value_or(
              PlaybackCategory::route_activation_failed));
    }

    bool started = false;
    try {
      started = candidate->start();
    } catch (...) {
      started = false;
    }
    if (!started) {
      return fail_candidate(
          ActivationStatus::failed,
          PlaybackCategory::route_activation_failed);
    }

    AudioDeviceSnapshot candidate_snapshot;
    try {
      candidate_snapshot = candidate->snapshot();
    } catch (...) {
      candidate_snapshot.active = false;
    }
    if (!is_valid_candidate_snapshot(candidate_snapshot)) {
      return fail_candidate(
          ActivationStatus::candidate_stale,
          PlaybackCategory::route_candidate_stale);
    }

    AudioDeviceSnapshot commit_snapshot;
    try {
      commit_snapshot = candidate->snapshot();
    } catch (...) {
      commit_snapshot.active = false;
    }
    const bool candidate_still_valid =
        is_valid_candidate_snapshot(commit_snapshot) &&
        commit_snapshot.device_instance_id ==
            candidate_snapshot.device_instance_id &&
        commit_snapshot.snapshot_sequence >
            candidate_snapshot.snapshot_sequence;
    if (!candidate_still_valid) {
      return fail_candidate(
          ActivationStatus::candidate_stale,
          PlaybackCategory::route_candidate_stale);
    }

    if (old_handoff_prepared && !old_ownership_rebuilt) {
      rebuild_after_handoff(old_handoff_exact);
      old_ownership_rebuilt = true;
    }

    if (output_ != nullptr) {
      try {
        output_->stop();
      } catch (...) {
      }
    }
    output_ = std::move(candidate);
    output_active_ = true;
    output_quiesced_ = false;
    output_paused_ = false;
    last_quiesce_unknown_ = false;
    set_device_baseline(commit_snapshot, true);
    if (route_generation_ != std::numeric_limits<std::uint64_t>::max()) {
      ++route_generation_;
    }
    last_write_status_.reset();
    return ActivationResult{
        .status = ActivationStatus::activated,
        .failure_category = std::nullopt,
    };
  }

  [[nodiscard]] QuiesceResult quiesce_output() {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return QuiesceResult{
          .status = QuiesceStatus::renderer_stopped,
          .final_snapshot = {},
          .failure_category = std::nullopt,
      };
    }
    IngressBarrier ingress{*this};
    const auto result = quiesce_impl(true);
    return result;
  }

  void deactivate_output(PlaybackCategory reason) {
    IngressBarrier ingress{*this};
    record_discontinuity(reason);
    if (output_ != nullptr && output_active_) {
      const auto quiesce = quiesce_impl(true);
      if (quiesce.status == QuiesceStatus::stale_snapshot ||
          quiesce.status == QuiesceStatus::invalid_snapshot) {
        mark_unknown_consumption(
            PlaybackCategory::route_handoff_unknown_consumption);
        rebuild_after_handoff(false);
      }
    }
    if (output_ != nullptr) {
      try {
        output_->stop();
      } catch (...) {
      }
      output_.reset();
    }
    if (output_quiesced_ && last_quiesce_unknown_) {
      rebuild_after_handoff(false);
    }
    output_active_ = false;
    output_quiesced_ = false;
    output_paused_ = false;
  }

  void shutdown() noexcept {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      while (!shutdown_complete_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      return;
    }
    IngressBarrier ingress{*this};
    ingress.keep_closed();
    if (output_ != nullptr && output_active_) {
      const auto quiesce = quiesce_impl(true);
      if (quiesce.status == QuiesceStatus::stale_snapshot ||
          quiesce.status == QuiesceStatus::invalid_snapshot) {
        mark_unknown_consumption(
            PlaybackCategory::route_handoff_unknown_consumption);
        rebuild_after_handoff(false);
      }
    }
    if (output_ != nullptr) {
      try {
        output_->stop();
      } catch (...) {
      }
      output_.reset();
    }
    output_active_ = false;
    output_quiesced_ = false;
    output_paused_ = false;
    for (std::size_t index = 0; index < total_capacity(); ++index) {
      if (slots_[index].state.load(std::memory_order_acquire) !=
          SlotState::free) {
        release_slot(index);
      }
    }
    ready_tail_.store(ready_head_.load(std::memory_order_acquire),
                      std::memory_order_release);
    in_flight_tail_ = in_flight_head_;
    in_flight_count_ = 0;
    replay_tail_ = replay_head_;
    replay_count_ = 0;
    shutdown_complete_.store(true, std::memory_order_release);
  }

  void close_ingress() noexcept {
    callback_ingress_permanently_closed_.store(true,
                                                std::memory_order_release);
    close_callback_ingress();
  }

  [[nodiscard]] MovieAudioRendererSnapshot snapshot() const noexcept {
    MovieAudioRendererSnapshot result;
    result.media_frames_enqueued_total =
        media_frames_enqueued_total_.load(std::memory_order_acquire);
    result.backend_frames_written_total = backend_frames_written_total_;
    result.replayed_frames_total = replayed_frames_total_;
    result.device_consumed_frames_total = device_consumed_frames_total_;
    result.logical_consumed_frames = logical_consumed_frames_;
    result.ready_capacity = config_.ready_capacity;
    result.in_flight_capacity = config_.in_flight_capacity;
    result.ready_block_count = ready_size() + replay_count_;
    result.replay_block_count = replay_count_;
    result.in_flight_block_count = in_flight_count_;
    result.output_active = output_active_;
    result.accepting_callbacks = accepting_callbacks_.load(
        std::memory_order_acquire);
    result.released_pcm_block_count = released_pcm_block_count_;
    result.overflow_count = overflow_count_.load(std::memory_order_acquire);
    result.underrun_count = underrun_count_;
    result.discontinuity_count = discontinuity_count_.load(
        std::memory_order_acquire);
    result.device_queue_frames = device_snapshot_.device_queue_frames;
    result.device_consumed_frames_total = device_consumed_frames_total_;
    result.playback_generation = playback_generation_;
    result.host_audio_epoch = host_audio_epoch_;
    result.route_generation = route_generation_;
    result.renderer_clock_epoch = renderer_clock_epoch_;
    result.last_write_status = last_write_status_;

    std::uint64_t ready_frames = 0;
    const auto ready_tail = ready_tail_.load(std::memory_order_acquire);
    const auto ready_head = ready_head_.load(std::memory_order_acquire);
    for (auto position = ready_tail; position < ready_head; ++position) {
      const auto index = ready_indices_[static_cast<std::size_t>(
          position % config_.ready_capacity)];
      const auto& block = slots_[index];
      (void)saturating_add(
          ready_frames, block.metadata.frame_count - block.accepted_frames);
    }
    for (std::size_t offset = 0; offset < replay_count_; ++offset) {
      const auto index = replay_indices_[static_cast<std::size_t>(
          (replay_tail_ + offset) % replay_indices_.size())];
      const auto& block = slots_[index];
      (void)saturating_add(
          ready_frames, block.metadata.frame_count - block.accepted_frames);
    }
    result.ready_frame_count = ready_frames;

    std::uint64_t in_flight_frames = 0;
    for (std::size_t offset = 0; offset < in_flight_count_; ++offset) {
      const auto index = in_flight_indices_[static_cast<std::size_t>(
          (in_flight_tail_ + offset) % config_.in_flight_capacity)];
      const auto& block = slots_[index];
      (void)saturating_add(
          in_flight_frames, block.accepted_frames - block.consumed_frames);
    }
    result.in_flight_frame_count = in_flight_frames;
    result.renderer_queue_duration = audio_duration_ms(
        ready_frames, config_.output_format.sample_rate).value_or(0);
    result.device_queue_duration = audio_duration_ms(
        result.device_queue_frames, config_.output_format.sample_rate)
                                       .value_or(0);

    const auto clock_snapshot = clock_.snapshot();
    result.estimated_playout_pts_ms = clock_snapshot.estimated_playout_pts_ms;
    result.confidence = clock_snapshot.confidence;
    result.clock_confidence = clock_snapshot.clock_confidence;
    if (confidence_invalidated_.load(std::memory_order_acquire)) {
      result.confidence = ClockConfidence::invalid;
      result.clock_confidence = ClockConfidence::invalid;
    }
    if (has_discontinuity_reason_.load(std::memory_order_acquire)) {
      result.last_discontinuity_reason =
          last_discontinuity_reason_.load(std::memory_order_acquire);
    }
    return result;
  }

  [[nodiscard]] std::size_t ready_capacity() const noexcept {
    return config_.ready_capacity;
  }

  [[nodiscard]] std::size_t in_flight_capacity() const noexcept {
    return config_.in_flight_capacity;
  }

 private:
  [[nodiscard]] CallbackAdmission enter_callback() noexcept {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return CallbackAdmission::renderer_stopped;
    }

    auto state = callback_lifecycle_.load(std::memory_order_acquire);
    for (;;) {
      if ((state & kCallbackIngressClosed) != 0) {
        return shutting_down_.load(std::memory_order_acquire)
            ? CallbackAdmission::renderer_stopped
            : CallbackAdmission::not_accepting;
      }
      const auto active_callbacks = state & kCallbackCountMask;
      if (active_callbacks == kCallbackCountMask) {
        return CallbackAdmission::not_accepting;
      }
      if (callback_lifecycle_.compare_exchange_weak(
              state, state + 1, std::memory_order_acquire,
              std::memory_order_acquire)) {
        if (shutting_down_.load(std::memory_order_acquire)) {
          leave_callback();
          return CallbackAdmission::renderer_stopped;
        }
        return CallbackAdmission::admitted;
      }
    }
  }

  void leave_callback() noexcept {
    callback_lifecycle_.fetch_sub(1, std::memory_order_release);
  }

  void close_callback_ingress() noexcept {
    accepting_callbacks_.store(false, std::memory_order_release);
    auto state = callback_lifecycle_.load(std::memory_order_acquire);
    for (;;) {
      if ((state & kCallbackIngressClosed) != 0) {
        break;
      }
      const auto closed = state | kCallbackIngressClosed;
      if (callback_lifecycle_.compare_exchange_weak(
              state, closed, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        break;
      }
    }

    while ((callback_lifecycle_.load(std::memory_order_acquire) &
            kCallbackCountMask) != 0) {
      std::this_thread::yield();
    }
  }

  void reopen_callback_ingress() noexcept {
    if (shutting_down_.load(std::memory_order_acquire) ||
        callback_ingress_permanently_closed_.load(std::memory_order_acquire)) {
      return;
    }
    callback_lifecycle_.store(0, std::memory_order_release);
    accepting_callbacks_.store(true, std::memory_order_release);
  }

  void discard_pending_pcm() noexcept {
    for (std::size_t index = 0; index < total_capacity(); ++index) {
      if (slots_[index].state.load(std::memory_order_acquire) !=
          SlotState::free) {
        release_slot(index);
      }
    }
    ready_tail_.store(0, std::memory_order_release);
    ready_head_.store(0, std::memory_order_release);
    in_flight_tail_ = 0;
    in_flight_head_ = 0;
    in_flight_count_ = 0;
    replay_tail_ = 0;
    replay_head_ = 0;
    replay_count_ = 0;
  }

  [[nodiscard]] std::size_t total_capacity() const noexcept {
    return config_.ready_capacity + config_.in_flight_capacity;
  }

  [[nodiscard]] EnqueueResult overflow_result() noexcept {
    saturating_atomic_add(overflow_count_, 1);
    record_discontinuity(PlaybackCategory::audio_queue_overflow);
    return EnqueueResult{
        .status = EnqueueStatus::overflow,
        .accepted_frames = 0,
        .validation_error = std::nullopt,
        .failure_category = PlaybackCategory::audio_queue_overflow,
    };
  }

  [[nodiscard]] std::optional<std::size_t> claim_free_slot() noexcept {
    for (std::size_t index = 0; index < total_capacity(); ++index) {
      auto expected = SlotState::free;
      if (slots_[index].state.compare_exchange_strong(
              expected, SlotState::filling, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        return index;
      }
    }
    return std::nullopt;
  }

  void release_slot(std::size_t index) noexcept {
    auto& block = slots_[index];
    block.metadata = {};
    block.accepted_frames = 0;
    block.consumed_frames = 0;
    block.replay_pending_frames = 0;
    block.enqueue_order = 0;
    block.state.store(SlotState::free, std::memory_order_release);
    (void)saturating_add(released_pcm_block_count_, 1);
  }

  [[nodiscard]] bool ready_empty() const noexcept {
    return ready_tail_.load(std::memory_order_relaxed) ==
        ready_head_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool pending_ready_empty() const noexcept {
    return replay_count_ == 0 && ready_empty();
  }

  [[nodiscard]] std::size_t ready_size() const noexcept {
    const auto head = ready_head_.load(std::memory_order_acquire);
    const auto tail = ready_tail_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(head - tail);
  }

  [[nodiscard]] std::size_t pending_front_index(bool from_replay) const
      noexcept {
    if (from_replay) {
      return replay_indices_[static_cast<std::size_t>(
          replay_tail_ % replay_indices_.size())];
    }
    const auto tail = ready_tail_.load(std::memory_order_relaxed);
    return ready_indices_[static_cast<std::size_t>(
        tail % config_.ready_capacity)];
  }

  void advance_ready_front() noexcept {
    const auto tail = ready_tail_.load(std::memory_order_relaxed);
    if (tail == ready_head_.load(std::memory_order_acquire)) {
      return;
    }
    const auto index = ready_indices_[static_cast<std::size_t>(
        tail % config_.ready_capacity)];
    ready_tail_.store(tail + 1, std::memory_order_release);
    auto& block = slots_[index];
    if (block.accepted_frames == block.consumed_frames) {
      if (block.accepted_frames == block.metadata.frame_count) {
        release_slot(index);
      } else {
        block.state.store(SlotState::ready, std::memory_order_release);
      }
    } else {
      block.state.store(SlotState::in_flight, std::memory_order_release);
    }
  }

  void advance_pending_front(bool from_replay) noexcept {
    if (!from_replay) {
      advance_ready_front();
      return;
    }
    if (replay_count_ == 0) {
      return;
    }
    const auto index = replay_indices_[static_cast<std::size_t>(
        replay_tail_ % replay_indices_.size())];
    ++replay_tail_;
    --replay_count_;
    auto& block = slots_[index];
    if (block.accepted_frames == block.consumed_frames) {
      if (block.accepted_frames == block.metadata.frame_count) {
        release_slot(index);
      } else {
        block.state.store(SlotState::ready, std::memory_order_release);
      }
    } else {
      block.state.store(SlotState::in_flight, std::memory_order_release);
    }
  }

  void append_in_flight(std::size_t index) noexcept {
    if (in_flight_count_ == config_.in_flight_capacity) {
      return;
    }
    in_flight_indices_[static_cast<std::size_t>(
        in_flight_head_ % config_.in_flight_capacity)] = index;
    ++in_flight_head_;
    ++in_flight_count_;
    slots_[index].state.store(SlotState::in_flight,
                              std::memory_order_release);
  }

  void append_ready_index(std::size_t index) noexcept {
    const auto head = ready_head_.load(std::memory_order_relaxed);
    ready_indices_[static_cast<std::size_t>(head % config_.ready_capacity)] =
        index;
    ready_head_.store(head + 1, std::memory_order_release);
  }

  void remove_in_flight_front() noexcept {
    if (in_flight_count_ == 0) {
      return;
    }
    const auto index = in_flight_indices_[static_cast<std::size_t>(
        in_flight_tail_ % config_.in_flight_capacity)];
    ++in_flight_tail_;
    --in_flight_count_;
    auto& block = slots_[index];
    if (block.accepted_frames == block.consumed_frames &&
        block.accepted_frames == block.metadata.frame_count) {
      release_slot(index);
    } else {
      block.state.store(SlotState::ready, std::memory_order_release);
    }
  }

  [[nodiscard]] AudioPcmBlockView make_write_view(
      const OwnedPcmBlock& block, std::uint64_t remaining_frames) const noexcept {
    const auto stride = block.metadata.payload.frame_stride_bytes;
    const auto byte_offset = static_cast<std::size_t>(block.accepted_frames) *
        static_cast<std::size_t>(stride);
    const auto byte_count = static_cast<std::size_t>(remaining_frames) *
        static_cast<std::size_t>(stride);
    const std::span<const std::byte> storage{block.bytes.data(),
                                             block.bytes.size()};
    return AudioPcmBlockView{
        .receiver_sequence = block.metadata.receiver_sequence,
        .frame_count = remaining_frames,
        .sample_rate = block.metadata.sample_rate,
        .channel_count = block.metadata.channel_count,
        .sample_format = block.metadata.sample_format,
        .interleaving = block.metadata.interleaving,
        .payload = AudioPcmPayloadView{
            .bytes = storage.subspan(byte_offset, byte_count),
            .frame_stride_bytes = stride,
        },
    };
  }

  [[nodiscard]] bool is_valid_candidate_snapshot(
      AudioDeviceSnapshot snapshot) const noexcept {
    return snapshot.device_instance_id != 0 &&
        snapshot.snapshot_sequence != 0 && snapshot.active &&
        is_valid_audio_device_snapshot(
            snapshot.accepted_frames_total,
            snapshot.device_consumed_frames_total,
            snapshot.device_queue_frames) &&
        snapshot.accepted_frames_total == 0 &&
        snapshot.device_consumed_frames_total == 0 &&
        snapshot.device_queue_frames == 0;
  }

  [[nodiscard]] bool has_renderer_write_provenance(
      AudioDeviceSnapshot snapshot) const noexcept {
    if (snapshot.accepted_frames_total < last_device_accepted_raw_ ||
        backend_frames_written_total_ < device_backend_write_baseline_) {
      return false;
    }
    const auto accepted_delta = snapshot.accepted_frames_total -
        last_device_accepted_raw_;
    const auto renderer_write_delta = backend_frames_written_total_ -
        device_backend_write_baseline_;
    return accepted_delta <= renderer_write_delta;
  }

  [[nodiscard]] bool is_valid_current_snapshot(
      AudioDeviceSnapshot snapshot) const noexcept {
    return snapshot.device_instance_id != 0 &&
        snapshot.snapshot_sequence != 0 &&
        is_valid_audio_device_snapshot(
            snapshot.accepted_frames_total,
            snapshot.device_consumed_frames_total,
            snapshot.device_queue_frames) &&
        snapshot.device_consumed_frames_total >= last_device_consumed_raw_ &&
         snapshot.accepted_frames_total >= last_device_accepted_raw_ &&
         snapshot.underrun_count >= last_device_underrun_raw_ &&
         snapshot.discontinuity_count >= last_device_discontinuity_raw_ &&
        has_renderer_write_provenance(snapshot);
  }

  [[nodiscard]] bool in_flight_frame_count(
      std::uint64_t& result) const noexcept {
    result = 0;
    for (std::size_t offset = 0; offset < in_flight_count_; ++offset) {
      const auto index = in_flight_indices_[static_cast<std::size_t>(
          (in_flight_tail_ + offset) % config_.in_flight_capacity)];
      const auto& block = slots_[index];
      if (block.consumed_frames > block.accepted_frames ||
          !saturating_add(
              result, block.accepted_frames - block.consumed_frames)) {
        return false;
      }
    }
    return true;
  }

  void handle_unknown_output(PlaybackCategory reason) noexcept {
    output_active_ = false;
    output_quiesced_ = false;
    output_paused_ = false;
    device_snapshot_.active = false;
    if (output_ != nullptr) {
      try {
        output_->stop();
      } catch (...) {
      }
    }
    mark_unknown_consumption(reason);
    rebuild_after_handoff(false);
  }

  void observe_output_snapshot(MonotonicTime monotonic_now) {
    if (output_ == nullptr) {
      return;
    }
    AudioDeviceSnapshot snapshot;
    try {
      snapshot = output_->snapshot();
    } catch (...) {
      handle_unknown_output(PlaybackCategory::audio_output_failure);
      return;
    }
    if (snapshot.device_instance_id != device_instance_id_ ||
        snapshot.snapshot_sequence <= last_snapshot_sequence_) {
      return;
    }
    if (!is_valid_current_snapshot(snapshot)) {
      handle_unknown_output(PlaybackCategory::audio_output_failure);
      return;
    }

    if (!snapshot.active) {
      handle_unknown_output(PlaybackCategory::audio_output_device_lost);
      return;
    }

    const auto consumed_delta = snapshot.device_consumed_frames_total -
        last_device_consumed_raw_;
    const bool consumption_known = apply_consumed_frames(consumed_delta);
    if (!consumption_known) {
      handle_unknown_output(PlaybackCategory::audio_consumption_unknown);
      return;
    }
    const bool underrun = snapshot.underrun_count > last_device_underrun_raw_;
    update_device_facts(snapshot);
    output_active_ = snapshot.active;
    observe_clock(monotonic_now, consumption_known,
                  snapshot.output_latency_frames, false, underrun);
  }

  [[nodiscard]] bool apply_consumed_frames(
      std::uint64_t delta) noexcept {
    std::uint64_t available_total = 0;
    for (std::size_t offset = 0; offset < in_flight_count_; ++offset) {
      const auto index = in_flight_indices_[static_cast<std::size_t>(
          (in_flight_tail_ + offset) % config_.in_flight_capacity)];
      const auto& block = slots_[index];
      if (!saturating_add(available_total,
                          block.accepted_frames - block.consumed_frames)) {
        return false;
      }
    }
    if (delta > available_total) {
      return false;
    }
    std::uint64_t new_logical = 0;
    if (!checked_add(logical_consumed_frames_, delta, new_logical)) {
      return false;
    }

    auto remaining = delta;
    while (remaining != 0) {
      const auto index = in_flight_indices_[static_cast<std::size_t>(
          in_flight_tail_ % config_.in_flight_capacity)];
      auto& block = slots_[index];
      const auto available = block.accepted_frames - block.consumed_frames;
      if (available == 0) {
        remove_in_flight_front();
        continue;
      }
      const auto consumed = std::min(available, remaining);
      logical_consumed_frames_ += consumed;
      block.consumed_frames += consumed;
      remaining -= consumed;
      if (block.consumed_frames == block.accepted_frames) {
        remove_in_flight_front();
      }
    }
    logical_consumed_frames_ = new_logical;
    return true;
  }

  void update_device_facts(
      AudioDeviceSnapshot snapshot, bool consumption_known = true) noexcept {
    if (snapshot.underrun_count >= last_device_underrun_raw_) {
      (void)saturating_add(
          underrun_count_, snapshot.underrun_count - last_device_underrun_raw_);
    }
    if (snapshot.discontinuity_count >= last_device_discontinuity_raw_) {
      const auto new_events = snapshot.discontinuity_count -
          last_device_discontinuity_raw_;
      if (new_events != 0) {
        record_discontinuity(
            snapshot.last_discontinuity_reason.value_or(
                PlaybackCategory::audio_output_failure),
            new_events);
      }
    }
    last_device_underrun_raw_ = snapshot.underrun_count;
    last_device_discontinuity_raw_ = snapshot.discontinuity_count;
    if (consumption_known) {
      last_device_consumed_raw_ = snapshot.device_consumed_frames_total;
      device_consumed_frames_total_ =
          snapshot.device_consumed_frames_total;
    } else {
      snapshot.device_consumed_frames_total = last_device_consumed_raw_;
    }
    last_device_accepted_raw_ = snapshot.accepted_frames_total;
    last_snapshot_sequence_ = snapshot.snapshot_sequence;
    device_snapshot_ = snapshot;
  }

  [[nodiscard]] QuiesceResult quiesce_impl(bool rebuild_ownership) {
    if (output_ == nullptr) {
      return QuiesceResult{
          .status = QuiesceStatus::no_output,
          .final_snapshot = {},
          .failure_category = PlaybackCategory::route_no_active_output,
      };
    }
    if (output_quiesced_) {
      return QuiesceResult{
          .status = last_quiesce_unknown_ ? QuiesceStatus::unknown_consumption
                                           : QuiesceStatus::quiesced,
          .final_snapshot = last_final_snapshot_,
          .failure_category = last_quiesce_unknown_
              ? std::optional<PlaybackCategory>{
                    PlaybackCategory::route_handoff_unknown_consumption}
              : std::nullopt,
      };
    }

    FinalDeviceSnapshot final_snapshot;
    try {
      final_snapshot = output_->quiesce_and_snapshot();
    } catch (...) {
      const auto resumed = resume_old_output(device_instance_id_);
      if (!resumed) {
        handle_unknown_output(PlaybackCategory::audio_output_failure);
      }
      return QuiesceResult{
          .status = QuiesceStatus::invalid_snapshot,
          .final_snapshot = {},
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }
    if (final_snapshot.device_instance_id == 0 ||
        final_snapshot.snapshot_sequence == 0) {
      const auto resumed = resume_old_output(device_instance_id_);
      if (!resumed) {
        handle_unknown_output(PlaybackCategory::audio_output_failure);
      }
      return QuiesceResult{
          .status = QuiesceStatus::invalid_snapshot,
          .final_snapshot = final_snapshot,
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }
    if (final_snapshot.device_instance_id != device_instance_id_ ||
        final_snapshot.snapshot_sequence <= last_snapshot_sequence_) {
      const auto resumed = resume_old_output(device_instance_id_);
      if (!resumed) {
        handle_unknown_output(
            PlaybackCategory::route_handoff_unknown_consumption);
      }
      return QuiesceResult{
          .status = QuiesceStatus::stale_snapshot,
          .final_snapshot = final_snapshot,
          .failure_category = PlaybackCategory::route_candidate_stale,
      };
    }
    const auto ordinary = as_ordinary_snapshot(final_snapshot);
    if (final_snapshot.active || !is_valid_final_device_snapshot(final_snapshot) ||
        !is_valid_current_snapshot(ordinary)) {
      const auto resumed = resume_old_output(device_instance_id_);
      if (!resumed) {
        handle_unknown_output(PlaybackCategory::audio_output_failure);
      }
      return QuiesceResult{
          .status = QuiesceStatus::invalid_snapshot,
          .final_snapshot = final_snapshot,
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }

    std::uint64_t in_flight_frames = 0;
    if (!in_flight_frame_count(in_flight_frames) ||
        final_snapshot.device_queue_frames > in_flight_frames) {
      const auto resumed = resume_old_output(device_instance_id_);
      if (!resumed) {
        handle_unknown_output(PlaybackCategory::audio_output_failure);
      }
      return QuiesceResult{
          .status = QuiesceStatus::invalid_snapshot,
          .final_snapshot = final_snapshot,
          .failure_category = PlaybackCategory::audio_output_failure,
      };
    }

    bool exact_consumption = final_snapshot.exact_consumption;
    if (exact_consumption) {
      const auto consumed_delta =
          final_snapshot.device_consumed_frames_total -
          last_device_consumed_raw_;
      const auto expected_consumed_delta =
          in_flight_frames - final_snapshot.device_queue_frames;
      if (consumed_delta != expected_consumed_delta) {
        const auto resumed = resume_old_output(device_instance_id_);
        if (!resumed) {
          handle_unknown_output(PlaybackCategory::audio_output_failure);
        }
        return QuiesceResult{
            .status = QuiesceStatus::invalid_snapshot,
            .final_snapshot = final_snapshot,
            .failure_category = PlaybackCategory::audio_output_failure,
        };
      }
      exact_consumption = apply_consumed_frames(consumed_delta);
    }
    update_device_facts(ordinary, exact_consumption);
    output_active_ = false;
    output_quiesced_ = true;
    output_paused_ = false;
    last_final_snapshot_ = final_snapshot;
    last_quiesce_unknown_ = !exact_consumption;
    if (exact_consumption) {
      if (rebuild_ownership) {
        rebuild_after_handoff(true);
      }
      return QuiesceResult{
          .status = QuiesceStatus::quiesced,
          .final_snapshot = final_snapshot,
          .failure_category = std::nullopt,
      };
    }

    mark_unknown_consumption(PlaybackCategory::route_handoff_unknown_consumption);
    return QuiesceResult{
        .status = QuiesceStatus::unknown_consumption,
        .final_snapshot = final_snapshot,
        .failure_category = PlaybackCategory::route_handoff_unknown_consumption,
    };
  }

  void rebuild_after_handoff(bool exact_consumption) noexcept {
    ready_tail_.store(0, std::memory_order_release);
    ready_head_.store(0, std::memory_order_release);
    in_flight_tail_ = 0;
    in_flight_head_ = 0;
    in_flight_count_ = 0;
    replay_tail_ = 0;
    replay_head_ = 0;
    replay_count_ = 0;

    std::size_t live_count = 0;
    for (std::size_t index = 0; index < total_capacity(); ++index) {
      if (slots_[index].state.load(std::memory_order_acquire) !=
          SlotState::free) {
        rebuild_order_[live_count++] = index;
      }
    }
    std::sort(rebuild_order_.begin(), rebuild_order_.begin() +
                                      static_cast<std::ptrdiff_t>(live_count),
              [this](std::size_t left, std::size_t right) {
                return slots_[left].enqueue_order < slots_[right].enqueue_order;
              });

    for (std::size_t offset = 0; offset < live_count; ++offset) {
      const auto index = rebuild_order_[offset];
      auto& block = slots_[index];
      if (!exact_consumption) {
        if (block.accepted_frames != 0) {
          release_slot(index);
          continue;
        }
        block.state.store(SlotState::ready, std::memory_order_release);
        append_replay_after_handoff(index);
        continue;
      }

      if (block.consumed_frames >= block.metadata.frame_count) {
        release_slot(index);
        continue;
      }
      const auto old_accepted_frames = block.accepted_frames;
      block.replay_pending_frames = old_accepted_frames -
          block.consumed_frames;
      block.accepted_frames = block.consumed_frames;
      block.state.store(SlotState::ready, std::memory_order_release);
      append_replay_after_handoff(index);
    }
  }

  void restore_exact_handoff_ownership() noexcept {
    const auto live_count = replay_count_;
    for (std::size_t offset = 0; offset < live_count; ++offset) {
      rebuild_order_[offset] = replay_indices_[static_cast<std::size_t>(
          (replay_tail_ + offset) % replay_indices_.size())];
    }

    ready_tail_.store(0, std::memory_order_release);
    ready_head_.store(0, std::memory_order_release);
    in_flight_tail_ = 0;
    in_flight_head_ = 0;
    in_flight_count_ = 0;
    replay_tail_ = 0;
    replay_head_ = 0;
    replay_count_ = 0;

    for (std::size_t offset = 0; offset < live_count; ++offset) {
      const auto index = rebuild_order_[offset];
      auto& block = slots_[index];
      const auto old_accepted_frames = block.accepted_frames +
          block.replay_pending_frames;
      block.accepted_frames = old_accepted_frames;
      block.replay_pending_frames = 0;

      if (block.accepted_frames == block.consumed_frames &&
          block.accepted_frames == block.metadata.frame_count) {
        release_slot(index);
        continue;
      }
      if (block.accepted_frames > block.consumed_frames) {
        append_in_flight(index);
      }
      if (block.accepted_frames < block.metadata.frame_count) {
        append_ready_index(index);
        if (block.accepted_frames == block.consumed_frames) {
          block.state.store(SlotState::ready, std::memory_order_release);
        }
      }
    }
  }

  void append_replay_after_handoff(std::size_t index) noexcept {
    replay_indices_[static_cast<std::size_t>(
        replay_head_ % replay_indices_.size())] = index;
    ++replay_head_;
    ++replay_count_;
  }

  [[nodiscard]] bool resume_old_output(
      std::uint64_t expected_device_instance_id) noexcept {
    if (output_ == nullptr || expected_device_instance_id == 0) {
      return false;
    }
    bool started = false;
    try {
      started = output_->start();
    } catch (...) {
      started = false;
    }
    if (!started) {
      output_active_ = false;
      output_paused_ = false;
      return false;
    }
    AudioDeviceSnapshot snapshot;
    try {
      snapshot = output_->snapshot();
    } catch (...) {
      output_active_ = false;
      output_paused_ = false;
      try {
        output_->stop();
      } catch (...) {
      }
      return false;
    }
    if (snapshot.device_instance_id != expected_device_instance_id ||
        snapshot.snapshot_sequence <= last_snapshot_sequence_ ||
        !snapshot.active || !is_valid_current_snapshot(snapshot)) {
      output_active_ = false;
      output_paused_ = false;
      try {
        output_->stop();
      } catch (...) {
      }
      return false;
    }
    const auto consumed_delta = snapshot.device_consumed_frames_total -
        last_device_consumed_raw_;
    if (!apply_consumed_frames(consumed_delta)) {
      output_active_ = false;
      output_paused_ = false;
      try {
        output_->stop();
      } catch (...) {
      }
      return false;
    }
    const bool underrun = snapshot.underrun_count > last_device_underrun_raw_;
    update_device_facts(snapshot);
    output_active_ = true;
    output_quiesced_ = false;
    output_paused_ = false;
    observe_clock(clock_time(), true, snapshot.output_latency_frames, false,
                  underrun);
    return true;
  }

  void set_device_baseline(
      AudioDeviceSnapshot snapshot, bool reset_write_baseline) noexcept {
    device_instance_id_ = snapshot.device_instance_id;
    last_snapshot_sequence_ = snapshot.snapshot_sequence;
    last_device_consumed_raw_ = snapshot.device_consumed_frames_total;
    last_device_accepted_raw_ = snapshot.accepted_frames_total;
    last_device_underrun_raw_ = snapshot.underrun_count;
    last_device_discontinuity_raw_ = snapshot.discontinuity_count;
    device_consumed_frames_total_ =
        snapshot.device_consumed_frames_total;
    device_snapshot_ = snapshot;
    if (reset_write_baseline) {
      device_backend_write_baseline_ = backend_frames_written_total_;
    }
  }

  void mark_unknown_consumption(PlaybackCategory reason) noexcept {
    consumption_unknown_ = true;
    record_discontinuity(reason);
    discontinuity_pending_.store(false, std::memory_order_release);
    observe_clock(clock_time(), false, std::nullopt, true, false);
  }

  void observe_clock(
      MonotonicTime monotonic_now, bool consumption_known,
      std::optional<std::uint64_t> output_latency_frames,
      bool discontinuity, bool underrun) noexcept {
    const bool pending_discontinuity =
        discontinuity_pending_.exchange(false, std::memory_order_acq_rel);
    AudioClockObservation observation{
        .logical_consumed_frames = logical_consumed_frames_,
        .output_latency_frames = output_latency_frames,
        .sample_rate = config_.output_format.sample_rate,
        .playback_generation = playback_generation_,
        .host_audio_epoch = host_audio_epoch_,
        .renderer_clock_epoch = renderer_clock_epoch_,
        .route_generation = route_generation_,
        .consumption_known = consumption_known,
        .anchor = anchor_,
        .correlation = std::nullopt,
        .discontinuity = discontinuity || pending_discontinuity,
        .underrun = underrun,
    };
    const auto result = clock_.observe(observation, monotonic_now);
    renderer_clock_epoch_ = result.renderer_clock_epoch;
    if (consumption_known && !discontinuity && !underrun &&
        result.confidence != ClockConfidence::invalid &&
        (!consumption_unknown_ ||
         result.confidence == ClockConfidence::locked)) {
      confidence_invalidated_.store(false, std::memory_order_release);
      if (result.confidence == ClockConfidence::locked) {
        consumption_unknown_ = false;
      }
    }
  }

  [[nodiscard]] MonotonicTime clock_time() const noexcept {
    if (last_now_.has_value()) {
      return *last_now_;
    }
    return std::chrono::steady_clock::now();
  }

  void record_discontinuity(
      PlaybackCategory reason, std::uint64_t count = 1) noexcept {
    saturating_atomic_add(discontinuity_count_, count);
    last_discontinuity_reason_.store(reason, std::memory_order_release);
    has_discontinuity_reason_.store(true, std::memory_order_release);
    confidence_invalidated_.store(true, std::memory_order_release);
    discontinuity_pending_.store(true, std::memory_order_release);
  }

  const MovieAudioRendererConfig config_;
  std::unique_ptr<OwnedPcmBlock[]> slots_;
  std::vector<std::size_t> ready_indices_;
  std::vector<std::size_t> in_flight_indices_;
  std::vector<std::size_t> replay_indices_;
  std::vector<std::size_t> rebuild_order_;

  // Ring cursors intentionally use unsigned modulo arithmetic. Their
  // differences stay bounded by the corresponding fixed-capacity ring.
  std::atomic<std::uint64_t> ready_head_{0};
  std::atomic<std::uint64_t> ready_tail_{0};
  std::uint64_t in_flight_head_ = 0;
  std::uint64_t in_flight_tail_ = 0;
  std::size_t in_flight_count_ = 0;
  std::uint64_t replay_head_ = 0;
  std::uint64_t replay_tail_ = 0;
  std::size_t replay_count_ = 0;
  std::uint64_t next_enqueue_order_ = 1;

  std::atomic<std::uint64_t> callback_lifecycle_{0};
  std::atomic<bool> accepting_callbacks_{true};
  std::atomic<bool> callback_ingress_permanently_closed_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<bool> shutdown_complete_{false};
  std::atomic<std::uint64_t> media_frames_enqueued_total_{0};
  std::atomic<std::uint64_t> overflow_count_{0};
  std::atomic<std::uint64_t> discontinuity_count_{0};
  std::atomic<bool> confidence_invalidated_{false};
  std::atomic<bool> discontinuity_pending_{false};
  std::atomic<bool> has_discontinuity_reason_{false};
  std::atomic<PlaybackCategory> last_discontinuity_reason_{
      PlaybackCategory::audio_output_failure};

  std::unique_ptr<AudioOutputDevice> output_;
  bool output_active_ = false;
  bool output_quiesced_ = false;
  bool output_paused_ = false;
  bool last_quiesce_unknown_ = false;
  std::uint64_t device_instance_id_ = 0;
  std::uint64_t last_snapshot_sequence_ = 0;
  std::uint64_t last_device_consumed_raw_ = 0;
  std::uint64_t last_device_accepted_raw_ = 0;
  std::uint64_t last_device_underrun_raw_ = 0;
  std::uint64_t last_device_discontinuity_raw_ = 0;
  std::uint64_t device_backend_write_baseline_ = 0;
  AudioDeviceSnapshot device_snapshot_{ };
  FinalDeviceSnapshot last_final_snapshot_{ };
  std::uint64_t device_consumed_frames_total_ = 0;

  std::uint64_t backend_frames_written_total_ = 0;
  std::uint64_t replayed_frames_total_ = 0;
  std::uint64_t logical_consumed_frames_ = 0;
  std::uint64_t released_pcm_block_count_ = 0;
  std::uint64_t underrun_count_ = 0;
  std::uint64_t playback_generation_ = 0;
  std::uint64_t host_audio_epoch_ = 0;
  std::uint64_t route_generation_ = 0;
  std::uint64_t renderer_clock_epoch_ = 0;
  bool consumption_unknown_ = false;
  std::optional<AudioClockAnchor> anchor_ = std::nullopt;
  std::optional<MonotonicTime> last_now_ = std::nullopt;
  MovieAudioClock clock_;
  std::optional<WriteStatus> last_write_status_ = std::nullopt;
};

MovieAudioRenderer::MovieAudioRenderer(MovieAudioRendererConfig config)
    : impl_{std::make_unique<Impl>(config)} {}

MovieAudioRenderer::MovieAudioRenderer(
    std::size_t ready_capacity, std::size_t in_flight_capacity)
    : MovieAudioRenderer(MovieAudioRendererConfig{
          .ready_capacity = ready_capacity,
          .in_flight_capacity = in_flight_capacity,
      }) {}

MovieAudioRenderer::~MovieAudioRenderer() {
  if (impl_ != nullptr) {
    impl_->shutdown();
  }
}

EnqueueResult MovieAudioRenderer::try_enqueue(
    AudioPcmBlockView pcm, std::uint64_t receiver_sequence) noexcept {
  return impl_->try_enqueue(pcm, receiver_sequence);
}

void MovieAudioRenderer::set_playback_anchor(AudioClockAnchor anchor) noexcept {
  impl_->set_playback_anchor(anchor);
}

void MovieAudioRenderer::clear_playback_anchor() noexcept {
  impl_->clear_playback_anchor();
}

void MovieAudioRenderer::pause_output() noexcept { impl_->pause_output(); }

void MovieAudioRenderer::resume_output() noexcept { impl_->resume_output(); }

void MovieAudioRenderer::pump(MonotonicTime monotonic_now) {
  impl_->pump(monotonic_now);
}

ActivationResult MovieAudioRenderer::activate_output(
    std::unique_ptr<AudioOutputDevice> device) {
  return impl_->activate_output(std::move(device));
}

QuiesceResult MovieAudioRenderer::quiesce_output() {
  return impl_->quiesce_output();
}

void MovieAudioRenderer::deactivate_output(PlaybackCategory reason) {
  impl_->deactivate_output(reason);
}

void MovieAudioRenderer::close_ingress() noexcept { impl_->close_ingress(); }

void MovieAudioRenderer::shutdown() noexcept {
  impl_->shutdown();
}

MovieAudioRendererSnapshot MovieAudioRenderer::snapshot() const noexcept {
  return impl_->snapshot();
}

std::size_t MovieAudioRenderer::ready_capacity() const noexcept {
  return impl_->ready_capacity();
}

std::size_t MovieAudioRenderer::in_flight_capacity() const noexcept {
  return impl_->in_flight_capacity();
}

}  // namespace shareme::core
