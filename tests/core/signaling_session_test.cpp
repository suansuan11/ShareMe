#include "shareme/signaling/signaling_session.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* expression, int line) { if (!condition) { std::cerr << "Requirement failed at line " << line << ": " << expression << '\n'; std::exit(EXIT_FAILURE); } }
#define REQUIRE(expression) require((expression), #expression, __LINE__)

void host_create_and_room_response() {
  shareme::signaling::SignalingSession session;
  const auto request = session.create_room();
  REQUIRE(request.type == "create-room");
  REQUIRE(request.payload == "{\"role\":\"host\"}");
  REQUIRE(session.handle({1, "room-created", "", 1, "{\"roomId\":\"ABCDEF\",\"token\":\"token\"}"}));
  REQUIRE(session.room_id() == "ABCDEF");
  REQUIRE(session.role() == shareme::signaling::Role::host);
  REQUIRE(session.joined());
}

void rejects_relay_before_room() {
  shareme::signaling::SignalingSession session;
  REQUIRE(!session.relay("session-description", "{}" ).has_value());
}

void accepts_movie_audio_relays_after_room() {
  shareme::signaling::SignalingSession session;
  static_cast<void>(session.create_room());
  REQUIRE(session.handle({1, "room-created", "", 1,
                          "{\"roomId\":\"ABCDEF\",\"token\":\"token\"}"}));
  for (const auto *type : {"movie-audio-session-description",
                           "movie-audio-ice-candidate"}) {
    const auto relay = session.relay(type, "{}");
    REQUIRE(relay.has_value());
    REQUIRE(relay->type == type);
    REQUIRE(relay->room_id == "ABCDEF");
  }
  REQUIRE(!session.relay("movie-audio-unknown", "{}").has_value());
}
}
int main() {
  host_create_and_room_response();
  rejects_relay_before_room();
  accepts_movie_audio_relays_after_room();
}
