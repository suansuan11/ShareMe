#include "drift_scenario.hpp"

#include <algorithm>
#include <array>

namespace shareme::tools {
namespace {

constexpr std::array<DriftScenarioEvent, 5> kEvents = {{
    {DriftScenarioAction::pause, 90'000, 0},
    {DriftScenarioAction::resume, 95'000, 0},
    {DriftScenarioAction::seek_forward, 150'000, 60'000},
    {DriftScenarioAction::seek_backward, 210'000, -30'000},
    {DriftScenarioAction::complete, DriftScenario::kDurationMs, 0},
}};

}  // namespace

shareme::core::DriftPhase DriftScenario::phase_at(
    std::int64_t elapsed_ms) const noexcept {
  if (elapsed_ms < 30'000)
    return shareme::core::DriftPhase::warmup;
  if (elapsed_ms < 90'000)
    return shareme::core::DriftPhase::steady;
  if (elapsed_ms < 95'000)
    return shareme::core::DriftPhase::paused;
  if (elapsed_ms < 150'000)
    return shareme::core::DriftPhase::post_resume;
  if (elapsed_ms < 210'000)
    return shareme::core::DriftPhase::post_forward_seek;
  if (elapsed_ms < DriftScenario::kDurationMs)
    return shareme::core::DriftPhase::post_backward_seek;
  return shareme::core::DriftPhase::cooldown;
}

std::vector<DriftScenarioEvent> DriftScenario::advance(
    std::int64_t elapsed_ms) {
  if (completed_ || elapsed_ms < last_elapsed_ms_)
    return {};
  last_elapsed_ms_ = elapsed_ms;

  std::vector<DriftScenarioEvent> events;
  while (next_event_index_ < kEvents.size() &&
         kEvents[next_event_index_].elapsed_ms <= elapsed_ms) {
    events.push_back(kEvents[next_event_index_]);
    if (events.back().action == DriftScenarioAction::complete)
      completed_ = true;
    ++next_event_index_;
  }
  return events;
}

bool DriftScenario::completed() const noexcept { return completed_; }

std::int64_t bounded_seek_target(
    std::int64_t current_pts_ms,
    std::int64_t delta_ms,
    std::int64_t start_pts_ms,
    std::int64_t end_pts_ms) noexcept {
  if (end_pts_ms <= start_pts_ms)
    return start_pts_ms;
  const auto candidate = static_cast<__int128>(current_pts_ms) + delta_ms;
  if (candidate <= start_pts_ms)
    return start_pts_ms;
  if (candidate >= end_pts_ms)
    return end_pts_ms;
  return static_cast<std::int64_t>(candidate);
}

bool has_drift_study_duration(
    std::int64_t start_pts_ms,
    std::int64_t current_pts_ms,
    std::int64_t end_pts_ms) noexcept {
  if (current_pts_ms < start_pts_ms || end_pts_ms < current_pts_ms)
    return false;
  return static_cast<__int128>(end_pts_ms) - current_pts_ms >= 330'000;
}

}  // namespace shareme::tools
