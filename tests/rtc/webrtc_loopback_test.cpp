#include "shareme/rtc/webrtc_probe.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition) {
    return;
  }
  std::cerr << "requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

} // namespace

int main() {
  using namespace std::chrono_literals;

  const auto result =
      shareme::rtc::run_webrtc_probe(shareme::rtc::ProbeConfig{});

  REQUIRE(result.status == shareme::rtc::ProbeStatus::passed);
  REQUIRE(result.connection_time <= 10s);
  REQUIRE(result.video_frames_received >= 30);
  REQUIRE(result.audio_packets_sent > 0);
  REQUIRE(result.audio_packets_received > 0);
  REQUIRE(result.audio_bytes_sent > 0);
  REQUIRE(result.audio_bytes_received > 0);
  REQUIRE(result.selected_candidate_type == "host");
  return EXIT_SUCCESS;
}
