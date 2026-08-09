#pragma once

namespace shareme::tools {

class RtcControlState final {
public:
  [[nodiscard]] bool microphone_muted() const noexcept;
  [[nodiscard]] bool speaker_muted() const noexcept;
  [[nodiscard]] bool session_ended() const noexcept;

  bool set_microphone_muted(bool muted) noexcept;
  bool set_speaker_muted(bool muted) noexcept;
  bool finish_session() noexcept;

private:
  bool microphone_muted_{false};
  bool speaker_muted_{false};
  bool session_ended_{false};
};

} // namespace shareme::tools
