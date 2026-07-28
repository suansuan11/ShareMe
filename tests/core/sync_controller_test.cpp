#include "shareme/core/sync_controller.hpp"

#include <cmath>
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

void selects_actions_at_exact_boundaries() {
  using shareme::core::SyncAction;
  using shareme::core::SyncController;

  const SyncController controller;
  REQUIRE(controller.decide(49).action == SyncAction::none);
  REQUIRE(controller.decide(-49).action == SyncAction::none);
  REQUIRE(controller.decide(50).action == SyncAction::adjust_buffer);
  REQUIRE(controller.decide(-50).action == SyncAction::adjust_buffer);
  REQUIRE(controller.decide(119).action == SyncAction::adjust_buffer);
  REQUIRE(controller.decide(120).action == SyncAction::adjust_rate);
  REQUIRE(controller.decide(-120).action == SyncAction::adjust_rate);
  REQUIRE(controller.decide(299).action == SyncAction::adjust_rate);
  REQUIRE(controller.decide(300).action == SyncAction::hard_resync);
  REQUIRE(controller.decide(-300).action == SyncAction::hard_resync);
}

void preserves_correction_direction() {
  using shareme::core::SyncAction;
  using shareme::core::SyncController;

  const SyncController controller;
  const auto viewer_behind = controller.decide(180);
  const auto viewer_ahead = controller.decide(-180);

  REQUIRE(viewer_behind.action == SyncAction::adjust_rate);
  REQUIRE(viewer_behind.target_delay_delta_ms == 180);
  REQUIRE(viewer_behind.playback_rate >= 0.98);
  REQUIRE(viewer_behind.playback_rate < 1.0);

  REQUIRE(viewer_ahead.action == SyncAction::adjust_rate);
  REQUIRE(viewer_ahead.target_delay_delta_ms == -180);
  REQUIRE(viewer_ahead.playback_rate > 1.0);
  REQUIRE(viewer_ahead.playback_rate <= 1.02);
}

void uses_normal_rate_outside_rate_adjustment() {
  using shareme::core::SyncController;

  const SyncController controller;
  REQUIRE(std::abs(controller.decide(0).playback_rate - 1.0) < 0.000001);
  REQUIRE(std::abs(controller.decide(70).playback_rate - 1.0) < 0.000001);
  REQUIRE(std::abs(controller.decide(400).playback_rate - 1.0) < 0.000001);
}

}  // namespace

int main() {
  selects_actions_at_exact_boundaries();
  preserves_correction_direction();
  uses_normal_rate_outside_rate_adjustment();
  return EXIT_SUCCESS;
}
