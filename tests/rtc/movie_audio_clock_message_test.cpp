#include "movie_audio_clock_message.hpp"

#include "shareme/core/movie_audio_pts_mapper.hpp"

#include <QJsonDocument>
#include <QJsonObject>

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

QByteArray compact(QJsonObject object) {
  return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
}

} // namespace

int main() {
  using shareme::tools::MovieAudioClockMessage;
  using shareme::tools::MovieAudioClockTracker;
  using shareme::tools::decode_movie_audio_clock;
  using shareme::tools::encode_movie_audio_clock;

  const MovieAudioClockMessage valid{
      .room_id = QStringLiteral("ABC234"),
      .sequence = 4,
      .playback_generation = 7,
      .audio_epoch = 2,
      .host_source_sequence = 91,
      .media_pts_ms = 12'500,
      .sample_rate = 48'000,
      .channel_count = 2};
  const auto encoded = encode_movie_audio_clock(valid);
  REQUIRE(!encoded.isEmpty());
  const auto decoded =
      decode_movie_audio_clock(encoded, QStringLiteral("ABC234"));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->room_id == valid.room_id);
  REQUIRE(decoded->sequence == valid.sequence);
  REQUIRE(decoded->playback_generation == valid.playback_generation);
  REQUIRE(decoded->audio_epoch == valid.audio_epoch);
  REQUIRE(decoded->host_source_sequence == valid.host_source_sequence);
  REQUIRE(decoded->media_pts_ms == valid.media_pts_ms);
  REQUIRE(decoded->sample_rate == valid.sample_rate);
  REQUIRE(decoded->channel_count == valid.channel_count);

  auto object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("version"), 2);
  REQUIRE(!decode_movie_audio_clock(compact(object), QStringLiteral("ABC234")));
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("roomId"), QStringLiteral("OTHER"));
  REQUIRE(!decode_movie_audio_clock(compact(object), QStringLiteral("ABC234")));

  auto payload = object = QJsonDocument::fromJson(encoded).object();
  payload = object.value(QStringLiteral("payload")).toObject();
  payload.insert(QStringLiteral("sampleRate"), 0);
  object.insert(QStringLiteral("payload"), payload);
  REQUIRE(!decode_movie_audio_clock(compact(object), QStringLiteral("ABC234")));

  object = QJsonDocument::fromJson(encoded).object();
  payload = object.value(QStringLiteral("payload")).toObject();
  payload.insert(QStringLiteral("channelCount"), 1);
  object.insert(QStringLiteral("payload"), payload);
  REQUIRE(!decode_movie_audio_clock(compact(object), QStringLiteral("ABC234")));

  auto unsafe = valid;
  unsafe.sequence = 9'007'199'254'740'992ULL;
  REQUIRE(encode_movie_audio_clock(unsafe).isEmpty());
  unsafe = valid;
  unsafe.media_pts_ms = 9'007'199'254'740'992LL;
  REQUIRE(encode_movie_audio_clock(unsafe).isEmpty());
  object = QJsonDocument::fromJson(encoded).object();
  object.insert(QStringLiteral("sequence"), 9'007'199'254'740'992.0);
  REQUIRE(!decode_movie_audio_clock(compact(object), QStringLiteral("ABC234")));

  MovieAudioClockTracker tracker;
  REQUIRE(tracker.accept(valid));
  auto stale_sequence = valid;
  stale_sequence.sequence = valid.sequence;
  REQUIRE(!tracker.accept(stale_sequence));
  auto generation_regression = valid;
  generation_regression.sequence = valid.sequence + 1;
  generation_regression.playback_generation = valid.playback_generation - 1;
  REQUIRE(!tracker.accept(generation_regression));
  auto format_change = valid;
  format_change.sequence = valid.sequence + 1;
  format_change.sample_rate = 44'100;
  REQUIRE(!tracker.accept(format_change));
  auto later = valid;
  later.sequence = valid.sequence + 1;
  later.playback_generation = valid.playback_generation + 1;
  REQUIRE(tracker.accept(later));

  MovieAudioClockTracker epoch_tracker;
  REQUIRE(epoch_tracker.accept(valid));
  auto epoch_regression = valid;
  epoch_regression.sequence = valid.sequence + 1;
  epoch_regression.audio_epoch = valid.audio_epoch - 1;
  REQUIRE(!epoch_tracker.accept(epoch_regression));

  MovieAudioClockTracker source_tracker;
  REQUIRE(source_tracker.accept(valid));
  auto source_regression = valid;
  source_regression.sequence = valid.sequence + 1;
  source_regression.host_source_sequence = valid.host_source_sequence - 1;
  REQUIRE(!source_tracker.accept(source_regression));

  MovieAudioClockTracker pts_tracker;
  REQUIRE(pts_tracker.accept(valid));
  auto pts_regression = valid;
  pts_regression.sequence = valid.sequence + 1;
  pts_regression.media_pts_ms = valid.media_pts_ms - 1;
  REQUIRE(!pts_tracker.accept(pts_regression));

  shareme::core::MovieAudioPtsMapper mapper;
  const auto anchor = mapper.accept_anchor({
      .control_sequence = valid.sequence,
      .playback_generation = valid.playback_generation,
      .audio_epoch = valid.audio_epoch,
      .host_source_sequence = valid.host_source_sequence,
      .media_pts_ms = valid.media_pts_ms,
      .sample_rate = valid.sample_rate,
      .channel_count = valid.channel_count});
  REQUIRE(anchor.accepted);
  REQUIRE(anchor.confidence ==
          shareme::core::PtsMappingConfidence::provisional);
  REQUIRE(mapper.confidence() ==
          shareme::core::PtsMappingConfidence::provisional);
}
