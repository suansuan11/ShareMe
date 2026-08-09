#pragma once

#include <memory>

#include "shareme/rtc/screen_video_source.hpp"

namespace shareme::rtc {

[[nodiscard]] std::unique_ptr<ScreenCaptureBackend>
create_windows_screen_capture_backend(const ScreenCaptureConfig &config);

} // namespace shareme::rtc
