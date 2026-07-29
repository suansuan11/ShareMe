#include "shareme/rtc/probe_cli.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace shareme::rtc {
namespace {

constexpr std::size_t kMaximumDiagnosticLength = 256;

std::string sanitize_diagnostic(std::string_view value) {
  std::string result;
  result.reserve(std::min(value.size(), kMaximumDiagnosticLength));
  for (const char character : value) {
    if (result.size() == kMaximumDiagnosticLength) {
      break;
    }
    const auto byte = static_cast<unsigned char>(character);
    result.push_back(byte < 0x20U || byte == 0x7FU ? ' ' : character);
  }
  return result;
}

ProbeCliParseResult failure(std::string_view diagnostic) {
  return {.config = std::nullopt,
          .diagnostic = sanitize_diagnostic(diagnostic)};
}

std::optional<int> parse_integer(std::string_view value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [position, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || position != end || value.empty()) {
    return std::nullopt;
  }
  return parsed;
}

}  // namespace

ProbeCliParseResult parse_probe_arguments(
    const std::vector<std::string_view>& arguments) {
  ProbeConfig config;
  bool saw_audio = false;
  bool saw_seconds = false;
  bool saw_width = false;
  bool saw_height = false;
  bool saw_fps = false;

  for (std::size_t index = 0; index < arguments.size(); index += 2) {
    const auto option = arguments[index];
    if (index + 1 >= arguments.size()) {
      return failure("missing value for command-line option");
    }
    const auto value = arguments[index + 1];

    bool* seen = nullptr;
    if (option == "--audio") {
      seen = &saw_audio;
    } else if (option == "--seconds") {
      seen = &saw_seconds;
    } else if (option == "--width") {
      seen = &saw_width;
    } else if (option == "--height") {
      seen = &saw_height;
    } else if (option == "--fps") {
      seen = &saw_fps;
    } else {
      return failure("unknown command-line option");
    }

    if (*seen) {
      return failure("command-line option was repeated");
    }
    *seen = true;

    if (option == "--audio") {
      if (value == "synthetic") {
        config.audio_mode = ProbeAudioMode::synthetic;
      } else if (value == "microphone") {
        config.audio_mode = ProbeAudioMode::microphone;
      } else {
        return failure("audio mode must be synthetic or microphone");
      }
      continue;
    }

    const auto parsed = parse_integer(value);
    if (!parsed.has_value()) {
      return failure("command-line option requires an integer value");
    }
    if (option == "--seconds") {
      config.run_for = std::chrono::seconds{*parsed};
    } else if (option == "--width") {
      config.width = *parsed;
    } else if (option == "--height") {
      config.height = *parsed;
    } else {
      config.frames_per_second = *parsed;
    }
  }

  if (const auto diagnostic = validate(config); diagnostic.has_value()) {
    return failure(*diagnostic);
  }
  return {.config = config, .diagnostic = {}};
}

}  // namespace shareme::rtc
