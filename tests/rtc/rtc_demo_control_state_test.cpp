#include "rtc_control_state.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

} // namespace

int main() {
  shareme::tools::RtcControlState state;
  REQUIRE(!state.microphone_muted());
  REQUIRE(!state.speaker_muted());
  REQUIRE(!state.session_ended());

  REQUIRE(state.set_microphone_muted(true));
  REQUIRE(state.microphone_muted());
  REQUIRE(!state.set_microphone_muted(true));
  REQUIRE(state.set_speaker_muted(true));
  REQUIRE(state.speaker_muted());

  REQUIRE(state.finish_session());
  REQUIRE(state.session_ended());
  REQUIRE(!state.finish_session());
  REQUIRE(!state.set_microphone_muted(false));
  REQUIRE(!state.set_speaker_muted(false));
  return EXIT_SUCCESS;
}
