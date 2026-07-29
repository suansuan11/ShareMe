#pragma once

#include "shareme/rtc/probe_contract.hpp"

namespace shareme::rtc {

[[nodiscard]] ProbeResult run_webrtc_probe(const ProbeConfig &config);

} // namespace shareme::rtc
