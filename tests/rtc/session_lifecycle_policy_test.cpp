#include "session_lifecycle_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

using shareme::tools::SessionLifecycleEvent;
using shareme::tools::SessionLifecyclePolicy;
using shareme::tools::SessionResumeDecision;
using shareme::tools::SessionLifecycleState;
using shareme::tools::decide_session_resume;

void folds_nested_sleep_and_lock_into_one_generation() {
  SessionLifecyclePolicy policy;

  REQUIRE(policy.state() == SessionLifecycleState::inactive);
  REQUIRE(policy.observe(SessionLifecycleEvent::will_sleep));
  REQUIRE(policy.generation() == 1);
  REQUIRE(policy.sleeping());
  REQUIRE(!policy.locked());
  REQUIRE(policy.state() == SessionLifecycleState::suspended);

  REQUIRE(!policy.observe(SessionLifecycleEvent::will_sleep));
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(policy.generation() == 1);
  REQUIRE(policy.sleeping());
  REQUIRE(policy.locked());
}

void evaluates_only_after_every_nested_cause_clears() {
  SessionLifecyclePolicy wake_first;
  REQUIRE(wake_first.observe(SessionLifecycleEvent::will_sleep));
  REQUIRE(wake_first.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(wake_first.observe(SessionLifecycleEvent::did_wake));
  REQUIRE(!wake_first.begin_evaluation());
  REQUIRE(wake_first.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(wake_first.begin_evaluation());
  REQUIRE(!wake_first.begin_evaluation());
  REQUIRE(wake_first.state() == SessionLifecycleState::evaluating);

  SessionLifecyclePolicy unlock_first;
  REQUIRE(unlock_first.observe(SessionLifecycleEvent::will_sleep));
  REQUIRE(unlock_first.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(unlock_first.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(!unlock_first.begin_evaluation());
  REQUIRE(unlock_first.observe(SessionLifecycleEvent::did_wake));
  REQUIRE(unlock_first.begin_evaluation());
}

void rejects_stale_resume_and_records_one_terminal_result() {
  SessionLifecyclePolicy policy;
  REQUIRE(!policy.observe(SessionLifecycleEvent::did_wake));
  REQUIRE(!policy.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(!policy.begin_evaluation());

  REQUIRE(policy.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(policy.begin_evaluation());
  REQUIRE(policy.record_recovered());
  REQUIRE(!policy.record_recovered());
  REQUIRE(!policy.record_failed());
  REQUIRE(policy.state() == SessionLifecycleState::recovered);
}

void starts_a_new_generation_after_a_terminal_episode_and_resets() {
  SessionLifecyclePolicy policy;
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(policy.begin_evaluation());
  REQUIRE(policy.record_failed());
  REQUIRE(policy.state() == SessionLifecycleState::failed);

  REQUIRE(policy.observe(SessionLifecycleEvent::will_sleep));
  REQUIRE(policy.generation() == 2);
  REQUIRE(policy.state() == SessionLifecycleState::suspended);

  policy.reset();
  REQUIRE(policy.state() == SessionLifecycleState::inactive);
  REQUIRE(policy.generation() == 0);
  REQUIRE(!policy.sleeping());
  REQUIRE(!policy.locked());
}

void classifies_resume_without_rebuilding_a_healthy_call() {
  REQUIRE(decide_session_resume(true, true, false, false, "") ==
          SessionResumeDecision::healthy);
  REQUIRE(decide_session_resume(false, true, false, false, "") ==
          SessionResumeDecision::connection_lost);
  REQUIRE(decide_session_resume(true, false, false, false, "") ==
          SessionResumeDecision::connection_lost);
  REQUIRE(decide_session_resume(true, true, true, false, "") ==
          SessionResumeDecision::connection_lost);
  REQUIRE(decide_session_resume(true, true, false, true, "") ==
          SessionResumeDecision::recover_capture);
  REQUIRE(decide_session_resume(
              true, true, false, false, "screen-capture-stopped-native") ==
          SessionResumeDecision::recover_capture);
  REQUIRE(decide_session_resume(true, true, false, false,
                                "screen-capture-start-failed") ==
          SessionResumeDecision::healthy);
}

void defers_capture_recovery_until_resume_evaluation_authorizes_it() {
  SessionLifecyclePolicy policy;
  REQUIRE(!policy.defers_capture_recovery());
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_locked));
  REQUIRE(policy.defers_capture_recovery());
  REQUIRE(!policy.capture_recovery_may_start(false));
  REQUIRE(policy.capture_recovery_may_start(true));
  REQUIRE(policy.observe(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(policy.begin_evaluation());
  REQUIRE(policy.defers_capture_recovery());
  REQUIRE(!policy.capture_recovery_may_start(false));
  REQUIRE(policy.capture_recovery_may_start(true));
  REQUIRE(policy.record_recovered());
  REQUIRE(!policy.defers_capture_recovery());
  REQUIRE(policy.capture_recovery_may_start(false));
}

} // namespace

int main() {
  folds_nested_sleep_and_lock_into_one_generation();
  evaluates_only_after_every_nested_cause_clears();
  rejects_stale_resume_and_records_one_terminal_result();
  starts_a_new_generation_after_a_terminal_episode_and_resets();
  classifies_resume_without_rebuilding_a_healthy_call();
  defers_capture_recovery_until_resume_evaluation_authorizes_it();
  return EXIT_SUCCESS;
}
