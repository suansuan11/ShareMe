#include "voice_quality_policy.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace {
void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

using shareme::tools::VoiceQualityCategory;
using shareme::tools::VoiceQualityPolicy;
using shareme::tools::VoiceQualitySnapshot;

VoiceQualitySnapshot snapshot(std::uint64_t received, std::int64_t lost,
                              std::uint64_t concealed,
                              std::uint64_t total_samples,
                              double jitter_ms) {
  return {
      .packets_received = received,
      .packets_lost = lost,
      .concealed_samples = concealed,
      .total_samples_received = total_samples,
      .jitter_ms = jitter_ms,
  };
}

void first_complete_snapshot_is_checking() {
  VoiceQualityPolicy policy;
  const auto result = policy.evaluate(snapshot(100, 2, 960, 48'000, 12.0));
  REQUIRE(result.category == VoiceQualityCategory::checking);
  REQUIRE(!result.packet_loss_ratio.has_value());
  REQUIRE(!result.concealment_ratio.has_value());
}

void exact_thresholds_are_inclusive() {
  VoiceQualityPolicy policy;
  static_cast<void>(policy.evaluate(snapshot(100, 0, 0, 48'000, 10.0)));

  const auto good = policy.evaluate(snapshot(198, 2, 960, 96'000, 30.0));
  REQUIRE(good.category == VoiceQualityCategory::good);
  REQUIRE(std::abs(*good.packet_loss_ratio - 0.02) < 0.000001);
  REQUIRE(std::abs(*good.concealment_ratio - 0.02) < 0.000001);

  const auto unstable =
      policy.evaluate(snapshot(293, 7, 3'360, 144'000, 60.0));
  REQUIRE(unstable.category == VoiceQualityCategory::unstable);
  REQUIRE(std::abs(*unstable.packet_loss_ratio - 0.05) < 0.000001);
  REQUIRE(std::abs(*unstable.concealment_ratio - 0.05) < 0.000001);

  const auto poor =
      policy.evaluate(snapshot(387, 13, 6'240, 192'000, 60.1));
  REQUIRE(poor.category == VoiceQualityCategory::poor);
}

void missing_or_zero_interval_never_claims_good() {
  VoiceQualityPolicy policy;
  VoiceQualitySnapshot incomplete;
  incomplete.packets_received = 10;
  REQUIRE(policy.evaluate(incomplete).category ==
          VoiceQualityCategory::checking);

  static_cast<void>(policy.evaluate(snapshot(10, 0, 0, 4'800, 5.0)));
  REQUIRE(policy.evaluate(snapshot(10, 0, 0, 4'800, 5.0)).category ==
          VoiceQualityCategory::checking);
}

void regression_fails_closed_and_reset_discards_history() {
  VoiceQualityPolicy policy;
  static_cast<void>(policy.evaluate(snapshot(100, 3, 100, 48'000, 5.0)));
  REQUIRE(policy.evaluate(snapshot(99, 3, 100, 48'000, 5.0)).category ==
          VoiceQualityCategory::poor);

  policy.reset();
  REQUIRE(policy.evaluate(snapshot(99, 3, 100, 48'000, 5.0)).category ==
          VoiceQualityCategory::checking);
}

void remote_mute_has_priority_without_advancing_baseline() {
  VoiceQualityPolicy policy;
  REQUIRE(policy.evaluate(snapshot(100, 0, 0, 48'000, 5.0), true).category ==
          VoiceQualityCategory::muted);
  REQUIRE(policy.evaluate(snapshot(200, 0, 0, 96'000, 5.0)).category ==
          VoiceQualityCategory::checking);
}
} // namespace

int main() {
  first_complete_snapshot_is_checking();
  exact_thresholds_are_inclusive();
  missing_or_zero_interval_never_claims_good();
  regression_fails_closed_and_reset_discards_history();
  remote_mute_has_priority_without_advancing_baseline();
  return EXIT_SUCCESS;
}
