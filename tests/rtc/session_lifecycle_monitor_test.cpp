#include "session_lifecycle_monitor.hpp"

#include <QCoreApplication>
#include <QEventLoop>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

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
using shareme::tools::SessionLifecycleMonitor;
using shareme::tools::session_lifecycle_event_for_notification;

void translates_only_the_four_owned_native_notifications() {
  REQUIRE(session_lifecycle_event_for_notification(
              "NSWorkspaceWillSleepNotification") ==
          std::optional{SessionLifecycleEvent::will_sleep});
  REQUIRE(session_lifecycle_event_for_notification(
              "NSWorkspaceDidWakeNotification") ==
          std::optional{SessionLifecycleEvent::did_wake});
  REQUIRE(session_lifecycle_event_for_notification(
              "com.apple.screenIsLocked") ==
          std::optional{SessionLifecycleEvent::screen_locked});
  REQUIRE(session_lifecycle_event_for_notification(
              "com.apple.screenIsUnlocked") ==
          std::optional{SessionLifecycleEvent::screen_unlocked});
  REQUIRE(!session_lifecycle_event_for_notification("").has_value());
  REQUIRE(!session_lifecycle_event_for_notification(
               "NSWorkspaceScreensDidSleepNotification")
               .has_value());
}

void start_is_single_owner_and_stop_rejects_late_events() {
  SessionLifecycleMonitor monitor;
  std::vector<SessionLifecycleEvent> observed;

  REQUIRE(monitor.start(
      [&observed](SessionLifecycleEvent event) { observed.push_back(event); }));
  REQUIRE(!monitor.start([](SessionLifecycleEvent) {}));
  REQUIRE(monitor.accepting());
  REQUIRE(monitor.notify_for_test(SessionLifecycleEvent::screen_locked));
  REQUIRE(observed ==
          std::vector{SessionLifecycleEvent::screen_locked});

  monitor.stop();
  REQUIRE(!monitor.accepting());
  REQUIRE(!monitor.notify_for_test(SessionLifecycleEvent::screen_unlocked));
  REQUIRE(observed ==
          std::vector{SessionLifecycleEvent::screen_locked});
}

void queued_event_is_dropped_when_shutdown_wins() {
  SessionLifecycleMonitor monitor;
  std::vector<SessionLifecycleEvent> observed;
  REQUIRE(monitor.start(
      [&observed](SessionLifecycleEvent event) { observed.push_back(event); }));
  REQUIRE(monitor.post_for_test(SessionLifecycleEvent::did_wake));
  monitor.stop();
  QCoreApplication::processEvents(QEventLoop::AllEvents);
  REQUIRE(observed.empty());
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  translates_only_the_four_owned_native_notifications();
  start_is_single_owner_and_stop_rejects_late_events();
  queued_event_is_dropped_when_shutdown_wins();
  return EXIT_SUCCESS;
}
