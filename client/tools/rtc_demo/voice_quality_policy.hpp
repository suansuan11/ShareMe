#pragma once

#include <cstdint>
#include <optional>

namespace shareme::tools {

enum class VoiceQualityCategory { checking, good, unstable, poor, muted };

struct VoiceQualitySnapshot {
  std::optional<std::uint64_t> packets_received;
  std::optional<std::int64_t> packets_lost;
  std::optional<std::uint64_t> concealed_samples;
  std::optional<std::uint64_t> total_samples_received;
  std::optional<double> jitter_ms;
};

struct VoiceQualityResult {
  VoiceQualityCategory category{VoiceQualityCategory::checking};
  std::optional<double> packet_loss_ratio;
  std::optional<double> concealment_ratio;
};

class VoiceQualityPolicy final {
public:
  [[nodiscard]] VoiceQualityResult
  evaluate(const VoiceQualitySnapshot &snapshot, bool remote_muted = false);
  void reset() noexcept;

private:
  std::optional<VoiceQualitySnapshot> previous_;
};

} // namespace shareme::tools
