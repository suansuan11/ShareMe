#pragma once

#include "shareme/core/audio_route.hpp"

#include <QByteArray>
#include <QMetaObject>

#include <functional>
#include <memory>

class QMediaDevices;

// Native supplements report a change only. The Qt monitor resolves the
// current QAudioDevice and creates the common value event on the Qt thread.
class AudioRouteNativeMonitor {
 public:
  using Callback = std::function<void(
      shareme::core::AudioRouteChangeKind,
      shareme::core::AudioRouteDefaultRole)>;

  virtual ~AudioRouteNativeMonitor() = default;

  AudioRouteNativeMonitor(const AudioRouteNativeMonitor &) = delete;
  AudioRouteNativeMonitor &operator=(const AudioRouteNativeMonitor &) = delete;

  [[nodiscard]] virtual bool start() = 0;
  virtual void stop() noexcept = 0;

 protected:
  AudioRouteNativeMonitor() = default;
};

[[nodiscard]] std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback);

class QtAudioRouteMonitor final {
 public:
  using Callback = shareme::core::AudioRouteMonitor::Callback;

  QtAudioRouteMonitor();
  ~QtAudioRouteMonitor();

  QtAudioRouteMonitor(const QtAudioRouteMonitor &) = delete;
  QtAudioRouteMonitor &operator=(const QtAudioRouteMonitor &) = delete;

  // A monitor has one start/stop lifetime, matching the portable ingress
  // contract. The callback receives only value data and no native object.
  [[nodiscard]] bool start(Callback callback);
  void stop() noexcept;
  [[nodiscard]] bool accepting() const noexcept;

  // Narrow deterministic seam for conversion and lifecycle tests. Production
  // route changes arrive through QMediaDevices or a native supplement.
  [[nodiscard]] bool notify_for_test(
      QByteArray device_key,
      shareme::core::AudioRouteChangeKind change_kind =
          shareme::core::AudioRouteChangeKind::default_output_changed,
      shareme::core::AudioRouteDefaultRole default_role =
          shareme::core::AudioRouteDefaultRole::default_output);

  // The returned object is owned by this monitor and is exposed only so the
  // registered Qt signal can be triggered without physical hardware in tests.
  [[nodiscard]] QMediaDevices *media_devices_for_test() const noexcept;

 private:
  struct State;

  std::shared_ptr<State> state_;
  std::unique_ptr<QMediaDevices> media_devices_;
  std::unique_ptr<AudioRouteNativeMonitor> native_monitor_;
  QMetaObject::Connection output_connection_;
};
