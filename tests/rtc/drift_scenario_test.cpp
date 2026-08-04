#include "drift_scenario.hpp"

#include <cstdlib>
#include <iostream>

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

void reports_frozen_profile_boundaries_and_actions_once() {
  shareme::tools::DriftScenario scenario;
  REQUIRE(scenario.phase_at(0) == shareme::core::DriftPhase::warmup);
  REQUIRE(scenario.phase_at(29'999) == shareme::core::DriftPhase::warmup);
  REQUIRE(scenario.phase_at(30'000) == shareme::core::DriftPhase::steady);
  REQUIRE(scenario.phase_at(89'999) == shareme::core::DriftPhase::steady);
  REQUIRE(scenario.phase_at(90'000) == shareme::core::DriftPhase::paused);
  REQUIRE(scenario.phase_at(94'999) == shareme::core::DriftPhase::paused);
  REQUIRE(scenario.phase_at(95'000) == shareme::core::DriftPhase::post_resume);
  REQUIRE(scenario.phase_at(149'999) == shareme::core::DriftPhase::post_resume);
  REQUIRE(scenario.phase_at(150'000) ==
          shareme::core::DriftPhase::post_forward_seek);
  REQUIRE(scenario.phase_at(209'999) ==
          shareme::core::DriftPhase::post_forward_seek);
  REQUIRE(scenario.phase_at(210'000) ==
          shareme::core::DriftPhase::post_backward_seek);
  REQUIRE(scenario.phase_at(299'999) ==
          shareme::core::DriftPhase::post_backward_seek);
  REQUIRE(scenario.phase_at(300'000) == shareme::core::DriftPhase::cooldown);

  REQUIRE(scenario.advance(89'999).empty());
  const auto pause = scenario.advance(90'000);
  REQUIRE(pause.size() == 1);
  REQUIRE(pause.front().action == shareme::tools::DriftScenarioAction::pause);
  REQUIRE(pause.front().elapsed_ms == 90'000);
  REQUIRE(scenario.advance(90'000).empty());
  REQUIRE(scenario.advance(94'999).empty());

  const auto resume = scenario.advance(95'000);
  REQUIRE(resume.size() == 1);
  REQUIRE(resume.front().action == shareme::tools::DriftScenarioAction::resume);
  REQUIRE(resume.front().elapsed_ms == 95'000);

  const auto forward = scenario.advance(150'000);
  REQUIRE(forward.size() == 1);
  REQUIRE(forward.front().action ==
          shareme::tools::DriftScenarioAction::seek_forward);
  REQUIRE(forward.front().seek_delta_ms == 60'000);
  REQUIRE(scenario.advance(150'000).empty());

  const auto backward = scenario.advance(210'000);
  REQUIRE(backward.size() == 1);
  REQUIRE(backward.front().action ==
          shareme::tools::DriftScenarioAction::seek_backward);
  REQUIRE(backward.front().seek_delta_ms == -30'000);

  const auto complete = scenario.advance(300'000);
  REQUIRE(complete.size() == 1);
  REQUIRE(complete.front().action == shareme::tools::DriftScenarioAction::complete);
  REQUIRE(scenario.completed());
  REQUIRE(scenario.advance(301'000).empty());
}

void bounds_seek_targets_and_requires_three_hundred_thirty_seconds_remaining() {
  REQUIRE(shareme::tools::bounded_seek_target(100, 60, 0, 150) == 150);
  REQUIRE(shareme::tools::bounded_seek_target(20, -60, 0, 150) == 0);
  REQUIRE(shareme::tools::bounded_seek_target(80, 30, 0, 150) == 110);
  REQUIRE(shareme::tools::has_drift_study_duration(0, 0, 330'000));
  REQUIRE(!shareme::tools::has_drift_study_duration(0, 1, 330'000));
  REQUIRE(!shareme::tools::has_drift_study_duration(0, 0, 329'999));
}

}  // namespace

int main() {
  reports_frozen_profile_boundaries_and_actions_once();
  bounds_seek_targets_and_requires_three_hundred_thirty_seconds_remaining();
  return EXIT_SUCCESS;
}
