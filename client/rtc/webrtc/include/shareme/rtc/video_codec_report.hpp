#pragma once

#include <string_view>

namespace shareme::rtc {

class VideoCodecReport final {
public:
  const std::string_view encoder;
  const std::string_view hardware_encoder_status;

private:
  constexpr VideoCodecReport(std::string_view encoder,
                             std::string_view hardware_encoder_status)
      : encoder(encoder), hardware_encoder_status(hardware_encoder_status) {}

  friend class SignaledPeer;
};

} // namespace shareme::rtc
