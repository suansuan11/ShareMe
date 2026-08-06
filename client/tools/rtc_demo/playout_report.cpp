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

[[nodiscard]] bool valid_suggested_action(const QString &action) {
  return action == QStringLiteral("none") ||
      action == QStringLiteral("early-hold") ||
      action == QStringLiteral("late-drop") ||
      action == QStringLiteral("hard-resync-candidate") ||
      action == QStringLiteral("clock-blocked");
}

[[nodiscard]] bool valid_applied_action(const QString &action) {
  return action == QStringLiteral("none") ||
      action == QStringLiteral("pass-through") ||
      action == QStringLiteral("present") ||
      action == QStringLiteral("late-drop") ||
      action == QStringLiteral("hard-resync");
}

[[nodiscard]] bool valid_clock_confidence(const QString &confidence) {
  return confidence == QStringLiteral("unavailable") ||
      confidence == QStringLiteral("provisional") ||
      confidence == QStringLiteral("locked") ||
      confidence == QStringLiteral("degraded") ||
      confidence == QStringLiteral("invalid");
}

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
  const auto suggested_action = report.viewer_suggested_action.isEmpty()
                                    ? QStringLiteral("none")
                                    : report.viewer_suggested_action;
  const auto applied_action = report.viewer_applied_action.isEmpty()
                                  ? QStringLiteral("none")
                                  : report.viewer_applied_action;
  const auto clock_confidence = report.audio_clock_confidence.isEmpty()
                                    ? QStringLiteral("unavailable")
                                    : report.audio_clock_confidence;
  if (!valid_room(report.room_id) || report.sequence == 0 ||
      report.sequence > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.rendered_pts_ms < -kMaximumJsonSafeInteger ||
      report.rendered_pts_ms > kMaximumJsonSafeInteger ||
      report.buffer_ms < 0 || report.buffer_ms > kMaximumBufferMs ||
      report.receive_time_ms < -kMaximumJsonSafeInteger ||
      report.receive_time_ms > kMaximumJsonSafeInteger ||
      report.generation >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.audio_playout_pts_ms < -kMaximumJsonSafeInteger ||
      report.audio_playout_pts_ms > kMaximumJsonSafeInteger ||
      report.logical_consumed_frames >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.renderer_queue_duration >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.device_queue_duration >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.route_generation >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      report.renderer_clock_epoch >
          static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      !valid_suggested_action(suggested_action) ||
      !valid_applied_action(applied_action) ||
      !valid_clock_confidence(clock_confidence))
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
                                          static_cast<qint64>(report.generation)},
                                         {QStringLiteral("viewerSuggestedAction"),
                                          suggested_action},
                                         {QStringLiteral("viewerAppliedAction"),
                                          applied_action},
                                         {QStringLiteral("audioClockConfidence"),
                                          clock_confidence},
                                         {QStringLiteral("audioPlayoutPtsMs"),
                                          static_cast<qint64>(
                                              report.audio_playout_pts_ms)},
                                         {QStringLiteral("logicalConsumedFrames"),
                                          static_cast<qint64>(
                                              report.logical_consumed_frames)},
                                         {QStringLiteral("rendererQueueDuration"),
                                          static_cast<qint64>(
                                              report.renderer_queue_duration)},
                                         {QStringLiteral("deviceQueueDuration"),
                                          static_cast<qint64>(
                                              report.device_queue_duration)},
                                         {QStringLiteral("routeGeneration"),
                                          static_cast<qint64>(
                                              report.route_generation)},
                                         {QStringLiteral("rendererClockEpoch"),
                                          static_cast<qint64>(
                                              report.renderer_clock_epoch)}}}})
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
  const auto has_extended_fields =
      payload.contains(QStringLiteral("viewerSuggestedAction")) ||
      payload.contains(QStringLiteral("viewerAppliedAction")) ||
      payload.contains(QStringLiteral("audioClockConfidence")) ||
      payload.contains(QStringLiteral("audioPlayoutPtsMs")) ||
      payload.contains(QStringLiteral("logicalConsumedFrames")) ||
      payload.contains(QStringLiteral("rendererQueueDuration")) ||
      payload.contains(QStringLiteral("deviceQueueDuration")) ||
      payload.contains(QStringLiteral("routeGeneration")) ||
      payload.contains(QStringLiteral("rendererClockEpoch"));
  const auto extended_field_count =
      static_cast<int>(payload.contains(QStringLiteral("viewerSuggestedAction"))) +
      static_cast<int>(payload.contains(QStringLiteral("viewerAppliedAction"))) +
      static_cast<int>(payload.contains(QStringLiteral("audioClockConfidence"))) +
      static_cast<int>(payload.contains(QStringLiteral("audioPlayoutPtsMs"))) +
      static_cast<int>(payload.contains(QStringLiteral("logicalConsumedFrames"))) +
      static_cast<int>(payload.contains(QStringLiteral("rendererQueueDuration"))) +
      static_cast<int>(payload.contains(QStringLiteral("deviceQueueDuration"))) +
      static_cast<int>(payload.contains(QStringLiteral("routeGeneration"))) +
      static_cast<int>(payload.contains(QStringLiteral("rendererClockEpoch")));
  if (!rendered || !buffer || *buffer < 0 || *buffer > kMaximumBufferMs ||
      !receive || !generation || (has_extended_fields && extended_field_count != 9))
    return std::nullopt;

  const auto suggested_action = has_extended_fields
                                    ? payload.value(QStringLiteral("viewerSuggestedAction"))
                                          .toString()
                                    : QStringLiteral("none");
  const auto applied_action = has_extended_fields
                                  ? payload.value(QStringLiteral("viewerAppliedAction"))
                                        .toString()
                                  : QStringLiteral("none");
  const auto clock_confidence = has_extended_fields
                                    ? payload.value(QStringLiteral("audioClockConfidence"))
                                          .toString()
                                    : QStringLiteral("unavailable");
  const auto audio_pts = has_extended_fields
                             ? signed_integer(payload.value(
                                   QStringLiteral("audioPlayoutPtsMs")))
                             : std::optional<std::int64_t>{0};
  const auto logical_consumed = has_extended_fields
                                    ? unsigned_integer(payload.value(
                                          QStringLiteral("logicalConsumedFrames")),
                                      false)
                                    : std::optional<std::uint64_t>{0};
  const auto renderer_queue = has_extended_fields
                                  ? unsigned_integer(payload.value(
                                        QStringLiteral("rendererQueueDuration")),
                                    false)
                                  : std::optional<std::uint64_t>{0};
  const auto device_queue = has_extended_fields
                                ? unsigned_integer(payload.value(
                                      QStringLiteral("deviceQueueDuration")),
                                  false)
                                : std::optional<std::uint64_t>{0};
  const auto route_generation = has_extended_fields
                                    ? unsigned_integer(payload.value(
                                          QStringLiteral("routeGeneration")),
                                      false)
                                    : std::optional<std::uint64_t>{0};
  const auto renderer_epoch = has_extended_fields
                                  ? unsigned_integer(payload.value(
                                        QStringLiteral("rendererClockEpoch")),
                                    false)
                                  : std::optional<std::uint64_t>{0};
  if (!valid_suggested_action(suggested_action) ||
      !valid_applied_action(applied_action) ||
      !valid_clock_confidence(clock_confidence) || !audio_pts ||
      !logical_consumed || !renderer_queue || !device_queue ||
      !route_generation || !renderer_epoch ||
      *logical_consumed > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      *renderer_queue > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      *device_queue > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      *route_generation > static_cast<std::uint64_t>(kMaximumJsonSafeInteger) ||
      *renderer_epoch > static_cast<std::uint64_t>(kMaximumJsonSafeInteger))
    return std::nullopt;
  return PlayoutReport{.room_id = expected_room,
                       .sequence = *sequence,
                       .rendered_pts_ms = *rendered,
                       .buffer_ms = *buffer,
                       .receive_time_ms = *receive,
                       .generation = *generation,
                       .viewer_suggested_action = suggested_action,
                       .viewer_applied_action = applied_action,
                       .audio_clock_confidence = clock_confidence,
                       .audio_playout_pts_ms = *audio_pts,
                       .logical_consumed_frames = *logical_consumed,
                       .renderer_queue_duration = *renderer_queue,
                       .device_queue_duration = *device_queue,
                       .route_generation = *route_generation,
                       .renderer_clock_epoch = *renderer_epoch};
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
