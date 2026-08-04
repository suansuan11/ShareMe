#pragma once

#include "shareme/core/sync_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shareme::core {

enum class DriftPhase {
  warmup,
  steady,
  paused,
  post_resume,
  post_forward_seek,
  post_backward_seek,
  cooldown,
};

struct DriftSample {
  static constexpr std::uint32_t kSchemaVersion = 1;

  std::uint32_t schema_version = kSchemaVersion;
  std::int64_t capture_time_ms = 0;
  std::uint64_t sample_index = 0;
  std::uint64_t report_sequence = 0;
  std::uint64_t generation = 0;
  std::int64_t host_pts_ms = 0;
  std::int64_t viewer_pts_ms = 0;
  std::int64_t delta_ms = 0;
  std::int64_t buffer_ms = 0;
  bool playing = false;
  SyncAction action = SyncAction::none;
  DriftPhase phase = DriftPhase::warmup;
  std::string selected_candidate_type;
};

struct DriftRecovery {
  DriftPhase phase = DriftPhase::warmup;
  bool complete = false;
  std::int64_t duration_ms = 0;
};

struct DriftSummary {
  bool complete = false;
  std::size_t accepted_samples = 0;
  std::size_t rejected_samples = 0;
  std::size_t sample_index_regressions = 0;
  std::size_t sequence_regressions = 0;
  std::size_t stale_generation_rejections = 0;
  std::size_t generation_transitions = 0;
  std::array<std::size_t, 7> phase_counts{};
  std::array<std::size_t, 4> action_counts{};
  std::int64_t signed_min_ms = 0;
  std::int64_t signed_max_ms = 0;
  double signed_mean_ms = 0.0;
  std::uint64_t absolute_p50_ms = 0;
  std::uint64_t absolute_p95_ms = 0;
  std::uint64_t absolute_p99_ms = 0;
  std::uint64_t absolute_max_ms = 0;
  std::int64_t first_capture_time_ms = 0;
  std::int64_t last_capture_time_ms = 0;
  std::int64_t covered_duration_ms = 0;
  std::size_t report_gap_count = 0;
  std::int64_t largest_report_gap_ms = 0;
  std::size_t hard_resync_candidate_episodes = 0;
  std::vector<DriftRecovery> recoveries;
};

class DriftAggregator {
public:
  [[nodiscard]] bool accept(DriftSample sample);
  void record_rejection() noexcept;
  void complete_run() noexcept;
  [[nodiscard]] DriftSummary summary() const;

private:
  struct RecoveryState {
    DriftPhase phase = DriftPhase::warmup;
    std::uint64_t generation = 0;
    std::int64_t start_time_ms = 0;
    std::size_t consecutive_good_samples = 0;
    std::size_t recovery_index = 0;
  };

  struct CandidateState {
    bool active = false;
    std::uint64_t generation = 0;
    std::int64_t start_time_ms = 0;
    std::int64_t last_time_ms = 0;
    std::size_t sample_count = 0;
  };

  [[nodiscard]] DriftSummary make_summary() const;
  void finish_candidate() noexcept;
  void update_recovery(const DriftSample& sample);

  bool complete_ = false;
  bool have_last_sample_ = false;
  std::uint64_t last_sample_index_ = 0;
  std::uint64_t last_sequence_ = 0;
  std::uint64_t current_generation_ = 0;
  DriftPhase last_phase_ = DriftPhase::warmup;
  std::int64_t first_capture_time_ms_ = 0;
  std::int64_t last_capture_time_ms_ = 0;
  std::int64_t signed_min_ms_ = 0;
  std::int64_t signed_max_ms_ = 0;
  long double signed_sum_ms_ = 0.0L;
  std::size_t accepted_samples_ = 0;
  std::size_t rejected_samples_ = 0;
  std::size_t sample_index_regressions_ = 0;
  std::size_t sequence_regressions_ = 0;
  std::size_t stale_generation_rejections_ = 0;
  std::size_t generation_transitions_ = 0;
  std::array<std::size_t, 7> phase_counts_{};
  std::array<std::size_t, 4> action_counts_{};
  std::vector<std::uint64_t> absolute_deltas_;
  std::size_t report_gap_count_ = 0;
  std::int64_t largest_report_gap_ms_ = 0;
  std::size_t hard_resync_candidate_episodes_ = 0;
  std::vector<DriftRecovery> recoveries_;
  RecoveryState recovery_{};
  bool recovery_active_ = false;
  CandidateState candidate_{};
};

}  // namespace shareme::core
