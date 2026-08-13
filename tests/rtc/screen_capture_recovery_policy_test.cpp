#include "screen_capture_recovery_policy.hpp"

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

using shareme::tools::ScreenCaptureRecoveryPolicy;
using shareme::tools::ScreenCaptureRecoveryState;

void recovers_on_the_first_bounded_attempt() {
  ScreenCaptureRecoveryPolicy policy;
  REQUIRE(policy.state() == ScreenCaptureRecoveryState::inactive);
  REQUIRE(policy.attempt() == 0);
  REQUIRE(!policy.delay_ms().has_value());

  REQUIRE(policy.begin());
  REQUIRE(!policy.begin());
  REQUIRE(policy.state() == ScreenCaptureRecoveryState::waiting);
  REQUIRE(policy.delay_ms() == std::optional<int>{250});

  REQUIRE(policy.begin_attempt());
  REQUIRE(policy.attempt() == 1);
  REQUIRE(policy.state() == ScreenCaptureRecoveryState::attempting);
  REQUIRE(!policy.begin_attempt());

  REQUIRE(policy.record_success());
  REQUIRE(policy.state() == ScreenCaptureRecoveryState::recovered);
  REQUIRE(!policy.record_failure());
  REQUIRE(!policy.delay_ms().has_value());
}

void exhausts_after_three_failures_with_fixed_backoff() {
  ScreenCaptureRecoveryPolicy policy;
  REQUIRE(policy.begin());

  const int expected_delays[] = {250, 500, 1000};
  for (std::size_t expected_attempt = 1; expected_attempt <= 3;
       ++expected_attempt) {
    REQUIRE(policy.delay_ms() ==
            std::optional<int>{expected_delays[expected_attempt - 1]});
    REQUIRE(policy.begin_attempt());
    REQUIRE(policy.attempt() == expected_attempt);
    REQUIRE(policy.record_failure());
  }

  REQUIRE(policy.state() == ScreenCaptureRecoveryState::exhausted);
  REQUIRE(policy.attempt() == 3);
  REQUIRE(!policy.delay_ms().has_value());
  REQUIRE(!policy.begin_attempt());
  REQUIRE(!policy.record_failure());
}

void reset_starts_a_fresh_episode() {
  ScreenCaptureRecoveryPolicy policy;
  REQUIRE(policy.begin());
  REQUIRE(policy.begin_attempt());
  REQUIRE(policy.record_failure());
  REQUIRE(policy.attempt() == 1);

  policy.reset();
  REQUIRE(policy.state() == ScreenCaptureRecoveryState::inactive);
  REQUIRE(policy.attempt() == 0);
  REQUIRE(policy.begin());
  REQUIRE(policy.delay_ms() == std::optional<int>{250});
}

} // namespace

int main() {
  recovers_on_the_first_bounded_attempt();
  exhausts_after_three_failures_with_fixed_backoff();
  reset_starts_a_fresh_episode();
  return EXIT_SUCCESS;
}
