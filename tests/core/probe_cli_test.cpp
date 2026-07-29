#include "shareme/rtc/probe_cli.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::rtc::ProbeCliParseResult parse(
    std::initializer_list<std::string_view> arguments) {
  return shareme::rtc::parse_probe_arguments(
      std::vector<std::string_view>{arguments});
}

void accepts_defaults_and_all_supported_options() {
  using namespace std::chrono_literals;
  using shareme::rtc::ProbeAudioMode;

  const auto defaults = parse({});
  REQUIRE(defaults.config.has_value());
  REQUIRE(defaults.diagnostic.empty());
  REQUIRE(defaults.config->audio_mode == ProbeAudioMode::synthetic);
  REQUIRE(defaults.config->run_for == 3s);

  const auto explicit_config =
      parse({"--audio", "microphone", "--seconds", "5", "--width", "1280",
             "--height", "720", "--fps", "60"});
  REQUIRE(explicit_config.config.has_value());
  REQUIRE(explicit_config.config->audio_mode == ProbeAudioMode::microphone);
  REQUIRE(explicit_config.config->run_for == 5s);
  REQUIRE(explicit_config.config->width == 1280);
  REQUIRE(explicit_config.config->height == 720);
  REQUIRE(explicit_config.config->frames_per_second == 60);
}

void rejects_unknown_repeated_missing_and_malformed_options() {
  REQUIRE(!parse({"--unknown", "value"}).config.has_value());
  REQUIRE(!parse({"--fps", "30", "--fps", "31"}).config.has_value());
  REQUIRE(!parse({"--width"}).config.has_value());
  REQUIRE(!parse({"--height", "360px"}).config.has_value());
  REQUIRE(!parse({"--seconds", "-1"}).config.has_value());
  REQUIRE(!parse({"--audio", "automatic"}).config.has_value());
}

void reports_bounded_sanitized_diagnostics() {
  const auto malformed = parse({"--width", "641"});
  REQUIRE(!malformed.config.has_value());
  REQUIRE(!malformed.diagnostic.empty());
  REQUIRE(malformed.diagnostic.size() <= 256);
  REQUIRE(malformed.diagnostic.find('\n') == std::string::npos);

  const auto hostile = parse({std::string_view{"--bad\noption"}});
  REQUIRE(!hostile.config.has_value());
  REQUIRE(hostile.diagnostic.find('\n') == std::string::npos);
}

}  // namespace

int main() {
  accepts_defaults_and_all_supported_options();
  rejects_unknown_repeated_missing_and_malformed_options();
  reports_bounded_sanitized_diagnostics();
  return EXIT_SUCCESS;
}
