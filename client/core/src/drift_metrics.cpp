#include "shareme/core/drift_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace shareme::core {
namespace {

constexpr std::size_t phase_index(DriftPhase phase) noexcept {
  return static_cast<std::size_t>(phase);
}

constexpr std::size_t action_index(SyncAction action) noexcept {
  return static_cast<std::size_t>(action);
}

std::uint64_t absolute_value(std::int64_t value) noexcept {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }

  return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

bool is_recovery_phase(DriftPhase phase) noexcept {
  return phase == DriftPhase::post_resume ||
      phase == DriftPhase::post_forward_seek ||
      phase == DriftPhase::post_backward_seek;
}

std::uint64_t nearest_rank(
    const std::vector<std::uint64_t>& sorted_values,
    std::uint32_t percentile) noexcept {
  if (sorted_values.empty()) {
    return 0;
  }

  const auto raw_rank = static_cast<std::size_t>(std::ceil(
      static_cast<long double>(sorted_values.size()) * percentile / 100.0L));
  const auto rank = std::max<std::size_t>(1, raw_rank);
  return sorted_values[std::min(rank, sorted_values.size()) - 1];
}

}  // namespace

bool DriftAggregator::accept(DriftSample sample) {
  if (complete_ || sample.schema_version != DriftSample::kSchemaVersion) {
    ++rejected_samples_;
    return false;
  }

  if (have_last_sample_ && sample.sample_index <= last_sample_index_) {
    ++sample_index_regressions_;
    ++rejected_samples_;
    return false;
  }

  if (have_last_sample_ && sample.report_sequence <= last_sequence_) {
    ++sequence_regressions_;
    ++rejected_samples_;
    return false;
  }

  if (have_last_sample_ && sample.generation < current_generation_) {
    ++stale_generation_rejections_;
    ++rejected_samples_;
    return false;
  }

  if (!have_last_sample_) {
    first_capture_time_ms_ = sample.capture_time_ms;
    signed_min_ms_ = sample.delta_ms;
    signed_max_ms_ = sample.delta_ms;
  } else {
    if (sample.generation > current_generation_) {
      ++generation_transitions_;
    }

    const auto gap_ms = sample.capture_time_ms - last_capture_time_ms_;
    if (gap_ms > 250 && sample.phase != DriftPhase::paused &&
        last_phase_ != DriftPhase::paused) {
      ++report_gap_count_;
      largest_report_gap_ms_ = std::max(largest_report_gap_ms_, gap_ms);
    }
    signed_min_ms_ = std::min(signed_min_ms_, sample.delta_ms);
    signed_max_ms_ = std::max(signed_max_ms_, sample.delta_ms);
  }

  have_last_sample_ = true;
  last_sample_index_ = sample.sample_index;
  last_sequence_ = sample.report_sequence;
  current_generation_ = sample.generation;
  last_phase_ = sample.phase;
  last_capture_time_ms_ = sample.capture_time_ms;
  signed_sum_ms_ += static_cast<long double>(sample.delta_ms);
  ++accepted_samples_;
  ++phase_counts_[phase_index(sample.phase)];
  ++action_counts_[action_index(sample.action)];
  absolute_deltas_.push_back(absolute_value(sample.delta_ms));

  update_recovery(sample);

  if (sample.playing && sample.action == SyncAction::hard_resync) {
    if (!candidate_.active || candidate_.generation != sample.generation) {
      finish_candidate();
      candidate_ = {
          true,
          sample.generation,
          sample.capture_time_ms,
          sample.capture_time_ms,
          1,
      };
    } else {
      candidate_.last_time_ms = sample.capture_time_ms;
      ++candidate_.sample_count;
    }
  } else {
    finish_candidate();
  }

  return true;
}

void DriftAggregator::complete_run() noexcept {
  complete_ = true;
  finish_candidate();
}

DriftSummary DriftAggregator::summary() const {
  return make_summary();
}

DriftSummary DriftAggregator::make_summary() const {
  DriftSummary result;
  result.complete = complete_;
  result.accepted_samples = accepted_samples_;
  result.rejected_samples = rejected_samples_;
  result.sample_index_regressions = sample_index_regressions_;
  result.sequence_regressions = sequence_regressions_;
  result.stale_generation_rejections = stale_generation_rejections_;
  result.generation_transitions = generation_transitions_;
  result.phase_counts = phase_counts_;
  result.action_counts = action_counts_;
  result.signed_min_ms = signed_min_ms_;
  result.signed_max_ms = signed_max_ms_;
  result.signed_mean_ms = accepted_samples_ == 0
      ? 0.0
      : static_cast<double>(
            signed_sum_ms_ / static_cast<long double>(accepted_samples_));
  result.first_capture_time_ms = first_capture_time_ms_;
  result.last_capture_time_ms = last_capture_time_ms_;
  result.covered_duration_ms = accepted_samples_ == 0
      ? 0
      : last_capture_time_ms_ - first_capture_time_ms_;
  result.report_gap_count = report_gap_count_;
  result.largest_report_gap_ms = largest_report_gap_ms_;
  result.hard_resync_candidate_episodes = hard_resync_candidate_episodes_;
  result.recoveries = recoveries_;

  auto sorted_deltas = absolute_deltas_;
  std::sort(sorted_deltas.begin(), sorted_deltas.end());
  result.absolute_p50_ms = nearest_rank(sorted_deltas, 50);
  result.absolute_p95_ms = nearest_rank(sorted_deltas, 95);
  result.absolute_p99_ms = nearest_rank(sorted_deltas, 99);
  if (!sorted_deltas.empty()) {
    result.absolute_max_ms = sorted_deltas.back();
  }

  if (candidate_.active && candidate_.sample_count >= 4 &&
      candidate_.last_time_ms - candidate_.start_time_ms >= 750) {
    ++result.hard_resync_candidate_episodes;
  }

  if (recovery_active_ && recovery_.recovery_index >= result.recoveries.size()) {
    result.recoveries.push_back(
        {recovery_.phase, false, 0});
  }

  return result;
}

void DriftAggregator::finish_candidate() noexcept {
  if (candidate_.active && candidate_.sample_count >= 4 &&
      candidate_.last_time_ms - candidate_.start_time_ms >= 750) {
    ++hard_resync_candidate_episodes_;
  }
  candidate_ = {};
}

void DriftAggregator::update_recovery(const DriftSample& sample) {
  if (!is_recovery_phase(sample.phase)) {
    return;
  }

  if (!recovery_active_ || recovery_.phase != sample.phase) {
    recovery_ = {
        sample.phase,
        sample.generation,
        sample.capture_time_ms,
        0,
        recoveries_.size(),
    };
    recoveries_.push_back({sample.phase, false, 0});
    recovery_active_ = true;
  } else if (recovery_.generation != sample.generation) {
    recovery_.generation = sample.generation;
    recovery_.start_time_ms = sample.capture_time_ms;
    recovery_.consecutive_good_samples = 0;
  }

  if (absolute_value(sample.delta_ms) <= 100) {
    ++recovery_.consecutive_good_samples;
  } else {
    recovery_.consecutive_good_samples = 0;
  }

  if (!recoveries_[recovery_.recovery_index].complete &&
      recovery_.consecutive_good_samples >= 3) {
    recoveries_[recovery_.recovery_index] = {
        sample.phase,
        true,
        sample.capture_time_ms - recovery_.start_time_ms,
    };
  }
}

}  // namespace shareme::core
