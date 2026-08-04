#pragma once

#include "shareme/core/drift_metrics.hpp"

#include <cstdint>
#include <vector>

namespace shareme::tools {

enum class DriftScenarioAction {
  pause,
  resume,
  seek_forward,
  seek_backward,
  complete,
};

struct DriftScenarioEvent {
  DriftScenarioAction action;
  std::int64_t elapsed_ms;
  std::int64_t seek_delta_ms;
};

class DriftScenario final {
public:
  static constexpr std::int64_t kDurationMs = 300'000;

  [[nodiscard]] shareme::core::DriftPhase phase_at(
      std::int64_t elapsed_ms) const noexcept;
  [[nodiscard]] std::vector<DriftScenarioEvent> advance(
      std::int64_t elapsed_ms);
  [[nodiscard]] bool completed() const noexcept;

private:
  std::size_t next_event_index_ = 0;
  std::int64_t last_elapsed_ms_ = -1;
  bool completed_ = false;
};

[[nodiscard]] std::int64_t bounded_seek_target(
    std::int64_t current_pts_ms,
    std::int64_t delta_ms,
    std::int64_t start_pts_ms,
    std::int64_t end_pts_ms) noexcept;

[[nodiscard]] bool has_drift_study_duration(
    std::int64_t start_pts_ms,
    std::int64_t current_pts_ms,
    std::int64_t end_pts_ms) noexcept;

}  // namespace shareme::tools
