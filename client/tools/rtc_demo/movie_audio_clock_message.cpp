#include "movie_audio_clock_message.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <limits>

namespace shareme::tools {
namespace {

constexpr std::int64_t kMaximumJsonSafeInteger = 9'007'199'254'740'991;
constexpr std::uint32_t kMovieAudioSampleRate = 48'000;
constexpr std::uint16_t kMovieAudioChannelCount = 2;

[[nodiscard]] bool valid_room(const QString &room) {
  static const QRegularExpression pattern{QStringLiteral("^[A-Z2-7]{6}$")};
  return pattern.match(room).hasMatch();
}

[[nodiscard]] std::optional<std::int64_t>
signed_integer(const QJsonValue &value) {
  if (!value.isDouble())
    return std::nullopt;
  const auto using_min_default =
      value.toInteger(std::numeric_limits<std::int64_t>::min());
  const auto using_max_default =
      value.toInteger(std::numeric_limits<std::int64_t>::max());
  if (using_min_default != using_max_default ||
      using_min_default < -kMaximumJsonSafeInteger ||
      using_min_default > kMaximumJsonSafeInteger)
    return std::nullopt;
  return using_min_default;
}

[[nodiscard]] std::optional<std::uint64_t>
unsigned_integer(const QJsonValue &value, bool positive) {
  const auto parsed = signed_integer(value);
  if (!parsed || *parsed < 0 || (positive && *parsed == 0))
    return std::nullopt;
  return static_cast<std::uint64_t>(*parsed);
}

[[nodiscard]] bool valid_message(const MovieAudioClockMessage &message) {
  return valid_room(message.room_id) && message.sequence != 0 &&
      message.sequence <= static_cast<std::uint64_t>(kMaximumJsonSafeInteger) &&
      message.playback_generation <=
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) &&
      message.audio_epoch <=
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) &&
      message.host_source_sequence != 0 &&
      message.host_source_sequence <=
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) &&
      message.media_pts_ms >= -kMaximumJsonSafeInteger &&
      message.media_pts_ms <= kMaximumJsonSafeInteger &&
      message.sample_rate == kMovieAudioSampleRate &&
      message.channel_count == kMovieAudioChannelCount;
}

} // namespace

QByteArray encode_movie_audio_clock(const MovieAudioClockMessage &message) {
  if (!valid_message(message))
    return {};
  return QJsonDocument(QJsonObject{
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("type"),
                            QStringLiteral("movie-audio-clock")},
                           {QStringLiteral("roomId"), message.room_id},
                           {QStringLiteral("sequence"),
                            static_cast<qint64>(message.sequence)},
                           {QStringLiteral("payload"),
                            QJsonObject{
                                {QStringLiteral("playbackGeneration"),
                                 static_cast<qint64>(
                                     message.playback_generation)},
                                {QStringLiteral("audioEpoch"),
                                 static_cast<qint64>(message.audio_epoch)},
                                {QStringLiteral("hostSourceSequence"),
                                 static_cast<qint64>(
                                     message.host_source_sequence)},
                                {QStringLiteral("mediaPtsMs"),
                                 static_cast<qint64>(message.media_pts_ms)},
                                {QStringLiteral("sampleRate"),
                                 static_cast<qint64>(message.sample_rate)},
                                {QStringLiteral("channelCount"),
                                 static_cast<qint64>(message.channel_count)}}}})
      .toJson(QJsonDocument::Compact);
}

std::optional<MovieAudioClockMessage>
decode_movie_audio_clock(const QByteArray &message,
                         const QString &expected_room) {
  const auto document = QJsonDocument::fromJson(message);
  if (!document.isObject() || !valid_room(expected_room))
    return std::nullopt;
  const auto object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("type")).toString() !=
          QStringLiteral("movie-audio-clock") ||
      object.value(QStringLiteral("roomId")).toString() != expected_room)
    return std::nullopt;

  const auto sequence =
      unsigned_integer(object.value(QStringLiteral("sequence")), true);
  const auto payload_value = object.value(QStringLiteral("payload"));
  if (!sequence || !payload_value.isObject())
    return std::nullopt;
  const auto payload = payload_value.toObject();
  const auto generation = unsigned_integer(
      payload.value(QStringLiteral("playbackGeneration")), false);
  const auto audio_epoch =
      unsigned_integer(payload.value(QStringLiteral("audioEpoch")), false);
  const auto source_sequence =
      unsigned_integer(payload.value(QStringLiteral("hostSourceSequence")),
                       true);
  const auto media_pts =
      signed_integer(payload.value(QStringLiteral("mediaPtsMs")));
  const auto sample_rate =
      unsigned_integer(payload.value(QStringLiteral("sampleRate")), true);
  const auto channel_count =
      unsigned_integer(payload.value(QStringLiteral("channelCount")), true);
  if (!generation || !audio_epoch || !source_sequence || !media_pts ||
      !sample_rate || !channel_count ||
      *sample_rate > std::numeric_limits<std::uint32_t>::max() ||
      *channel_count > std::numeric_limits<std::uint16_t>::max() ||
      *sample_rate != kMovieAudioSampleRate ||
      *channel_count != kMovieAudioChannelCount)
    return std::nullopt;

  return MovieAudioClockMessage{
      .room_id = expected_room,
      .sequence = *sequence,
      .playback_generation = *generation,
      .audio_epoch = *audio_epoch,
      .host_source_sequence = *source_sequence,
      .media_pts_ms = *media_pts,
      .sample_rate = static_cast<std::uint32_t>(*sample_rate),
      .channel_count = static_cast<std::uint16_t>(*channel_count)};
}

bool MovieAudioClockTracker::accept(
    const MovieAudioClockMessage &message) noexcept {
  if (!valid_message(message))
    return false;
  if (last_ &&
      (message.sequence <= last_->sequence ||
       message.playback_generation < last_->playback_generation ||
       message.audio_epoch < last_->audio_epoch ||
       (message.playback_generation == last_->playback_generation &&
        message.audio_epoch == last_->audio_epoch &&
        message.host_source_sequence < last_->host_source_sequence) ||
       message.sample_rate != last_->sample_rate ||
       message.channel_count != last_->channel_count ||
       (message.playback_generation == last_->playback_generation &&
        message.audio_epoch == last_->audio_epoch &&
        message.media_pts_ms < last_->media_pts_ms)))
    return false;
  last_ = message;
  return true;
}

const std::optional<MovieAudioClockMessage> &
MovieAudioClockTracker::last() const noexcept {
  return last_;
}

} // namespace shareme::tools
