#include "drift_metrics_jsonl.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>

#include <array>
#include <utility>

namespace shareme::tools {
namespace {

QString phase_name(shareme::core::DriftPhase phase) {
  switch (phase) {
  case shareme::core::DriftPhase::warmup:
    return QStringLiteral("warmup");
  case shareme::core::DriftPhase::steady:
    return QStringLiteral("steady");
  case shareme::core::DriftPhase::paused:
    return QStringLiteral("paused");
  case shareme::core::DriftPhase::post_resume:
    return QStringLiteral("post-resume");
  case shareme::core::DriftPhase::post_forward_seek:
    return QStringLiteral("post-forward-seek");
  case shareme::core::DriftPhase::post_backward_seek:
    return QStringLiteral("post-backward-seek");
  case shareme::core::DriftPhase::cooldown:
    return QStringLiteral("cooldown");
  }
  return QStringLiteral("unknown");
}

QString action_name(shareme::core::SyncAction action) {
  switch (action) {
  case shareme::core::SyncAction::none:
    return QStringLiteral("none");
  case shareme::core::SyncAction::adjust_buffer:
    return QStringLiteral("adjust-buffer");
  case shareme::core::SyncAction::adjust_rate:
    return QStringLiteral("adjust-rate");
  case shareme::core::SyncAction::hard_resync:
    return QStringLiteral("hard-resync");
  }
  return QStringLiteral("unknown");
}

QJsonValue unsigned_value(std::uint64_t value) {
  return QJsonValue::fromVariant(
      QVariant::fromValue(static_cast<qulonglong>(value)));
}

QString safe_candidate_type(const std::string& candidate_type) {
  const auto value = QString::fromStdString(candidate_type).trimmed().toLower();
  if (value == QStringLiteral("host") ||
      value == QStringLiteral("srflx") ||
      value == QStringLiteral("relay") ||
      value == QStringLiteral("prflx")) {
    return value;
  }
  return QStringLiteral("unknown");
}

QJsonObject sample_object(const shareme::core::DriftSample& sample) {
  return {
      {"kind", "sample"},
      {"schemaVersion", static_cast<int>(sample.schema_version)},
      {"captureTimeMs", sample.capture_time_ms},
      {"sampleIndex", unsigned_value(sample.sample_index)},
      {"reportSequence", unsigned_value(sample.report_sequence)},
      {"generation", unsigned_value(sample.generation)},
      {"hostPtsMs", sample.host_pts_ms},
      {"viewerPtsMs", sample.viewer_pts_ms},
      {"deltaMs", sample.delta_ms},
      {"bufferMs", sample.buffer_ms},
      {"playing", sample.playing},
      {"action", action_name(sample.action)},
      {"phase", phase_name(sample.phase)},
      {"candidateType", safe_candidate_type(sample.selected_candidate_type)},
  };
}

QJsonObject count_object(const std::array<std::size_t, 7>& counts) {
  const std::array<std::pair<const char*, shareme::core::DriftPhase>, 7> names =
      {{{"warmup", shareme::core::DriftPhase::warmup},
        {"steady", shareme::core::DriftPhase::steady},
        {"paused", shareme::core::DriftPhase::paused},
        {"post-resume", shareme::core::DriftPhase::post_resume},
        {"post-forward-seek", shareme::core::DriftPhase::post_forward_seek},
        {"post-backward-seek", shareme::core::DriftPhase::post_backward_seek},
        {"cooldown", shareme::core::DriftPhase::cooldown}}};
  QJsonObject result;
  for (const auto& [name, phase] : names)
    result.insert(QString::fromUtf8(name), unsigned_value(counts[static_cast<std::size_t>(phase)]));
  return result;
}

QJsonObject action_count_object(const std::array<std::size_t, 4>& counts) {
  const std::array<std::pair<const char*, shareme::core::SyncAction>, 4> names =
      {{{"none", shareme::core::SyncAction::none},
        {"adjust-buffer", shareme::core::SyncAction::adjust_buffer},
        {"adjust-rate", shareme::core::SyncAction::adjust_rate},
        {"hard-resync", shareme::core::SyncAction::hard_resync}}};
  QJsonObject result;
  for (const auto& [name, action] : names)
    result.insert(QString::fromUtf8(name), unsigned_value(counts[static_cast<std::size_t>(action)]));
  return result;
}

QJsonObject summary_object(const shareme::core::DriftSummary& summary) {
  QJsonArray recoveries;
  for (const auto& recovery : summary.recoveries) {
    recoveries.append(QJsonObject{
        {"phase", phase_name(recovery.phase)},
        {"complete", recovery.complete},
        {"durationMs", recovery.duration_ms},
    });
  }
  return {
      {"kind", "summary"},
      {"schemaVersion", static_cast<int>(shareme::core::DriftSample::kSchemaVersion)},
      {"complete", summary.complete},
      {"acceptedSamples", unsigned_value(summary.accepted_samples)},
      {"rejectedSamples", unsigned_value(summary.rejected_samples)},
      {"sampleIndexRegressions", unsigned_value(summary.sample_index_regressions)},
      {"sequenceRegressions", unsigned_value(summary.sequence_regressions)},
      {"staleGenerationRejections", unsigned_value(summary.stale_generation_rejections)},
      {"generationTransitions", unsigned_value(summary.generation_transitions)},
      {"phaseCounts", count_object(summary.phase_counts)},
      {"actionCounts", action_count_object(summary.action_counts)},
      {"signedMinMs", summary.signed_min_ms},
      {"signedMaxMs", summary.signed_max_ms},
      {"signedMeanMs", summary.signed_mean_ms},
      {"absoluteP50Ms", unsigned_value(summary.absolute_p50_ms)},
      {"absoluteP95Ms", unsigned_value(summary.absolute_p95_ms)},
      {"absoluteP99Ms", unsigned_value(summary.absolute_p99_ms)},
      {"absoluteMaxMs", unsigned_value(summary.absolute_max_ms)},
      {"firstCaptureTimeMs", summary.first_capture_time_ms},
      {"lastCaptureTimeMs", summary.last_capture_time_ms},
      {"coveredDurationMs", summary.covered_duration_ms},
      {"reportGapCount", unsigned_value(summary.report_gap_count)},
      {"largestReportGapMs", summary.largest_report_gap_ms},
      {"hardResyncCandidateEpisodes", unsigned_value(summary.hard_resync_candidate_episodes)},
      {"recoveries", recoveries},
  };
}

}  // namespace

DriftMetricsJsonlWriter::DriftMetricsJsonlWriter(QString final_path)
    : final_path_(std::move(final_path)), file_() {}

bool DriftMetricsJsonlWriter::open() {
  if (opened_ || finalized_)
    return fail(QStringLiteral("already-open"));
  if (final_path_.isEmpty())
    return fail(QStringLiteral("empty-output-path"));

  const QFileInfo final_info(final_path_);
  if (final_info.exists())
    return fail(QStringLiteral("output-exists"));
  if (!final_info.dir().exists())
    return fail(QStringLiteral("output-directory-missing"));

  temporary_path_ = final_path_ + QStringLiteral(".partial");
  if (QFile::exists(temporary_path_))
    return fail(QStringLiteral("temporary-output-exists"));
  file_.setFileName(temporary_path_);
  if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return fail(QStringLiteral("open-failed"));
  opened_ = true;
  return true;
}

bool DriftMetricsJsonlWriter::append(const shareme::core::DriftSample& sample) {
  if (!opened_ || finalized_)
    return fail(QStringLiteral("not-open"));
  if (have_last_sample_ && sample.sample_index <= last_sample_index_)
    return fail(QStringLiteral("sample-index-regression"));

  const auto line = QJsonDocument(sample_object(sample)).toJson(QJsonDocument::Compact);
  if (file_.write(line) != line.size() || file_.write("\n", 1) != 1)
    return fail(QStringLiteral("write-failed"));
  have_last_sample_ = true;
  last_sample_index_ = sample.sample_index;
  return true;
}

bool DriftMetricsJsonlWriter::finalize(const shareme::core::DriftSummary& summary) {
  if (!opened_ || finalized_)
    return fail(QStringLiteral("not-open"));
  const auto line = QJsonDocument(summary_object(summary)).toJson(QJsonDocument::Compact);
  if (file_.write(line) != line.size() || file_.write("\n", 1) != 1 ||
      !file_.flush()) {
    return fail(QStringLiteral("write-failed"));
  }
  file_.close();
  if (!QFile::rename(temporary_path_, final_path_))
    return fail(QStringLiteral("atomic-rename-failed"));
  finalized_ = true;
  return true;
}

QString DriftMetricsJsonlWriter::failure_category() const {
  return failure_category_;
}

bool DriftMetricsJsonlWriter::fail(QString category) {
  if (failure_category_.isEmpty())
    failure_category_ = std::move(category);
  if (file_.isOpen())
    file_.close();
  opened_ = false;
  return false;
}

}  // namespace shareme::tools
