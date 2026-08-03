#include "playback_state.hpp"

#include <QJsonDocument>
#include <QJsonObject>

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
  using shareme::tools::PlaybackState;
  using shareme::tools::PlaybackStateTracker;
  using shareme::tools::decode_playback_state;
  using shareme::tools::encode_playback_state;

  const PlaybackState playing{.room_id = QStringLiteral("ABC234"),
                              .sequence = 1,
                              .state = QStringLiteral("playing"),
                              .media_pts_ms = 12'500,
                              .effective_at_host_time_ms = -91,
                              .rate = 1.0,
                              .generation = 4};
  const auto encoded = encode_playback_state(playing);
  const auto decoded = decode_playback_state(encoded, QStringLiteral("ABC234"));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->sequence == playing.sequence);
  REQUIRE(decoded->state == playing.state);
  REQUIRE(decoded->media_pts_ms == playing.media_pts_ms);
  REQUIRE(decoded->effective_at_host_time_ms == playing.effective_at_host_time_ms);
  REQUIRE(decoded->generation == playing.generation);

  const auto make_message = [](QJsonObject object) {
    return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
  };
  auto object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("version"), 2);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("roomId"), QStringLiteral("OTHER"));
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("sequence"), 0);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  auto payload = QJsonDocument::fromJson(encoded).object().value(QStringLiteral("payload")).toObject();
  payload.insert(QStringLiteral("state"), QStringLiteral("stopped"));
  object.insert(QStringLiteral("payload"), payload);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  payload = object.value(QStringLiteral("payload")).toObject();
  payload.insert(QStringLiteral("rate"), 2.1);
  object.insert(QStringLiteral("payload"), payload);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));

  REQUIRE(!decode_playback_state(encoded, QStringLiteral("ROOM1")));
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("sequence"), 1.5);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("sequence"), 9'007'199'254'740'992.0);
  REQUIRE(!decode_playback_state(make_message(object), QStringLiteral("ABC234")));
  auto invalid_encode = playing;
  invalid_encode.room_id = QStringLiteral("ROOM1");
  REQUIRE(encode_playback_state(invalid_encode).isEmpty());
  invalid_encode = playing;
  invalid_encode.sequence = 9'007'199'254'740'992ULL;
  REQUIRE(encode_playback_state(invalid_encode).isEmpty());

  PlaybackStateTracker tracker;
  REQUIRE(tracker.accept(playing));
  REQUIRE(!tracker.accept(playing));
  auto stale_sequence = playing;
  stale_sequence.sequence = 0;
  REQUIRE(!tracker.accept(stale_sequence));
  auto later = playing;
  later.sequence = 2;
  later.generation = 5;
  REQUIRE(tracker.accept(later));
  auto newer_generation_stale_sequence = later;
  newer_generation_stale_sequence.sequence = 1;
  newer_generation_stale_sequence.generation = 6;
  REQUIRE(!tracker.accept(newer_generation_stale_sequence));
  auto stale_generation = later;
  stale_generation.sequence = 3;
  stale_generation.generation = 4;
  REQUIRE(!tracker.accept(stale_generation));
  REQUIRE(tracker.last().has_value());
  REQUIRE(tracker.last()->generation == 5);
}
