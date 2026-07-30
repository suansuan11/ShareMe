#include "shareme/rtc/signaled_peer.hpp"
#include <cstdlib>
#include <iostream>
namespace {
void require(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "Requirement failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}
} // namespace
#define REQUIRE(expression) require((expression), #expression, __LINE__)
int main() {
  using shareme::rtc::SignaledAudioMode;
  using shareme::rtc::SignaledRole;
  REQUIRE(!shareme::rtc::signaled_audio_processing_enabled(
      SignaledAudioMode::synthetic));
  REQUIRE(shareme::rtc::signaled_audio_processing_enabled(
      SignaledAudioMode::microphone));
  REQUIRE(shareme::rtc::valid_remote_description(SignaledRole::viewer, "offer",
                                                 "v=0\r\n"));
  REQUIRE(shareme::rtc::valid_remote_description(SignaledRole::host, "answer",
                                                 "v=0\r\n"));
  REQUIRE(!shareme::rtc::valid_remote_description(SignaledRole::host, "offer",
                                                  "v=0\r\n"));
  REQUIRE(!shareme::rtc::valid_remote_description(SignaledRole::viewer,
                                                  "answer", ""));
  REQUIRE(shareme::rtc::valid_remote_candidate(
      "0", 0, "candidate:1 1 udp 1 127.0.0.1 9 typ host"));
  REQUIRE(!shareme::rtc::valid_remote_candidate("", -1, ""));
  auto peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host, .audio_mode = SignaledAudioMode::synthetic},
      {});
  REQUIRE(peer != nullptr);
  REQUIRE(peer->start());
  peer->stop();
  peer.reset();
}
