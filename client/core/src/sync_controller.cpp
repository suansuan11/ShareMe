#include "shareme/core/sync_controller.hpp"

#include <algorithm>
#include <cstdint>

namespace shareme::core {
namespace {

[[nodiscard]] std::uint64_t magnitude(std::int64_t value) noexcept {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }

  return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

}  // namespace

SyncDecision SyncController::decide(std::int64_t viewer_delta_ms) const noexcept {
  const auto absolute_delta_ms = magnitude(viewer_delta_ms);
  if (absolute_delta_ms < 50) {
    return {SyncAction::none, viewer_delta_ms, 1.0};
  }
  if (absolute_delta_ms < 120) {
    return {SyncAction::adjust_buffer, viewer_delta_ms, 1.0};
  }
  if (absolute_delta_ms < 300) {
    const auto correction =
        std::clamp(static_cast<double>(absolute_delta_ms) / 15000.0, 0.0, 0.02);
    const auto playback_rate =
        viewer_delta_ms > 0 ? 1.0 - correction : 1.0 + correction;
    return {SyncAction::adjust_rate, viewer_delta_ms, playback_rate};
  }

  return {SyncAction::hard_resync, viewer_delta_ms, 1.0};
}

}  // namespace shareme::core
