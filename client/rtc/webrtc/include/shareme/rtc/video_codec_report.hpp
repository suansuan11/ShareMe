#pragma once

#include <string>

namespace shareme::rtc {

struct VideoCodecReport {
  std::string encoder{"vp8-software"};
  std::string hardware_encoder_status{"unavailable-locked-abi"};
};

} // namespace shareme::rtc
