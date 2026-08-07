#include "qt_audio_route_monitor.hpp"

#include <pulse/pulseaudio.h>

#include <atomic>
#include <utility>

namespace {

class LinuxAudioRouteMonitor final : public AudioRouteNativeMonitor {
 public:
  explicit LinuxAudioRouteMonitor(Callback callback)
      : callback_{std::move(callback)} {}

  ~LinuxAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (mainloop_ != nullptr)
      return false;

    mainloop_ = pa_threaded_mainloop_new();
    if (mainloop_ == nullptr)
      return false;
    context_ = pa_context_new(pa_threaded_mainloop_get_api(mainloop_),
                               "ShareMe audio route monitor");
    if (context_ == nullptr) {
      stop();
      return false;
    }
    pa_context_set_state_callback(context_, &context_state_changed, this);
    pa_context_set_subscribe_callback(context_, &subscription_changed, this);
    if (pa_threaded_mainloop_start(mainloop_) < 0) {
      stop();
      return false;
    }
    mainloop_started_ = true;

    pa_threaded_mainloop_lock(mainloop_);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN,
                           nullptr) < 0) {
      pa_threaded_mainloop_unlock(mainloop_);
      stop();
      return false;
    }
    for (;;) {
      const auto state = pa_context_get_state(context_);
      if (state == PA_CONTEXT_READY)
        break;
      if (!PA_CONTEXT_IS_GOOD(state)) {
        pa_threaded_mainloop_unlock(mainloop_);
        stop();
        return false;
      }
      pa_threaded_mainloop_wait(mainloop_);
    }

    const auto operation = pa_context_subscribe(
        context_, static_cast<pa_subscription_mask_t>(
                      PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK),
        nullptr, nullptr);
    if (operation == nullptr) {
      pa_threaded_mainloop_unlock(mainloop_);
      stop();
      return false;
    }
    pa_operation_unref(operation);
    accepting_.store(true, std::memory_order_release);
    pa_threaded_mainloop_unlock(mainloop_);
    return true;
  }

  void stop() noexcept override {
    accepting_.store(false, std::memory_order_release);
    if (mainloop_ == nullptr)
      return;

    if (context_ != nullptr && mainloop_started_) {
      pa_threaded_mainloop_lock(mainloop_);
      pa_context_disconnect(context_);
      pa_threaded_mainloop_unlock(mainloop_);
    }
    if (mainloop_started_) {
      pa_threaded_mainloop_stop(mainloop_);
      mainloop_started_ = false;
    }
    if (context_ != nullptr) {
      pa_context_unref(context_);
      context_ = nullptr;
    }
    pa_threaded_mainloop_free(mainloop_);
    mainloop_ = nullptr;
  }

 private:
  static void context_state_changed(pa_context *, void *user_data) {
    auto *monitor = static_cast<LinuxAudioRouteMonitor *>(user_data);
    if (monitor != nullptr && monitor->mainloop_ != nullptr)
      pa_threaded_mainloop_signal(monitor->mainloop_, 0);
  }

  static void server_info_ready(pa_context *, const pa_server_info *, void *user_data) {
    auto *monitor = static_cast<LinuxAudioRouteMonitor *>(user_data);
    if (monitor == nullptr ||
        !monitor->accepting_.load(std::memory_order_acquire))
      return;
    if (monitor->callback_)
      monitor->callback_(
          shareme::core::AudioRouteChangeKind::default_output_changed,
          shareme::core::AudioRouteDefaultRole::default_output);
  }

  static void subscription_changed(pa_context *context,
                                   pa_subscription_event_type_t event_type,
                                   uint32_t, void *user_data) {
    auto *monitor = static_cast<LinuxAudioRouteMonitor *>(user_data);
    if (monitor == nullptr ||
        !monitor->accepting_.load(std::memory_order_acquire))
      return;
    const auto facility = static_cast<pa_subscription_event_type_t>(
        event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
    if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
      const auto operation =
          pa_context_get_server_info(context, &server_info_ready, monitor);
      if (operation != nullptr)
        pa_operation_unref(operation);
      return;
    }
    if (facility == PA_SUBSCRIPTION_EVENT_SINK && monitor->callback_)
      monitor->callback_(
          shareme::core::AudioRouteChangeKind::device_list_changed,
          shareme::core::AudioRouteDefaultRole::none);
  }

  Callback callback_;
  pa_threaded_mainloop *mainloop_{nullptr};
  pa_context *context_{nullptr};
  bool mainloop_started_{false};
  std::atomic_bool accepting_{false};
};

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
  return std::make_unique<LinuxAudioRouteMonitor>(std::move(callback));
}
