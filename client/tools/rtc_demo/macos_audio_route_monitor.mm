#include "qt_audio_route_monitor.hpp"

#include <CoreAudio/CoreAudio.h>
#include <Block.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

namespace {

constexpr AudioObjectPropertyAddress kDefaultOutputAddress{
    .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain,
};

class MacAudioRouteMonitor final : public AudioRouteNativeMonitor {
  struct CallbackState {
    Callback callback;
    std::mutex mutex;
    std::condition_variable changed;
    bool accepting{false};
    std::size_t in_flight{0};

    void open() noexcept {
      std::lock_guard lock{mutex};
      accepting = true;
    }

    void close() noexcept {
      std::lock_guard lock{mutex};
      accepting = false;
    }

    [[nodiscard]] bool admit() noexcept {
      std::lock_guard lock{mutex};
      if (!accepting)
        return false;
      ++in_flight;
      return true;
    }

    void complete() noexcept {
      std::lock_guard lock{mutex};
      if (in_flight != 0)
        --in_flight;
      if (in_flight == 0)
        changed.notify_all();
    }

    [[nodiscard]] bool wait_for_quiescence(
        std::chrono::milliseconds timeout) noexcept {
      std::unique_lock lock{mutex};
      return changed.wait_for(lock, timeout,
                              [this] { return in_flight == 0; });
    }
  };

 public:
  explicit MacAudioRouteMonitor(Callback callback)
      : callback_state_{std::make_shared<CallbackState>()} {
    callback_state_->callback = std::move(callback);
  }

  ~MacAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (started_ || listener_registered_)
      return false;
    callback_state_->open();
    const auto callback_state = callback_state_;
    listener_ = Block_copy(^(UInt32, const AudioObjectPropertyAddress *) {
      if (!callback_state->admit())
        return;
      try {
        if (callback_state->callback)
          callback_state->callback(
              shareme::core::AudioRouteChangeKind::default_output_changed,
              shareme::core::AudioRouteDefaultRole::default_output);
      } catch (...) {
      }
      callback_state->complete();
    });
    const auto status = AudioObjectAddPropertyListenerBlock(
        kAudioObjectSystemObject, &kDefaultOutputAddress, nullptr, listener_);
    if (status != noErr) {
      callback_state_->close();
      Block_release(listener_);
      listener_ = nullptr;
      return false;
    }
    listener_registered_ = true;
    started_ = true;
    return true;
  }

  void stop() noexcept override {
    callback_state_->close();
    if (listener_registered_) {
      last_remove_status_ = AudioObjectRemovePropertyListenerBlock(
          kAudioObjectSystemObject, &kDefaultOutputAddress, nullptr,
          listener_);
      if (last_remove_status_ == noErr)
        listener_registered_ = false;
    }
    const auto callbacks_drained = callback_state_->wait_for_quiescence(
        std::chrono::milliseconds{250});
    if (!listener_registered_ && callbacks_drained && listener_ != nullptr) {
      Block_release(listener_);
      listener_ = nullptr;
    }
    started_ = false;
  }

 private:
  std::shared_ptr<CallbackState> callback_state_;
  AudioObjectPropertyListenerBlock listener_{nullptr};
  OSStatus last_remove_status_{noErr};
  bool listener_registered_{false};
  bool started_{false};
};

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  return std::make_unique<MacAudioRouteMonitor>(std::move(callback));
}
