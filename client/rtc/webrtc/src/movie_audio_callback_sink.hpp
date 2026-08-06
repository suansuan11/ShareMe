#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

#include "api/media_stream_interface.h"
#include "counting_audio_sink.hpp"
#include "shareme/core/audio_output_contract.hpp"

namespace shareme::rtc {

class MovieAudioCallbackSink final : public webrtc::AudioTrackSinkInterface {
 public:
  using Callback = std::function<void(shareme::core::AudioPcmBlockView,
                                      std::uint64_t)>;

  void set_callback(Callback callback) {
    std::lock_guard lock(callback_mutex_);
    callback_ = std::move(callback);
  }

  void close_and_wait() noexcept {
    std::unique_lock lock(callback_mutex_);
    callback_accepting_ = false;
    callback_cv_.wait(lock, [this] { return callbacks_in_flight_ == 0; });
  }

  void OnData(const void *audio_data, int bits_per_sample, int sample_rate,
              std::size_t number_of_channels, std::size_t number_of_frames,
              std::optional<std::int64_t> capture_timestamp_ms) override {
    Callback callback;
    {
      std::lock_guard lock(callback_mutex_);
      if (!callback_accepting_ || !callback_)
        return;
      ++callbacks_in_flight_;
      try {
        callback = callback_;
      } catch (...) {
        leave_callback_locked();
        return;
      }
    }

    const auto leave = [this] { leave_callback(); };
    counter_.OnData(audio_data, bits_per_sample, sample_rate,
                    number_of_channels, number_of_frames,
                    capture_timestamp_ms);
    if (audio_data == nullptr || bits_per_sample != 16 ||
        sample_rate != 48'000 || number_of_channels != 2 ||
        number_of_frames != 480) {
      leave();
      return;
    }

    const auto receiver_sequence =
        receiver_sequence_.fetch_add(1, std::memory_order_release) + 1;
    const auto frame_stride_bytes =
        static_cast<std::uint32_t>(number_of_channels * sizeof(std::int16_t));
    const auto byte_count = number_of_frames * frame_stride_bytes;
    const shareme::core::AudioPcmBlockView pcm{
        .receiver_sequence = receiver_sequence,
        .frame_count = number_of_frames,
        .sample_rate = static_cast<std::uint32_t>(sample_rate),
        .channel_count = static_cast<std::uint16_t>(number_of_channels),
        .sample_format = shareme::core::AudioSampleFormat::signed_int16,
        .interleaving = shareme::core::AudioInterleaving::interleaved,
        .payload = {.bytes = std::span<const std::byte>{
                        static_cast<const std::byte *>(audio_data), byte_count},
                    .frame_stride_bytes = frame_stride_bytes}};
    try {
      callback(pcm, receiver_sequence);
    } catch (...) {
      // WebRTC callbacks must not propagate application exceptions.
    }
    leave();
  }

  [[nodiscard]] CountingAudioSink::Snapshot snapshot() const noexcept {
    return counter_.snapshot();
  }

 private:
  void leave_callback_locked() noexcept {
    --callbacks_in_flight_;
    if (callbacks_in_flight_ == 0)
      callback_cv_.notify_all();
  }

  void leave_callback() noexcept {
    std::lock_guard lock(callback_mutex_);
    leave_callback_locked();
  }

  CountingAudioSink counter_;
  Callback callback_;
  mutable std::mutex callback_mutex_;
  std::condition_variable callback_cv_;
  std::size_t callbacks_in_flight_{0};
  bool callback_accepting_{true};
  std::atomic<std::uint64_t> receiver_sequence_{0};
};

}  // namespace shareme::rtc
