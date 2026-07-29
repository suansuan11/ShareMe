#pragma once

#include "shareme/rtc/probe_contract.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shareme::rtc {

struct ProbeCliParseResult {
  std::optional<ProbeConfig> config;
  std::string diagnostic;
};

[[nodiscard]] ProbeCliParseResult parse_probe_arguments(
    const std::vector<std::string_view>& arguments);

}  // namespace shareme::rtc
