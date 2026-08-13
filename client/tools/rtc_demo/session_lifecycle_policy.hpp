#pragma once

#include <cstddef>
#include <string_view>

namespace shareme::tools {

enum class SessionLifecycleEvent {
  will_sleep,
  did_wake,
  screen_locked,
  screen_unlocked,
};

enum class SessionLifecycleState {
  inactive,
  suspended,
  evaluating,
  recovered,
  failed,
};

enum class SessionResumeDecision {
  healthy,
  recover_capture,
  connection_lost,
};

[[nodiscard]] SessionResumeDecision decide_session_resume(
    bool signaling_connected, bool peer_started, bool media_unavailable,
    bool capture_recovery_was_active,
    std::string_view capture_error) noexcept;

class SessionLifecyclePolicy {
public:
  [[nodiscard]] bool observe(SessionLifecycleEvent event) noexcept;
  [[nodiscard]] bool begin_evaluation() noexcept;
  [[nodiscard]] bool record_recovered() noexcept;
  [[nodiscard]] bool record_failed() noexcept;
  void reset() noexcept;

  [[nodiscard]] SessionLifecycleState state() const noexcept;
  [[nodiscard]] std::size_t generation() const noexcept;
  [[nodiscard]] bool sleeping() const noexcept;
  [[nodiscard]] bool locked() const noexcept;

private:
  void begin_suspension_if_needed() noexcept;

  SessionLifecycleState state_{SessionLifecycleState::inactive};
  std::size_t generation_{0};
  bool sleeping_{false};
  bool locked_{false};
};

} // namespace shareme::tools
