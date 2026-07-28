#include "shareme/rtc/probe_contract.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

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

void accepts_only_bounded_even_video_configs() {
  using namespace std::chrono_literals;
  using shareme::rtc::ProbeConfig;
  using shareme::rtc::validate;

  REQUIRE(!validate(ProbeConfig{}).has_value());
  REQUIRE(validate(ProbeConfig{.width = 0}).has_value());
  REQUIRE(validate(ProbeConfig{.width = 641}).has_value());
  REQUIRE(validate(ProbeConfig{.height = 359}).has_value());
  REQUIRE(validate(ProbeConfig{.frames_per_second = 0}).has_value());
  REQUIRE(validate(ProbeConfig{.frames_per_second = 61}).has_value());
  REQUIRE(validate(ProbeConfig{.run_for = 999ms}).has_value());
  REQUIRE(validate(ProbeConfig{.run_for = 30'001ms}).has_value());
  REQUIRE(validate(ProbeConfig{.connect_timeout = 999ms}).has_value());
  REQUIRE(validate(ProbeConfig{.connect_timeout = 30'001ms}).has_value());
}

void exposes_stable_enum_names() {
  using shareme::rtc::ProbeAudioMode;
  using shareme::rtc::ProbeStatus;
  using shareme::rtc::to_string;

  REQUIRE(to_string(ProbeAudioMode::synthetic) == "synthetic");
  REQUIRE(to_string(ProbeAudioMode::microphone) == "microphone");
  REQUIRE(to_string(ProbeStatus::passed) == "passed");
  REQUIRE(to_string(ProbeStatus::timed_out) == "timed-out");
  REQUIRE(to_string(ProbeStatus::dependency_error) == "dependency-error");
  REQUIRE(to_string(ProbeStatus::permission_denied) == "permission-denied");
  REQUIRE(to_string(ProbeStatus::negotiation_failed) == "negotiation-failed");
  REQUIRE(to_string(ProbeStatus::media_failed) == "media-failed");
  REQUIRE(to_string(ProbeStatus::shutdown_failed) == "shutdown-failed");
}

void serializes_sanitized_probe_evidence() {
  using namespace std::chrono_literals;
  using shareme::rtc::ProbeResult;
  using shareme::rtc::ProbeStatus;
  using shareme::rtc::to_json;

  const ProbeResult result{
      .status = ProbeStatus::passed,
      .connection_time = 125ms,
      .video_frames_sent = 90,
      .video_frames_received = 88,
      .video_frames_dropped = 2,
      .audio_packets_sent = 150,
      .audio_packets_received = 149,
      .audio_bytes_sent = 12'000,
      .audio_bytes_received = 11'920,
      .audio_level = 0.25,
      .round_trip_time_ms = 2.5,
      .selected_candidate_type = "host",
      .diagnostic = "line one\n\"quoted\"",
  };

  const auto json = to_json(result, "locked-revision", "Darwin", "arm64");
  REQUIRE(json.find("\"status\":\"passed\"") != std::string::npos);
  REQUIRE(json.find("\"videoFramesReceived\":88") != std::string::npos);
  REQUIRE(json.find("\"audioLevel\":0.25") != std::string::npos);
  REQUIRE(json.find("\"roundTripTimeMs\":2.5") != std::string::npos);
  REQUIRE(json.find("\"candidateType\":\"host\"") != std::string::npos);
  REQUIRE(json.find("line one\\n\\\"quoted\\\"") != std::string::npos);
  REQUIRE(json.back() == '}');
}

void truncates_diagnostics_and_serializes_missing_metrics_as_null() {
  using shareme::rtc::ProbeResult;
  using shareme::rtc::ProbeStatus;
  using shareme::rtc::to_json;

  ProbeResult result;
  result.status = ProbeStatus::media_failed;
  result.diagnostic = std::string(300, 'x');

  const auto json = to_json(result, "revision", "Windows", "AMD64");
  REQUIRE(json.find("\"audioLevel\":null") != std::string::npos);
  REQUIRE(json.find("\"roundTripTimeMs\":null") != std::string::npos);
  REQUIRE(json.find(std::string(256, 'x')) != std::string::npos);
  REQUIRE(json.find(std::string(257, 'x')) == std::string::npos);
}

}  // namespace

int main() {
  accepts_only_bounded_even_video_configs();
  exposes_stable_enum_names();
  serializes_sanitized_probe_evidence();
  truncates_diagnostics_and_serializes_missing_metrics_as_null();
  return EXIT_SUCCESS;
}
