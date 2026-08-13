#include "session_lifecycle_policy.hpp"

#include "screen_capture_recovery_policy.hpp"

namespace shareme::tools {

SessionResumeDecision decide_session_resume(
    bool signaling_connected, bool peer_started, bool media_unavailable,
    bool capture_recovery_was_active,
    std::string_view capture_error) noexcept {
  if (!signaling_connected || !peer_started || media_unavailable)
    return SessionResumeDecision::connection_lost;
  if (capture_recovery_was_active ||
      is_recoverable_screen_capture_error(capture_error)) {
    return SessionResumeDecision::recover_capture;
  }
  return SessionResumeDecision::healthy;
}

bool SessionLifecyclePolicy::observe(SessionLifecycleEvent event) noexcept {
  switch (event) {
  case SessionLifecycleEvent::will_sleep:
    if (sleeping_)
      return false;
    begin_suspension_if_needed();
    sleeping_ = true;
    return true;
  case SessionLifecycleEvent::did_wake:
    if (!sleeping_)
      return false;
    sleeping_ = false;
    return true;
  case SessionLifecycleEvent::screen_locked:
    if (locked_)
      return false;
    begin_suspension_if_needed();
    locked_ = true;
    return true;
  case SessionLifecycleEvent::screen_unlocked:
    if (!locked_)
      return false;
    locked_ = false;
    return true;
  }
  return false;
}

bool SessionLifecyclePolicy::begin_evaluation() noexcept {
  if (state_ != SessionLifecycleState::suspended || sleeping_ || locked_)
    return false;
  state_ = SessionLifecycleState::evaluating;
  return true;
}

bool SessionLifecyclePolicy::record_recovered() noexcept {
  if (state_ != SessionLifecycleState::evaluating)
    return false;
  state_ = SessionLifecycleState::recovered;
  return true;
}

bool SessionLifecyclePolicy::record_failed() noexcept {
  if (state_ != SessionLifecycleState::evaluating)
    return false;
  state_ = SessionLifecycleState::failed;
  return true;
}

void SessionLifecyclePolicy::reset() noexcept {
  state_ = SessionLifecycleState::inactive;
  generation_ = 0;
  sleeping_ = false;
  locked_ = false;
}

SessionLifecycleState SessionLifecyclePolicy::state() const noexcept {
  return state_;
}

std::size_t SessionLifecyclePolicy::generation() const noexcept {
  return generation_;
}

bool SessionLifecyclePolicy::sleeping() const noexcept { return sleeping_; }

bool SessionLifecyclePolicy::locked() const noexcept { return locked_; }

bool SessionLifecyclePolicy::defers_capture_recovery() const noexcept {
  return state_ == SessionLifecycleState::suspended ||
         state_ == SessionLifecycleState::evaluating;
}

bool SessionLifecyclePolicy::capture_recovery_may_start(
    bool resume_authorized) const noexcept {
  return resume_authorized || !defers_capture_recovery();
}

void SessionLifecyclePolicy::begin_suspension_if_needed() noexcept {
  if (sleeping_ || locked_)
    return;
  ++generation_;
  state_ = SessionLifecycleState::suspended;
}

} // namespace shareme::tools
