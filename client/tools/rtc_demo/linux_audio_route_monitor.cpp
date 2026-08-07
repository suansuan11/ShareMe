#include "qt_audio_route_monitor.hpp"

#if defined(SHAREME_HAS_PIPEWIRE)
#include <pipewire/extensions/metadata.h>
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>
#else
#include <pulse/pulseaudio.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace {

using shareme::core::AudioRouteChangeKind;
using shareme::core::AudioRouteDefaultRole;

#if defined(SHAREME_HAS_PIPEWIRE)

class LinuxPipeWireAudioRouteMonitor final : public AudioRouteNativeMonitor {
 public:
  explicit LinuxPipeWireAudioRouteMonitor(Callback callback)
      : callback_{std::move(callback)} {}

  ~LinuxPipeWireAudioRouteMonitor() override { stop(); }

  bool start() override {
    if (started_)
      return false;

    pw_init(nullptr, nullptr);
    initialized_ = true;
    mainloop_ = pw_main_loop_new(nullptr);
    if (mainloop_ == nullptr) {
      stop();
      return false;
    }
    context_ = pw_context_new(pw_main_loop_get_loop(mainloop_), nullptr, 0);
    if (context_ == nullptr) {
      stop();
      return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_ == nullptr) {
      stop();
      return false;
    }
    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    if (registry_ == nullptr) {
      stop();
      return false;
    }

    static const pw_registry_events registry_events{
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = &registry_global,
        .global_remove = &registry_global_remove,
    };
    if (pw_registry_add_listener(registry_, &registry_listener_,
                                 &registry_events, this) < 0) {
      stop();
      return false;
    }

    accepting_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { pw_main_loop_run(mainloop_); });
    started_ = true;
    return true;
  }

  void stop() noexcept override {
    accepting_.store(false, std::memory_order_release);
    if (mainloop_ != nullptr)
      pw_main_loop_quit(mainloop_);
    if (worker_.joinable())
      worker_.join();

    if (metadata_ != nullptr) {
      spa_hook_remove(&metadata_listener_);
      pw_proxy_destroy(reinterpret_cast<pw_proxy *>(metadata_));
      metadata_ = nullptr;
    }
    if (registry_ != nullptr) {
      spa_hook_remove(&registry_listener_);
      pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry_));
      registry_ = nullptr;
    }
    if (core_ != nullptr) {
      pw_core_disconnect(core_);
      core_ = nullptr;
    }
    if (context_ != nullptr) {
      pw_context_destroy(context_);
      context_ = nullptr;
    }
    if (mainloop_ != nullptr) {
      pw_main_loop_destroy(mainloop_);
      mainloop_ = nullptr;
    }
    if (initialized_) {
      pw_deinit();
      initialized_ = false;
    }
    started_ = false;
  }

 private:
  static void registry_global(void *user_data, uint32_t id, uint32_t,
                              const char *type, uint32_t, const spa_dict *props) {
    auto *monitor = static_cast<LinuxPipeWireAudioRouteMonitor *>(user_data);
    if (monitor == nullptr ||
        !monitor->accepting_.load(std::memory_order_acquire))
      return;

    if (type != nullptr &&
        std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
        monitor->metadata_ == nullptr) {
      monitor->metadata_id_ = id;
      monitor->metadata_ = static_cast<pw_metadata *>(pw_registry_bind(
          monitor->registry_, id, type, PW_VERSION_METADATA, 0));
      if (monitor->metadata_ != nullptr) {
        static const pw_metadata_events metadata_events{
            .version = PW_VERSION_METADATA_EVENTS,
            .property = &metadata_property,
        };
        static_cast<void>(pw_metadata_add_listener(
            monitor->metadata_, &monitor->metadata_listener_,
            &metadata_events, monitor));
      }
    }

    const auto media_class = props == nullptr
        ? nullptr
        : spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (media_class != nullptr &&
        std::strcmp(media_class, "Audio/Sink") == 0)
      monitor->emit(AudioRouteChangeKind::device_list_changed,
                    AudioRouteDefaultRole::none);
  }

  static void registry_global_remove(void *user_data, uint32_t id) {
    auto *monitor = static_cast<LinuxPipeWireAudioRouteMonitor *>(user_data);
    if (monitor == nullptr ||
        !monitor->accepting_.load(std::memory_order_acquire))
      return;
    if (id == monitor->metadata_id_ && monitor->metadata_ != nullptr) {
      spa_hook_remove(&monitor->metadata_listener_);
      pw_proxy_destroy(reinterpret_cast<pw_proxy *>(monitor->metadata_));
      monitor->metadata_ = nullptr;
      monitor->metadata_id_ = std::numeric_limits<std::uint32_t>::max();
    }
    monitor->emit(AudioRouteChangeKind::device_list_changed,
                  AudioRouteDefaultRole::none);
  }

  static int metadata_property(void *user_data, uint32_t, const char *key,
                               const char *, const char *) {
    auto *monitor = static_cast<LinuxPipeWireAudioRouteMonitor *>(user_data);
    if (monitor == nullptr || key == nullptr ||
        std::strcmp(key, "default.audio.sink") != 0)
      return 0;
    monitor->emit(AudioRouteChangeKind::default_output_changed,
                  AudioRouteDefaultRole::default_output);
    return 0;
  }

  void emit(AudioRouteChangeKind change_kind,
            AudioRouteDefaultRole default_role) noexcept {
    if (!accepting_.load(std::memory_order_acquire))
      return;
    try {
      if (callback_)
        callback_(change_kind, default_role);
    } catch (...) {
    }
  }

  Callback callback_;
  pw_main_loop *mainloop_{nullptr};
  pw_context *context_{nullptr};
  pw_core *core_{nullptr};
  pw_registry *registry_{nullptr};
  pw_metadata *metadata_{nullptr};
  spa_hook registry_listener_{};
  spa_hook metadata_listener_{};
  std::thread worker_;
  std::atomic_bool accepting_{false};
  std::uint32_t metadata_id_{std::numeric_limits<std::uint32_t>::max()};
  bool initialized_{false};
  bool started_{false};
};

#else

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
    monitor->notify(
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
    if (facility == PA_SUBSCRIPTION_EVENT_SINK)
      monitor->notify(
          shareme::core::AudioRouteChangeKind::device_list_changed,
          shareme::core::AudioRouteDefaultRole::none);
  }

  void notify(shareme::core::AudioRouteChangeKind change_kind,
              shareme::core::AudioRouteDefaultRole default_role) noexcept {
    if (!accepting_.load(std::memory_order_acquire))
      return;
    try {
      if (callback_)
        callback_(change_kind, default_role);
    } catch (...) {
    }
  }

  Callback callback_;
  pa_threaded_mainloop *mainloop_{nullptr};
  pa_context *context_{nullptr};
  bool mainloop_started_{false};
  std::atomic_bool accepting_{false};
};

#endif

} // namespace

std::unique_ptr<AudioRouteNativeMonitor>
create_audio_route_native_monitor(AudioRouteNativeMonitor::Callback callback) {
#if defined(SHAREME_HAS_PIPEWIRE)
  return std::make_unique<LinuxPipeWireAudioRouteMonitor>(std::move(callback));
#else
  return std::make_unique<LinuxAudioRouteMonitor>(std::move(callback));
#endif
}
