#include "qt_audio_route_monitor.hpp"

#include <CoreAudio/CoreAudio.h>
#include <Block.h>

#include <atomic>
#include <memory>
#include <utility>

namespace {

constexpr AudioObjectPropertyAddress kDefaultOutputAddress{
    .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain,
};

class MacAudioRouteMonitor final : public AudioRouteNativeMonitor {
 public:
  explicit MacAudioRouteMonitor(Callback callback)
      : callback_state_{std::make_shared<CallbackState>()} {
    callback_state_->callback = std::move(callback);
  }

  ~MacAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (started_)
      return false;
    callback_state_->accepting.store(true, std::memory_order_release);
    const auto callback_state = callback_state_;
    listener_ = Block_copy(^(UInt32, const AudioObjectPropertyAddress *) {
      if (!callback_state->accepting.load(std::memory_order_acquire))
        return;
      if (callback_state->callback)
        callback_state->callback(
            shareme::core::AudioRouteChangeKind::default_output_changed,
            shareme::core::AudioRouteDefaultRole::default_output);
    });
    const auto status = AudioObjectAddPropertyListenerBlock(
        kAudioObjectSystemObject, &kDefaultOutputAddress, nullptr, listener_);
    if (status != noErr) {
      callback_state_->accepting.store(false, std::memory_order_release);
      Block_release(listener_);
      listener_ = nullptr;
      return false;
    }
    started_ = true;
    return true;
  }

  void stop() noexcept override {
    callback_state_->accepting.store(false, std::memory_order_release);
    if (started_) {
      static_cast<void>(AudioObjectRemovePropertyListenerBlock(
          kAudioObjectSystemObject, &kDefaultOutputAddress, nullptr,
          listener_));
      started_ = false;
    }
    if (listener_ != nullptr) {
      Block_release(listener_);
      listener_ = nullptr;
    }
  }

 private:
  struct CallbackState {
    Callback callback;
    std::atomic_bool accepting{false};
  };

  std::shared_ptr<CallbackState> callback_state_;
  AudioObjectPropertyListenerBlock listener_{nullptr};
  bool started_{false};
};

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  return std::make_unique<MacAudioRouteMonitor>(std::move(callback));
}
