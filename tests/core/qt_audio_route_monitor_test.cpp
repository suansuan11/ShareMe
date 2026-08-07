#include "qt_audio_route_monitor.hpp"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QMetaObject>

#include <chrono>
#include <cstdlib>
#include <iostream>
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

using shareme::core::AudioRouteChangeKind;
using shareme::core::AudioRouteDefaultRole;
using shareme::core::AudioRouteEvent;
using shareme::core::AudioRouteObservedAt;

void qt_signal_registration_and_value_conversion_are_observable() {
  QtAudioRouteMonitor monitor;
  std::vector<AudioRouteEvent> events;
  REQUIRE(monitor.start([&](AudioRouteEvent event) {
    events.push_back(event);
  }));
  REQUIRE(monitor.accepting());

  // The monitor owns a real QMediaDevices object and registers this signal in
  // start(). Invoking the signal avoids requiring physical hardware in CI.
  REQUIRE(QMetaObject::invokeMethod(monitor.media_devices_for_test(),
                                    "audioOutputsChanged",
                                    Qt::DirectConnection));
  REQUIRE(!events.empty());

  const auto first_count = events.size();
  REQUIRE(monitor.notify_for_test(
      QByteArrayLiteral("test-output-a"),
      AudioRouteChangeKind::default_output_changed,
      AudioRouteDefaultRole::default_output));
  REQUIRE(monitor.notify_for_test(
      QByteArrayLiteral("test-output-a"),
      AudioRouteChangeKind::device_list_changed,
      AudioRouteDefaultRole::none));

  REQUIRE(events.size() == first_count + 2);
  const auto &first = events[first_count];
  const auto &second = events[first_count + 1];
  REQUIRE(first.event_sequence < second.event_sequence);
  REQUIRE(first.stable_device_id != 0);
  REQUIRE(first.stable_device_id == second.stable_device_id);
  REQUIRE(first.observed_at != AudioRouteObservedAt{});
  REQUIRE(second.observed_at >= first.observed_at);
}

void shutdown_closes_the_qt_monitor_ingress() {
  QtAudioRouteMonitor monitor;
  std::size_t callback_count = 0;
  REQUIRE(monitor.start([&](AudioRouteEvent) { ++callback_count; }));
  REQUIRE(monitor.notify_for_test(QByteArrayLiteral("test-output-b")));
  const auto before_stop = callback_count;
  REQUIRE(monitor.post_for_test(QByteArrayLiteral("test-output-queued")));

  monitor.stop();
  REQUIRE(!monitor.accepting());
  REQUIRE(!monitor.notify_for_test(QByteArrayLiteral("test-output-c")));
  QCoreApplication::processEvents();
  REQUIRE(!monitor.start([](AudioRouteEvent) {}));
  REQUIRE(callback_count == before_stop);
}

void route_events_resolve_only_to_the_current_qt_device() {
  QtAudioRouteMonitor monitor;
  std::vector<AudioRouteEvent> events;
  REQUIRE(monitor.start([&](AudioRouteEvent event) {
    events.push_back(event);
  }));

  const auto current = QMediaDevices::defaultAudioOutput();
  const auto current_key = current.isNull() ? QByteArray{}
                                             : current.id();
  REQUIRE(monitor.notify_for_test(current_key));
  const auto current_event = events.back();
  const auto resolved = monitor.resolve_device_for_event(current_event);
  REQUIRE(resolved.has_value());
  REQUIRE(resolved->isNull() == current.isNull());
  if (!current.isNull())
    REQUIRE(resolved->id() == current.id());

  const auto stale_key = current.isNull()
      ? QByteArrayLiteral("not-the-current-output")
      : current.id() + QByteArrayLiteral("-stale");
  REQUIRE(monitor.notify_for_test(stale_key));
  REQUIRE(!monitor.resolve_device_for_event(events.back()).has_value());
}

void native_supplement_status_distinguishes_qt_only_monitoring() {
  QtAudioRouteMonitor monitor;
  REQUIRE(monitor.start([](AudioRouteEvent) {}));
  // This target deliberately compiles only the common Qt monitor source, so
  // the absent supplement must be observable rather than implied as started.
  REQUIRE(monitor.native_supplement_status() ==
          AudioRouteNativeSupplementStatus::unavailable);
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  static_cast<void>(application);
  qt_signal_registration_and_value_conversion_are_observable();
  shutdown_closes_the_qt_monitor_ingress();
  route_events_resolve_only_to_the_current_qt_device();
  native_supplement_status_distinguishes_qt_only_monitoring();
  return EXIT_SUCCESS;
}
