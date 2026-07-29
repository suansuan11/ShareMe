#include "microphone_permission.hpp"

namespace shareme::rtc {

MicrophonePermissionStatus platform_microphone_permission_status() noexcept {
  return MicrophonePermissionStatus::unknown;
}

} // namespace shareme::rtc
