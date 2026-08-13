#include "session_lifecycle_monitor.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <atomic>
#include <utility>

namespace shareme::tools {

std::optional<SessionLifecycleEvent>
session_lifecycle_event_for_notification(std::string_view name) noexcept {
  if (name == "NSWorkspaceWillSleepNotification")
    return SessionLifecycleEvent::will_sleep;
  if (name == "NSWorkspaceDidWakeNotification")
    return SessionLifecycleEvent::did_wake;
  if (name == "com.apple.screenIsLocked")
    return SessionLifecycleEvent::screen_locked;
  if (name == "com.apple.screenIsUnlocked")
    return SessionLifecycleEvent::screen_unlocked;
  return std::nullopt;
}

struct SessionLifecycleMonitor::State {
  Callback callback;
  std::atomic_bool accepting{false};

  [[nodiscard]] bool start(Callback next_callback) {
    bool expected = false;
    if (!accepting.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel))
      return false;
    callback = std::move(next_callback);
    if (!callback) {
      accepting.store(false, std::memory_order_release);
      return false;
    }
    return true;
  }

  void stop() noexcept {
    accepting.store(false, std::memory_order_release);
    callback = {};
  }

  [[nodiscard]] bool notify(SessionLifecycleEvent event) {
    if (!accepting.load(std::memory_order_acquire) || !callback)
      return false;
    callback(event);
    return true;
  }

  [[nodiscard]] static bool post(QCoreApplication *context,
                                 std::weak_ptr<State> weak_state,
                                 SessionLifecycleEvent event) {
    if (context == nullptr)
      return false;
    return QMetaObject::invokeMethod(
        context,
        [weak_state, event] {
          if (const auto state = weak_state.lock())
            static_cast<void>(state->notify(event));
        },
        Qt::QueuedConnection);
  }
};

#if !defined(SHAREME_HAS_NATIVE_SESSION_LIFECYCLE)
std::unique_ptr<SessionLifecycleNativeMonitor>
create_session_lifecycle_native_monitor(
    SessionLifecycleNativeMonitor::Callback callback) {
  static_cast<void>(callback);
  return {};
}
#endif

SessionLifecycleMonitor::SessionLifecycleMonitor()
    : state_{std::make_shared<State>()} {}

SessionLifecycleMonitor::~SessionLifecycleMonitor() { stop(); }

bool SessionLifecycleMonitor::start(Callback callback) {
  if (!state_->start(std::move(callback)))
    return false;

  const auto weak_state = std::weak_ptr<State>{state_};
  auto *context = QCoreApplication::instance();
  native_monitor_ = create_session_lifecycle_native_monitor(
      [weak_state, context](SessionLifecycleEvent event) {
        static_cast<void>(State::post(context, weak_state, event));
      });
  if (native_monitor_ && !native_monitor_->start()) {
    native_monitor_.reset();
    state_->stop();
    return false;
  }
  return true;
}

void SessionLifecycleMonitor::stop() noexcept {
  state_->stop();
  if (native_monitor_) {
    native_monitor_->stop();
    native_monitor_.reset();
  }
}

bool SessionLifecycleMonitor::accepting() const noexcept {
  return state_->accepting.load(std::memory_order_acquire);
}

bool SessionLifecycleMonitor::notify_for_test(SessionLifecycleEvent event) {
  return state_->notify(event);
}

bool SessionLifecycleMonitor::post_for_test(SessionLifecycleEvent event) {
  return State::post(QCoreApplication::instance(), std::weak_ptr<State>{state_},
                     event);
}

} // namespace shareme::tools
