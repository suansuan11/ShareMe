#include "voice_quality_policy.hpp"

#include <cmath>

namespace shareme::tools {
namespace {

[[nodiscard]] bool complete(const VoiceQualitySnapshot &snapshot) noexcept {
  return snapshot.packets_received && snapshot.packets_lost &&
         snapshot.concealed_samples && snapshot.total_samples_received &&
         snapshot.jitter_ms && std::isfinite(*snapshot.jitter_ms) &&
         *snapshot.jitter_ms >= 0.0 && *snapshot.packets_lost >= 0;
}

[[nodiscard]] bool regressed(const VoiceQualitySnapshot &current,
                             const VoiceQualitySnapshot &previous) noexcept {
  return *current.packets_received < *previous.packets_received ||
         *current.packets_lost < *previous.packets_lost ||
         *current.concealed_samples < *previous.concealed_samples ||
         *current.total_samples_received < *previous.total_samples_received;
}

} // namespace

VoiceQualityResult VoiceQualityPolicy::evaluate(
    const VoiceQualitySnapshot &snapshot, bool remote_muted) {
  if (remote_muted) {
    if (complete(snapshot))
      previous_ = snapshot;
    else
      previous_.reset();
    return {.category = VoiceQualityCategory::muted};
  }
  if (!complete(snapshot)) {
    previous_.reset();
    return {};
  }
  if (!previous_) {
    previous_ = snapshot;
    return {};
  }

  if (regressed(snapshot, *previous_)) {
    previous_ = snapshot;
    return {.category = VoiceQualityCategory::poor};
  }

  const auto received_delta =
      *snapshot.packets_received - *previous_->packets_received;
  const auto lost_delta = *snapshot.packets_lost - *previous_->packets_lost;
  const auto concealed_delta =
      *snapshot.concealed_samples - *previous_->concealed_samples;
  const auto samples_delta =
      *snapshot.total_samples_received - *previous_->total_samples_received;
  previous_ = snapshot;

  const auto packet_total =
      received_delta + static_cast<std::uint64_t>(lost_delta);
  if (packet_total == 0 || samples_delta == 0)
    return {};

  const double loss_ratio = static_cast<double>(lost_delta) /
                            static_cast<double>(packet_total);
  const double concealment_ratio = static_cast<double>(concealed_delta) /
                                   static_cast<double>(samples_delta);
  VoiceQualityCategory category = VoiceQualityCategory::poor;
  if (loss_ratio <= 0.02 && *snapshot.jitter_ms <= 30.0 &&
      concealment_ratio <= 0.02) {
    category = VoiceQualityCategory::good;
  } else if (loss_ratio <= 0.05 && *snapshot.jitter_ms <= 60.0 &&
             concealment_ratio <= 0.05) {
    category = VoiceQualityCategory::unstable;
  }
  return {
      .category = category,
      .packet_loss_ratio = loss_ratio,
      .concealment_ratio = concealment_ratio,
  };
}

void VoiceQualityPolicy::reset() noexcept { previous_.reset(); }

} // namespace shareme::tools
