#include "qt_audio_route_monitor.hpp"

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

  monitor.stop();
  REQUIRE(!monitor.accepting());
  REQUIRE(!monitor.notify_for_test(QByteArrayLiteral("test-output-c")));
  REQUIRE(!monitor.start([](AudioRouteEvent) {}));
  REQUIRE(callback_count == before_stop);
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  static_cast<void>(application);
  qt_signal_registration_and_value_conversion_are_observable();
  shutdown_closes_the_qt_monitor_ingress();
  return EXIT_SUCCESS;
}
