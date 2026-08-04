#include "shareme/core/drift_metrics.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::core::DriftSample sample(
    std::int64_t capture_time_ms,
    std::uint64_t sample_index,
    std::uint64_t report_sequence,
    std::uint64_t generation,
    std::int64_t delta_ms,
    shareme::core::DriftPhase phase = shareme::core::DriftPhase::steady,
    shareme::core::SyncAction action = shareme::core::SyncAction::none) {
  return {
      .capture_time_ms = capture_time_ms,
      .sample_index = sample_index,
      .report_sequence = report_sequence,
      .generation = generation,
      .host_pts_ms = 10'000,
      .viewer_pts_ms = 10'000,
      .delta_ms = delta_ms,
      .buffer_ms = 80,
      .playing = phase != shareme::core::DriftPhase::paused,
      .action = action,
      .phase = phase,
      .selected_candidate_type = "host",
  };
}

void reports_empty_and_complete_runs_deterministically() {
  shareme::core::DriftAggregator aggregator;

  const auto before = aggregator.summary();
  REQUIRE(before.accepted_samples == 0);
  REQUIRE(before.rejected_samples == 0);
  REQUIRE(!before.complete);

  aggregator.complete_run();
  const auto after = aggregator.summary();
  REQUIRE(after.complete);
  REQUIRE(after.absolute_p50_ms == 0);
  REQUIRE(after.absolute_p99_ms == 0);
}

void aggregates_signed_values_actions_phases_and_percentiles() {
  shareme::core::DriftAggregator aggregator;
  REQUIRE(aggregator.accept(sample(0, 0, 0, 1, -400,
                                  shareme::core::DriftPhase::warmup)));
  REQUIRE(aggregator.accept(sample(250, 1, 1, 1, 20)));
  REQUIRE(aggregator.accept(sample(500, 2, 2, 1, 80)));
  REQUIRE(aggregator.accept(sample(750, 3, 3, 1, 120,
                                  shareme::core::DriftPhase::steady,
                                  shareme::core::SyncAction::adjust_buffer)));
  REQUIRE(aggregator.accept(sample(1'000, 4, 4, 1, 300,
                                  shareme::core::DriftPhase::post_resume,
                                  shareme::core::SyncAction::adjust_rate)));
  REQUIRE(aggregator.accept(sample(1'250, 5, 5, 1, -101,
                                  shareme::core::DriftPhase::post_resume)));
  REQUIRE(aggregator.accept(sample(1'500, 6, 6, 1, 100,
                                  shareme::core::DriftPhase::post_resume,
                                  shareme::core::SyncAction::hard_resync)));
  REQUIRE(aggregator.accept(sample(1'750, 7, 7, 1, 100,
                                  shareme::core::DriftPhase::post_resume,
                                  shareme::core::SyncAction::hard_resync)));
  REQUIRE(aggregator.accept(sample(2'000, 8, 8, 1, 100,
                                  shareme::core::DriftPhase::steady,
                                  shareme::core::SyncAction::hard_resync)));

  const auto summary = aggregator.summary();
  REQUIRE(summary.accepted_samples == 9);
  REQUIRE(summary.signed_min_ms == -400);
  REQUIRE(summary.signed_max_ms == 300);
  REQUIRE(summary.signed_mean_ms == 35.44444444444444);
  REQUIRE(summary.absolute_p50_ms == 100);
  REQUIRE(summary.absolute_p95_ms == 400);
  REQUIRE(summary.absolute_p99_ms == 400);
  REQUIRE(summary.absolute_max_ms == 400);
  REQUIRE(summary.phase_counts[static_cast<std::size_t>(
              shareme::core::DriftPhase::warmup)] == 1);
  REQUIRE(summary.phase_counts[static_cast<std::size_t>(
              shareme::core::DriftPhase::post_resume)] == 4);
  REQUIRE(summary.action_counts[static_cast<std::size_t>(
              shareme::core::SyncAction::hard_resync)] == 3);
  REQUIRE(summary.recoveries.size() == 1);
  REQUIRE(!summary.recoveries.front().complete);
}

void rejects_regressions_counts_generation_changes_and_excludes_pause_gap() {
  shareme::core::DriftAggregator aggregator;
  REQUIRE(aggregator.accept(sample(0, 0, 0, 1, 0)));
  REQUIRE(aggregator.accept(sample(250, 1, 1, 1, 0)));
  REQUIRE(aggregator.accept(sample(500, 2, 2, 1, 0,
                                  shareme::core::DriftPhase::paused)));
  REQUIRE(aggregator.accept(sample(5'500, 3, 3, 2, 0,
                                  shareme::core::DriftPhase::post_resume)));
  REQUIRE(!aggregator.accept(sample(5'750, 4, 3, 2, 0)));
  REQUIRE(!aggregator.accept(sample(6'000, 3, 4, 2, 0)));
  REQUIRE(!aggregator.accept(sample(6'250, 5, 5, 1, 0)));
  REQUIRE(aggregator.accept(sample(8'750, 6, 6, 2, 0)));

  const auto summary = aggregator.summary();
  REQUIRE(summary.accepted_samples == 5);
  REQUIRE(summary.rejected_samples == 3);
  REQUIRE(summary.generation_transitions == 1);
  REQUIRE(summary.stale_generation_rejections == 1);
  REQUIRE(summary.sample_index_regressions == 1);
  REQUIRE(summary.sequence_regressions == 1);
  REQUIRE(summary.largest_report_gap_ms == 3'250);
  REQUIRE(summary.report_gap_count == 1);
}

void recovers_only_after_three_consecutive_current_generation_samples() {
  shareme::core::DriftAggregator aggregator;
  REQUIRE(aggregator.accept(sample(1'000, 0, 0, 1, 250,
                                  shareme::core::DriftPhase::post_forward_seek)));
  REQUIRE(aggregator.accept(sample(1'250, 1, 1, 1, 100,
                                  shareme::core::DriftPhase::post_forward_seek)));
  REQUIRE(aggregator.accept(sample(1'500, 2, 2, 1, 99,
                                  shareme::core::DriftPhase::post_forward_seek)));
  REQUIRE(aggregator.accept(sample(1'750, 3, 3, 1, 100,
                                  shareme::core::DriftPhase::post_forward_seek)));

  const auto summary = aggregator.summary();
  REQUIRE(summary.recoveries.size() == 1);
  REQUIRE(summary.recoveries.front().complete);
  REQUIRE(summary.recoveries.front().duration_ms == 750);

  shareme::core::DriftAggregator incomplete;
  REQUIRE(incomplete.accept(sample(0, 0, 0, 1, 101,
                                   shareme::core::DriftPhase::post_backward_seek)));
  REQUIRE(incomplete.accept(sample(250, 1, 1, 1, 100,
                                   shareme::core::DriftPhase::post_backward_seek)));
  REQUIRE(incomplete.accept(sample(500, 2, 2, 1, 101,
                                   shareme::core::DriftPhase::post_backward_seek)));
  incomplete.complete_run();
  REQUIRE(incomplete.summary().recoveries.size() == 1);
  REQUIRE(!incomplete.summary().recoveries.front().complete);
}

void computes_overflow_safe_absolute_value_and_nearest_rank_percentiles() {
  shareme::core::DriftAggregator aggregator;
  const auto minimum = std::numeric_limits<std::int64_t>::min();
  REQUIRE(aggregator.accept(sample(0, 0, 0, 1, minimum)));
  REQUIRE(aggregator.accept(sample(250, 1, 1, 1, 1)));
  REQUIRE(aggregator.accept(sample(500, 2, 2, 1, 2)));
  REQUIRE(aggregator.accept(sample(750, 3, 3, 1, 3)));
  REQUIRE(aggregator.accept(sample(1'000, 4, 4, 1, 4)));

  const auto summary = aggregator.summary();
  REQUIRE(summary.absolute_p50_ms == 3);
  const auto minimum_magnitude = static_cast<std::uint64_t>(1) << 63;
  REQUIRE(summary.absolute_p95_ms == minimum_magnitude);
  REQUIRE(summary.absolute_p99_ms == minimum_magnitude);
  REQUIRE(summary.absolute_max_ms == minimum_magnitude);
}

void counts_samples_rejected_by_an_external_capture_bound() {
  shareme::core::DriftAggregator aggregator;
  aggregator.record_rejection();
  REQUIRE(aggregator.summary().rejected_samples == 1);
}

void retains_failure_categories_for_the_measurement_gate() {
  shareme::core::DriftAggregator aggregator;
  aggregator.record_error("rtc-failure");
  aggregator.record_error("native-audio-failure");
  const auto summary = aggregator.summary();
  REQUIRE(summary.errors.size() == 2);
  REQUIRE(summary.errors.front() == "rtc-failure");
  REQUIRE(summary.errors.back() == "native-audio-failure");
}

}  // namespace

int main() {
  reports_empty_and_complete_runs_deterministically();
  aggregates_signed_values_actions_phases_and_percentiles();
  rejects_regressions_counts_generation_changes_and_excludes_pause_gap();
  recovers_only_after_three_consecutive_current_generation_samples();
  computes_overflow_safe_absolute_value_and_nearest_rank_percentiles();
  counts_samples_rejected_by_an_external_capture_bound();
  retains_failure_categories_for_the_measurement_gate();
  return EXIT_SUCCESS;
}
