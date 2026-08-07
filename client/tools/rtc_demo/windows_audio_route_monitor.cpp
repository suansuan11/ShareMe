#include "qt_audio_route_monitor.hpp"

#include <windows.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <utility>

namespace {

class WindowsAudioRouteNotification final : public IMMNotificationClient {
 public:
  explicit WindowsAudioRouteNotification(AudioRouteNativeMonitor::Callback callback)
      : callback_{std::move(callback)} {}

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
    if (flow == eRender && callback_)
      callback_(shareme::core::AudioRouteChangeKind::default_output_changed,
                shareme::core::AudioRouteDefaultRole::default_output);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY)
      override {
    return S_OK;
  }

 private:
  ~WindowsAudioRouteNotification() override = default;

  std::atomic<ULONG> reference_count_{1};
  AudioRouteNativeMonitor::Callback callback_;
};

class WindowsAudioRouteMonitor final : public AudioRouteNativeMonitor {
 public:
  explicit WindowsAudioRouteMonitor(Callback callback)
      : callback_{std::move(callback)} {}

  ~WindowsAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (started_)
      return false;

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

    auto *notification = new WindowsAudioRouteNotification(callback_);
    result = enumerator_->RegisterEndpointNotificationCallback(notification);
    if (FAILED(result)) {
      notification->Release();
      stop();
      return false;
    }
    notification_ = notification;
    started_ = true;
    return true;
  }

  void stop() noexcept override {
    if (enumerator_ != nullptr && notification_ != nullptr)
      static_cast<void>(enumerator_->UnregisterEndpointNotificationCallback(
          notification_));
    if (notification_ != nullptr) {
      notification_->Release();
      notification_ = nullptr;
    }
    if (enumerator_ != nullptr) {
      enumerator_->Release();
      enumerator_ = nullptr;
    }
    if (com_initialized_) {
      CoUninitialize();
      com_initialized_ = false;
    }
    started_ = false;
  }

 private:
  Callback callback_;
  IMMDeviceEnumerator *enumerator_{nullptr};
  WindowsAudioRouteNotification *notification_{nullptr};
  bool com_initialized_{false};
  bool started_{false};
};

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  return std::make_unique<WindowsAudioRouteMonitor>(std::move(callback));
}
