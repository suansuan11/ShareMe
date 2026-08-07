#include "qt_audio_route_monitor.hpp"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QHash>
#include <QMediaDevices>
#include <QMetaObject>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

struct QtAudioRouteMonitor::State {
  shareme::core::AudioRouteMonitor ingress;
  QHash<QByteArray, shareme::core::AudioRouteDeviceId> stable_ids;
  shareme::core::AudioRouteEventSequence next_event_sequence{1};
  shareme::core::AudioRouteDeviceId next_device_id{1};
  QByteArray last_default_key;
  bool observed_default{false};
  std::atomic_bool accepting{false};

  [[nodiscard]] bool start(Callback callback) {
    if (!ingress.start(std::move(callback)))
      return false;
    accepting.store(true, std::memory_order_release);
    return true;
  }

  void stop() noexcept {
    accepting.store(false, std::memory_order_release);
    ingress.stop();
  }

  [[nodiscard]] bool is_accepting() const noexcept {
    return accepting.load(std::memory_order_acquire) && ingress.accepting();
  }

  [[nodiscard]] shareme::core::AudioRouteDeviceId stable_id_for(
      const QByteArray &device_key) {
    if (device_key.isEmpty())
      return 0;
    if (const auto existing = stable_ids.constFind(device_key);
        existing != stable_ids.cend())
      return existing.value();
    if (next_device_id == 0 || next_device_id ==
                                  std::numeric_limits<
                                      shareme::core::AudioRouteDeviceId>::max())
      return 0;
    const auto assigned = next_device_id++;
    stable_ids.insert(device_key, assigned);
    return assigned;
  }

  [[nodiscard]] bool notify_key(
      QByteArray device_key, shareme::core::AudioRouteChangeKind change_kind,
      shareme::core::AudioRouteDefaultRole default_role) {
    if (!is_accepting())
      return false;
    if (next_event_sequence == 0)
      return false;

    const auto sequence = next_event_sequence;
    if (next_event_sequence !=
        std::numeric_limits<shareme::core::AudioRouteEventSequence>::max())
      ++next_event_sequence;
    else
      next_event_sequence = 0;

    return ingress.notify(shareme::core::AudioRouteEvent{
        .event_sequence = sequence,
        .stable_device_id = stable_id_for(device_key),
        .change_kind = change_kind,
        .default_role = default_role,
        .observed_at = std::chrono::steady_clock::now(),
    });
  }

  void observe_qt_outputs() {
    const auto device = QMediaDevices::defaultAudioOutput();
    const auto device_key = device.isNull() ? QByteArray{} : device.id();
    const auto default_changed = !observed_default ||
        device_key != last_default_key;
    last_default_key = device_key;
    observed_default = true;
    static_cast<void>(notify_key(
        device_key,
        default_changed ? shareme::core::AudioRouteChangeKind::
                              default_output_changed
                        : shareme::core::AudioRouteChangeKind::device_list_changed,
        device.isNull() ? shareme::core::AudioRouteDefaultRole::none
                         : shareme::core::AudioRouteDefaultRole::default_output));
  }

  [[nodiscard]] static bool post_key(
      QCoreApplication *dispatch_context, std::weak_ptr<State> weak_state,
      QByteArray device_key, shareme::core::AudioRouteChangeKind change_kind,
      shareme::core::AudioRouteDefaultRole default_role) {
    if (dispatch_context == nullptr)
      return false;
    return QMetaObject::invokeMethod(
        dispatch_context,
        [weak_state, device_key = std::move(device_key), change_kind,
         default_role] {
          if (const auto state = weak_state.lock();
              state && state->is_accepting())
            static_cast<void>(state->notify_key(device_key, change_kind,
                                                 default_role));
        },
        Qt::QueuedConnection);
  }

  [[nodiscard]] static bool post_native_change(
      QCoreApplication *dispatch_context, std::weak_ptr<State> weak_state,
      shareme::core::AudioRouteChangeKind change_kind,
      shareme::core::AudioRouteDefaultRole default_role) {
    if (dispatch_context == nullptr)
      return false;
    return QMetaObject::invokeMethod(
        dispatch_context,
        [weak_state, change_kind, default_role] {
          if (const auto state = weak_state.lock();
              state && state->is_accepting()) {
            const auto device = QMediaDevices::defaultAudioOutput();
            static_cast<void>(state->notify_key(
                device.isNull() ? QByteArray{} : device.id(), change_kind,
                default_role));
          }
        },
        Qt::QueuedConnection);
  }
};

#if !defined(SHAREME_HAS_NATIVE_AUDIO_ROUTE_SUPPLEMENT)
std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  static_cast<void>(callback);
  return {};
}
#endif

QtAudioRouteMonitor::QtAudioRouteMonitor()
    : state_{std::make_shared<State>()},
      media_devices_{std::make_unique<QMediaDevices>()} {}

QtAudioRouteMonitor::~QtAudioRouteMonitor() { stop(); }

bool QtAudioRouteMonitor::start(Callback callback) {
  if (!state_->start(std::move(callback)))
    return false;

  const auto state = state_;
  output_connection_ = QObject::connect(
      media_devices_.get(), &QMediaDevices::audioOutputsChanged,
      media_devices_.get(), [state] { state->observe_qt_outputs(); });
  state->observe_qt_outputs();

  const auto weak_state = std::weak_ptr<State>{state_};
  auto *dispatch_context = QCoreApplication::instance();
  native_monitor_ = create_audio_route_native_monitor(
      [weak_state, dispatch_context](
          shareme::core::AudioRouteChangeKind change_kind,
          shareme::core::AudioRouteDefaultRole default_role) {
        static_cast<void>(State::post_native_change(
            dispatch_context, weak_state, change_kind, default_role));
      });
  if (native_monitor_ && !native_monitor_->start())
    native_monitor_.reset();
  return true;
}

void QtAudioRouteMonitor::stop() noexcept {
  state_->stop();
  if (native_monitor_) {
    native_monitor_->stop();
    native_monitor_.reset();
  }
  QObject::disconnect(output_connection_);
  output_connection_ = {};
}

bool QtAudioRouteMonitor::accepting() const noexcept {
  return state_->is_accepting();
}

bool QtAudioRouteMonitor::notify_for_test(
    QByteArray device_key, shareme::core::AudioRouteChangeKind change_kind,
    shareme::core::AudioRouteDefaultRole default_role) {
  return state_->notify_key(std::move(device_key), change_kind, default_role);
}

bool QtAudioRouteMonitor::post_for_test(QByteArray device_key) {
  return State::post_key(QCoreApplication::instance(),
                         std::weak_ptr<State>{state_}, std::move(device_key),
                         shareme::core::AudioRouteChangeKind::
                             default_output_changed,
                         shareme::core::AudioRouteDefaultRole::default_output);
}

QMediaDevices *QtAudioRouteMonitor::media_devices_for_test() const noexcept {
  return media_devices_.get();
}
