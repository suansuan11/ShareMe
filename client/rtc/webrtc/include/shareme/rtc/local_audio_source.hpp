#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "api/audio_options.h"
#include "api/media_stream_interface.h"
#include "api/notifier.h"

namespace shareme::rtc {

class LocalAudioSource
    : public webrtc::Notifier<webrtc::AudioSourceInterface> {
public:
  [[nodiscard]] virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::uint64_t generated_count() const noexcept = 0;
  [[nodiscard]] virtual std::optional<std::int64_t>
  last_pts_ms() const noexcept = 0;
  [[nodiscard]] virtual std::string error() const = 0;

  [[nodiscard]] SourceState state() const final { return kLive; }
  [[nodiscard]] bool remote() const final { return false; }
  [[nodiscard]] const webrtc::AudioOptions options() const final {
    return options_;
  }

protected:
  LocalAudioSource() {
    options_.echo_cancellation = false;
    options_.noise_suppression = false;
    options_.auto_gain_control = false;
  }
  ~LocalAudioSource() override = default;

private:
  webrtc::AudioOptions options_;
};

} // namespace shareme::rtc
