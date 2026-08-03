#include "playback_state.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace shareme::tools {
namespace {
constexpr std::int64_t kMaximumJsonSafeInteger = 9'007'199'254'740'991;

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
positive_unsigned_integer(const QJsonValue &value, bool positive) {
  const auto parsed = signed_integer(value);
  if (!parsed || *parsed < 0 || (positive && *parsed == 0))
    return std::nullopt;
  return static_cast<std::uint64_t>(*parsed);
}
} // namespace

QByteArray encode_playback_state(const PlaybackState &state) {
  if (!valid_room(state.room_id) || state.sequence == 0 ||
      state.sequence > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      (state.state != QStringLiteral("playing") &&
       state.state != QStringLiteral("paused")) ||
      state.media_pts_ms < -kMaximumJsonSafeInteger ||
      state.media_pts_ms > kMaximumJsonSafeInteger ||
      state.effective_at_host_time_ms < -kMaximumJsonSafeInteger ||
      state.effective_at_host_time_ms > kMaximumJsonSafeInteger ||
      !std::isfinite(state.rate) || state.rate < 0.5 || state.rate > 2.0 ||
      state.generation >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger))
    return {};
  return QJsonDocument(QJsonObject{
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("type"), QStringLiteral("playback-state")},
                           {QStringLiteral("roomId"), state.room_id},
                           {QStringLiteral("sequence"), static_cast<qint64>(state.sequence)},
                           {QStringLiteral("payload"),
                            QJsonObject{{QStringLiteral("state"), state.state},
                                        {QStringLiteral("mediaPtsMs"), static_cast<qint64>(state.media_pts_ms)},
                                        {QStringLiteral("effectiveAtHostTimeMs"), static_cast<qint64>(state.effective_at_host_time_ms)},
                                        {QStringLiteral("rate"), state.rate},
                                        {QStringLiteral("generation"), static_cast<qint64>(state.generation)}}}})
      .toJson(QJsonDocument::Compact);
}

std::optional<PlaybackState> decode_playback_state(const QByteArray &message,
                                                   const QString &expected_room) {
  const auto document = QJsonDocument::fromJson(message);
  if (!document.isObject() || !valid_room(expected_room))
    return std::nullopt;
  const auto object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("type")).toString() != QStringLiteral("playback-state") ||
      object.value(QStringLiteral("roomId")).toString() != expected_room)
    return std::nullopt;
  const auto sequence = positive_unsigned_integer(object.value(QStringLiteral("sequence")), true);
  const auto payload_value = object.value(QStringLiteral("payload"));
  if (!sequence || !payload_value.isObject())
    return std::nullopt;
  const auto payload = payload_value.toObject();
  const auto state = payload.value(QStringLiteral("state")).toString();
  const auto media_pts = signed_integer(payload.value(QStringLiteral("mediaPtsMs")));
  const auto effective_at = signed_integer(payload.value(QStringLiteral("effectiveAtHostTimeMs")));
  const auto generation = positive_unsigned_integer(payload.value(QStringLiteral("generation")), false);
  const auto rate = payload.value(QStringLiteral("rate"));
  if ((state != QStringLiteral("playing") && state != QStringLiteral("paused")) ||
      !media_pts || !effective_at || !generation || !rate.isDouble() ||
      !std::isfinite(rate.toDouble()) || rate.toDouble() < 0.5 || rate.toDouble() > 2.0)
    return std::nullopt;
  return PlaybackState{.room_id = expected_room,
                       .sequence = *sequence,
                       .state = state,
                       .media_pts_ms = *media_pts,
                       .effective_at_host_time_ms = *effective_at,
                       .rate = rate.toDouble(),
                       .generation = *generation};
}

bool PlaybackStateTracker::accept(const PlaybackState &state) noexcept {
  if (last_ && (state.sequence <= last_->sequence ||
                state.generation < last_->generation))
    return false;
  last_ = state;
  return true;
}

const std::optional<PlaybackState> &PlaybackStateTracker::last() const noexcept {
  return last_;
}

} // namespace shareme::tools
