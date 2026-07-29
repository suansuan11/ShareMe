#pragma once

#include "audio_device_factory.hpp"

namespace shareme::rtc {

[[nodiscard]] MicrophonePermissionStatus
platform_microphone_permission_status() noexcept;

} // namespace shareme::rtc
