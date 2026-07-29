#include "shareme/rtc/probe_cli.hpp"
#include "shareme/rtc/probe_contract.hpp"
#include "shareme/rtc/webrtc_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#ifndef SHAREME_PROBE_REVISION
#define SHAREME_PROBE_REVISION "unknown"
#endif

namespace {

constexpr std::string_view platform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

constexpr std::string_view architecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return "x86_64";
#else
  return "unknown";
#endif
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }

  const auto parsed = shareme::rtc::parse_probe_arguments(arguments);
  if (!parsed.config.has_value()) {
    std::cerr << parsed.diagnostic << '\n';
    return 2;
  }

  const auto result = shareme::rtc::run_webrtc_probe(*parsed.config);
  std::cout << shareme::rtc::to_json(result, SHAREME_PROBE_REVISION, platform(),
                                     architecture())
            << '\n';
  return result.status == shareme::rtc::ProbeStatus::passed ? EXIT_SUCCESS
                                                            : EXIT_FAILURE;
}
