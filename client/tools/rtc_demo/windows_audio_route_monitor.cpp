#include "qt_audio_route_monitor.hpp"

#include <windows.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

namespace {

struct WindowsAudioRouteCallbackState {
  AudioRouteNativeMonitor::Callback callback;
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

class WindowsAudioRouteNotification final : public IMMNotificationClient {
 public:
  explicit WindowsAudioRouteNotification(
      std::shared_ptr<WindowsAudioRouteCallbackState> state)
      : state_{std::move(state)} {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interface_id,
                                           void **object) override {
    if (object == nullptr)
      return E_POINTER;
    *object = nullptr;
    if (interface_id == __uuidof(IUnknown) ||
        interface_id == __uuidof(IMMNotificationClient)) {
      *object = static_cast<IMMNotificationClient *>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return reference_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining =
        reference_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0)
      delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole,
                                                   LPCWSTR) override {
    if (flow != eRender || !state_->admit())
      return S_OK;
    try {
      if (state_->callback)
        state_->callback(
            shareme::core::AudioRouteChangeKind::default_output_changed,
            shareme::core::AudioRouteDefaultRole::default_output);
    } catch (...) {
    }
    state_->complete();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY)
      override {
    return S_OK;
  }

 private:
  ~WindowsAudioRouteNotification() override = default;

  std::atomic<ULONG> reference_count_{1};
  std::shared_ptr<WindowsAudioRouteCallbackState> state_;
};

class WindowsAudioRouteMonitor final : public AudioRouteNativeMonitor {
 public:
  explicit WindowsAudioRouteMonitor(Callback callback)
      : callback_state_{
            std::make_shared<WindowsAudioRouteCallbackState>()} {
    callback_state_->callback = std::move(callback);
  }

  ~WindowsAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (started_ || registered_)
      return false;
    callback_state_->open();

    const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
      return false;
    com_initialized_ = SUCCEEDED(com_result);

    auto result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator_));
    if (FAILED(result)) {
      stop();
      return false;
    }

    auto *notification = new WindowsAudioRouteNotification(callback_state_);
    result = enumerator_->RegisterEndpointNotificationCallback(notification);
    if (FAILED(result)) {
      notification->Release();
      stop();
      return false;
    }
    notification_ = notification;
    registered_ = true;
    started_ = true;
    return true;
  }

  void stop() noexcept override {
    callback_state_->close();
    if (registered_ && enumerator_ != nullptr && notification_ != nullptr) {
      last_unregister_status_ =
          enumerator_->UnregisterEndpointNotificationCallback(notification_);
      if (SUCCEEDED(last_unregister_status_))
        registered_ = false;
    }
    const auto callbacks_drained = callback_state_->wait_for_quiescence(
        std::chrono::milliseconds{250});
    if (!registered_ && callbacks_drained && notification_ != nullptr) {
      notification_->Release();
      notification_ = nullptr;
    }
    if (!registered_ && enumerator_ != nullptr) {
      enumerator_->Release();
      enumerator_ = nullptr;
    }
    if (!registered_ && notification_ == nullptr && com_initialized_) {
      CoUninitialize();
      com_initialized_ = false;
    }
    started_ = false;
  }

 private:
  std::shared_ptr<WindowsAudioRouteCallbackState> callback_state_;
  IMMDeviceEnumerator *enumerator_{nullptr};
  WindowsAudioRouteNotification *notification_{nullptr};
  HRESULT last_unregister_status_{S_OK};
  bool com_initialized_{false};
  bool registered_{false};
  bool started_{false};
};

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  return std::make_unique<WindowsAudioRouteMonitor>(std::move(callback));
}
