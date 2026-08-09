#include "rtc_control_state.hpp"

namespace shareme::tools {

bool RtcControlState::microphone_muted() const noexcept {
  return microphone_muted_;
}

bool RtcControlState::speaker_muted() const noexcept {
  return speaker_muted_;
}

bool RtcControlState::session_ended() const noexcept { return session_ended_; }

bool RtcControlState::set_microphone_muted(bool muted) noexcept {
  if (session_ended_ || microphone_muted_ == muted)
    return false;
  microphone_muted_ = muted;
  return true;
}

bool RtcControlState::set_speaker_muted(bool muted) noexcept {
  if (session_ended_ || speaker_muted_ == muted)
    return false;
  speaker_muted_ = muted;
  return true;
}

bool RtcControlState::finish_session() noexcept {
  if (session_ended_)
    return false;
  session_ended_ = true;
  return true;
}

} // namespace shareme::tools
