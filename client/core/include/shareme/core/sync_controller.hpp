#pragma once

#include <cstdint>

namespace shareme::core {

enum class SyncAction {
  none,
  adjust_buffer,
  adjust_rate,
  hard_resync,
};

struct SyncDecision {
  SyncAction action;
  std::int64_t target_delay_delta_ms;
  double playback_rate;
};

class SyncController {
public:
  // Positive means the viewer is behind the host. The returned delay delta
  // keeps the same sign so the rendering owner can apply the correction.
  [[nodiscard]] SyncDecision decide(
      std::int64_t viewer_delta_ms) const noexcept;
};

}  // namespace shareme::core
