#include "shareme/rtc/screen_video_source.hpp"

#include <utility>

#include "shareme/rtc/macos_screen_capture_source.hpp"

#if defined(_WIN32)
#include "windows_screen_capture_backend.hpp"
#endif

namespace shareme::rtc {
namespace {

MacScreenCaptureConfig mac_capture_config(const ScreenCaptureConfig &config) {
  return {.profile = config.profile,
          .display_id = config.display_id,
          .show_cursor = config.show_cursor};
}

} // namespace

std::unique_ptr<ScreenCaptureBackend>
create_platform_screen_capture_backend(const ScreenCaptureConfig &config) {
#if defined(__APPLE__)
  const auto mac_config = mac_capture_config(config);
  return std::make_unique<MacScreenCaptureSource>(
      mac_config, create_screen_capture_kit_stream(mac_config));
#elif defined(_WIN32)
  return create_windows_screen_capture_backend(config);
#else
  static_cast<void>(config);
  return nullptr;
#endif
}

webrtc::scoped_refptr<ScreenVideoSource>
ScreenVideoSource::create(ScreenCaptureConfig config) {
  return webrtc::scoped_refptr<ScreenVideoSource>(new ScreenVideoSource(
      config, create_platform_screen_capture_backend(config)));
}

} // namespace shareme::rtc
