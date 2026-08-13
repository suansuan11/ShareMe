#pragma once

#include "session_lifecycle_policy.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace shareme::tools {

[[nodiscard]] std::optional<SessionLifecycleEvent>
session_lifecycle_event_for_notification(std::string_view name) noexcept;

class SessionLifecycleNativeMonitor {
public:
  using Callback = std::function<void(SessionLifecycleEvent)>;

  virtual ~SessionLifecycleNativeMonitor() = default;
  SessionLifecycleNativeMonitor(const SessionLifecycleNativeMonitor &) = delete;
  SessionLifecycleNativeMonitor &
  operator=(const SessionLifecycleNativeMonitor &) = delete;

  [[nodiscard]] virtual bool start() = 0;
  virtual void stop() noexcept = 0;

protected:
  SessionLifecycleNativeMonitor() = default;
};

[[nodiscard]] std::unique_ptr<SessionLifecycleNativeMonitor>
create_session_lifecycle_native_monitor(
    SessionLifecycleNativeMonitor::Callback callback);

class SessionLifecycleMonitor final {
public:
  using Callback = std::function<void(SessionLifecycleEvent)>;

  SessionLifecycleMonitor();
  ~SessionLifecycleMonitor();

  SessionLifecycleMonitor(const SessionLifecycleMonitor &) = delete;
  SessionLifecycleMonitor &operator=(const SessionLifecycleMonitor &) = delete;

  [[nodiscard]] bool start(Callback callback);
  void stop() noexcept;
  [[nodiscard]] bool accepting() const noexcept;

  [[nodiscard]] bool notify_for_test(SessionLifecycleEvent event);
  [[nodiscard]] bool post_for_test(SessionLifecycleEvent event);

private:
  struct State;

  std::shared_ptr<State> state_;
  std::unique_ptr<SessionLifecycleNativeMonitor> native_monitor_;
};

} // namespace shareme::tools
