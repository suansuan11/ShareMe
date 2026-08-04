#include "playout_report.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace shareme::tools {
namespace {
constexpr std::int64_t kMaximumJsonSafeInteger = 9'007'199'254'740'991;
constexpr std::int64_t kMaximumBufferMs = 10'000;
constexpr std::int64_t kRtpTicksPerMillisecond = 90;
constexpr std::int64_t kMaximumRtpDelta =
    kMaximumBufferMs * kRtpTicksPerMillisecond;

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

[[nodiscard]] bool checked_add(std::int64_t left, std::int64_t right,
                               std::int64_t &result) noexcept {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
    return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_subtract(std::int64_t left, std::int64_t right,
                                    std::int64_t &result) noexcept {
  if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
      (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right))
    return false;
  result = left - right;
  return true;
}

[[nodiscard]] std::int64_t rtp_ticks_to_milliseconds(
    std::int64_t ticks) noexcept {
  if (ticks >= 0)
    return (ticks + kRtpTicksPerMillisecond / 2) /
           kRtpTicksPerMillisecond;
  return -((-ticks + kRtpTicksPerMillisecond / 2) /
           kRtpTicksPerMillisecond);
}
} // namespace

QByteArray encode_playout_report(const PlayoutReport &report) {
  if (!valid_room(report.room_id) || report.sequence == 0 ||
      report.sequence > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.rendered_pts_ms < -kMaximumJsonSafeInteger ||
      report.rendered_pts_ms > kMaximumJsonSafeInteger ||
      report.buffer_ms < 0 || report.buffer_ms > kMaximumBufferMs ||
      report.receive_time_ms < -kMaximumJsonSafeInteger ||
      report.receive_time_ms > kMaximumJsonSafeInteger ||
      report.generation >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger))
    return {};
  return QJsonDocument(QJsonObject{
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("type"),
                            QStringLiteral("playout-report")},
                           {QStringLiteral("roomId"), report.room_id},
                           {QStringLiteral("sequence"),
                            static_cast<qint64>(report.sequence)},
                           {QStringLiteral("payload"),
                            QJsonObject{{QStringLiteral("renderedPtsMs"),
                                         static_cast<qint64>(report.rendered_pts_ms)},
                                        {QStringLiteral("bufferMs"),
                                         static_cast<qint64>(report.buffer_ms)},
                                        {QStringLiteral("receiveTimeMs"),
                                         static_cast<qint64>(report.receive_time_ms)},
                                        {QStringLiteral("generation"),
                                         static_cast<qint64>(report.generation)}}}})
      .toJson(QJsonDocument::Compact);
}

std::optional<PlayoutReport>
decode_playout_report(const QByteArray &message, const QString &expected_room) {
  const auto document = QJsonDocument::fromJson(message);
  if (!document.isObject() || !valid_room(expected_room))
    return std::nullopt;
  const auto object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("type")).toString() !=
          QStringLiteral("playout-report") ||
      object.value(QStringLiteral("roomId")).toString() != expected_room)
    return std::nullopt;
  const auto sequence =
      unsigned_integer(object.value(QStringLiteral("sequence")), true);
  const auto payload_value = object.value(QStringLiteral("payload"));
  if (!sequence || !payload_value.isObject())
    return std::nullopt;
  const auto payload = payload_value.toObject();
  const auto rendered =
      signed_integer(payload.value(QStringLiteral("renderedPtsMs")));
  const auto buffer = signed_integer(payload.value(QStringLiteral("bufferMs")));
  const auto receive =
      signed_integer(payload.value(QStringLiteral("receiveTimeMs")));
  const auto generation =
      unsigned_integer(payload.value(QStringLiteral("generation")), false);
  if (!rendered || !buffer || *buffer < 0 || *buffer > kMaximumBufferMs ||
      !receive || !generation)
    return std::nullopt;
  return PlayoutReport{.room_id = expected_room,
                       .sequence = *sequence,
                       .rendered_pts_ms = *rendered,
                       .buffer_ms = *buffer,
                       .receive_time_ms = *receive,
                       .generation = *generation};
}

std::optional<RenderedPlayoutSample>
reconcile_rendered_frame(const PlaybackState &anchor,
                         std::uint32_t rendered_rtp_timestamp,
                         std::int64_t elapsed_since_anchor_ms) noexcept {
  if (!anchor.video_anchor_media_pts_ms || !anchor.video_rtp_timestamp ||
      elapsed_since_anchor_ms < 0 || elapsed_since_anchor_ms > kMaximumBufferMs)
    return std::nullopt;
  const auto raw_delta = rendered_rtp_timestamp - *anchor.video_rtp_timestamp;
  const auto signed_delta =
      raw_delta <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
          ? static_cast<std::int64_t>(raw_delta)
          : static_cast<std::int64_t>(raw_delta) - (std::int64_t{1} << 32);
  if (signed_delta < -kMaximumRtpDelta || signed_delta > kMaximumRtpDelta)
    return std::nullopt;
  std::int64_t rendered_pts = 0;
  if (!checked_add(*anchor.video_anchor_media_pts_ms,
                   rtp_ticks_to_milliseconds(signed_delta), rendered_pts))
    return std::nullopt;
  const auto projected = anchor.state == QStringLiteral("playing")
                             ? elapsed_since_anchor_ms
                             : 0;
  std::int64_t expected_pts = 0;
  if (!checked_add(anchor.media_pts_ms, projected, expected_pts))
    return std::nullopt;
  std::int64_t viewer_delta = 0;
  if (!checked_subtract(expected_pts, rendered_pts, viewer_delta))
    return std::nullopt;
  const auto buffer_ms =
      std::clamp(viewer_delta, std::int64_t{0}, kMaximumBufferMs);
  return RenderedPlayoutSample{.rendered_pts_ms = rendered_pts,
                               .buffer_ms = buffer_ms,
                               .generation = anchor.generation};
}

std::optional<std::int64_t>
viewer_delta_ms(std::int64_t host_pts_ms,
                std::int64_t rendered_pts_ms) noexcept {
  std::int64_t result = 0;
  if (!checked_subtract(host_pts_ms, rendered_pts_ms, result))
    return std::nullopt;
  return result;
}

bool PlayoutReportTracker::accept(
    const PlayoutReport &report,
    std::uint64_t expected_generation) noexcept {
  if (report.generation != expected_generation ||
      (last_ && report.sequence <= last_->sequence))
    return false;
  last_ = report;
  return true;
}

const std::optional<PlayoutReport> &PlayoutReportTracker::last() const noexcept {
  return last_;
}

bool RenderedPlayoutTracker::accept(
    const PlaybackState &anchor,
    const RenderedPlayoutSample &sample) noexcept {
  if (sample.generation != anchor.generation)
    return false;
  if (!last_ || last_->generation != sample.generation) {
    if (!anchor.video_anchor_media_pts_ms ||
        sample.rendered_pts_ms < *anchor.video_anchor_media_pts_ms)
      return false;
  } else if (sample.rendered_pts_ms < last_->rendered_pts_ms) {
    return false;
  }
  last_ = sample;
  return true;
}

void RenderedPlayoutTracker::reset() noexcept { last_.reset(); }

const std::optional<RenderedPlayoutSample> &
RenderedPlayoutTracker::last() const noexcept {
  return last_;
}

} // namespace shareme::tools
