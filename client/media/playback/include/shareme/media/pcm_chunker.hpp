#pragma once

#include "shareme/media/media_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace shareme::media {

struct PcmChunk {
  std::vector<std::int16_t> interleaved_samples;
  std::int64_t pts_ms{0};
};

class PcmChunker {
public:
  [[nodiscard]] bool push(AudioFrame frame);
  [[nodiscard]] std::optional<PcmChunk> pop();
  void reset() noexcept;
  [[nodiscard]] std::string error() const;

private:
  static constexpr std::size_t samples_per_channel_per_chunk = 480;
  static constexpr std::size_t channel_count = 2;
  static constexpr std::size_t samples_per_chunk =
      samples_per_channel_per_chunk * channel_count;
  static constexpr std::size_t max_pending_samples =
      4'800 * channel_count;

  std::vector<std::int16_t> pending_samples_;
  std::optional<std::int64_t> pending_start_pts_ms_;
  std::string error_;
};

}  // namespace shareme::media
