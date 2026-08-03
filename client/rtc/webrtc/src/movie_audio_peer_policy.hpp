#pragma once

#include "audio_device_factory.hpp"
#include "shareme/rtc/signaled_peer.hpp"

namespace shareme::rtc {

[[nodiscard]] constexpr AudioDeviceMode
movie_audio_device_mode(SignaledRole role, bool native_playout) noexcept {
  return role == SignaledRole::viewer && native_playout
             ? AudioDeviceMode::playout
             : AudioDeviceMode::synthetic;
}

} // namespace shareme::rtc
