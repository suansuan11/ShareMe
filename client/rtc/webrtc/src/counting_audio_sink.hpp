#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "api/media_stream_interface.h"

namespace shareme::rtc {

class CountingAudioSink final : public webrtc::AudioTrackSinkInterface {
public:
  void OnData(const void *audio_data, int bits_per_sample, int sample_rate,
              std::size_t number_of_channels, std::size_t number_of_frames,
              std::optional<std::int64_t>) override {
    sample_rate_.store(sample_rate, std::memory_order_relaxed);
    channels_.store(static_cast<int>(number_of_channels),
                    std::memory_order_relaxed);

    int observed_peak = 0;
    if (audio_data != nullptr && bits_per_sample == 16) {
      const auto *samples = static_cast<const std::int16_t *>(audio_data);
      const auto sample_count = number_of_channels * number_of_frames;
      for (std::size_t index = 0; index < sample_count; ++index) {
        const auto sample = static_cast<std::int32_t>(samples[index]);
        observed_peak = std::max(
            observed_peak, static_cast<int>(sample < 0 ? -sample : sample));
      }
    }

    auto current_peak = peak_.load(std::memory_order_relaxed);
    while (current_peak < observed_peak &&
           !peak_.compare_exchange_weak(current_peak, observed_peak,
                                        std::memory_order_relaxed)) {
    }
    callback_count_.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t callback_count() const noexcept {
    return callback_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int sample_rate() const noexcept {
    return sample_rate_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int channels() const noexcept {
    return channels_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int peak() const noexcept {
    return peak_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint64_t> callback_count_{0};
  std::atomic<int> sample_rate_{0};
  std::atomic<int> channels_{0};
  std::atomic<int> peak_{0};
};

} // namespace shareme::rtc
