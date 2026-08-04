#include "drift_metrics_jsonl.hpp"

#include "shareme/core/drift_metrics.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::core::DriftSample sample(std::uint64_t index) {
  return {
      .capture_time_ms = static_cast<std::int64_t>(index * 250),
      .sample_index = index,
      .report_sequence = index + 10,
      .generation = 4,
      .host_pts_ms = 20'000 + static_cast<std::int64_t>(index * 250),
      .viewer_pts_ms = 20'000 + static_cast<std::int64_t>(index * 250) - 40,
      .delta_ms = 40,
      .buffer_ms = 90,
      .playing = true,
      .action = shareme::core::SyncAction::none,
      .phase = shareme::core::DriftPhase::steady,
      .selected_candidate_type = "relay",
  };
}

void writes_samples_and_summary_as_atomic_sanitized_jsonl() {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("drift.jsonl"));
  shareme::tools::DriftMetricsJsonlWriter writer(path);
  REQUIRE(writer.open());
  REQUIRE(!QFile::exists(path));
  REQUIRE(writer.append(sample(0)));
  REQUIRE(writer.append(sample(1)));

  shareme::core::DriftAggregator aggregator;
  REQUIRE(aggregator.accept(sample(0)));
  REQUIRE(aggregator.accept(sample(1)));
  aggregator.complete_run();
  REQUIRE(writer.finalize(aggregator.summary()));

  QFile file(path);
  REQUIRE(file.open(QIODevice::ReadOnly));
  const auto content = file.readAll();
  const auto lines = content.split('\n');
  REQUIRE(lines.size() == 4);
  REQUIRE(lines.at(3).isEmpty());
  const auto first = QJsonDocument::fromJson(lines.at(0)).object();
  const auto second = QJsonDocument::fromJson(lines.at(1)).object();
  const auto summary = QJsonDocument::fromJson(lines.at(2)).object();
  REQUIRE(first.value("kind") == QStringLiteral("sample"));
  REQUIRE(first.value("schemaVersion").toInt() == 1);
  REQUIRE(first.value("sampleIndex").toInteger() == 0);
  REQUIRE(second.value("sampleIndex").toInteger() == 1);
  REQUIRE(summary.value("kind") == QStringLiteral("summary"));
  REQUIRE(summary.value("complete").toBool());
  REQUIRE(summary.value("acceptedSamples").toInt() == 2);
  REQUIRE(!content.contains("room"));
}

void rejects_existing_output_and_unsanitized_candidate_values() {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("existing.jsonl"));
  QFile existing(path);
  REQUIRE(existing.open(QIODevice::WriteOnly));
  existing.write("pre-existing");
  existing.close();

  shareme::tools::DriftMetricsJsonlWriter existing_writer(path);
  REQUIRE(!existing_writer.open());
  REQUIRE(existing_writer.failure_category() == QStringLiteral("output-exists"));

  const auto sanitized_path = directory.filePath(QStringLiteral("safe.jsonl"));
  shareme::tools::DriftMetricsJsonlWriter writer(sanitized_path);
  REQUIRE(writer.open());
  auto unsafe = sample(0);
  unsafe.selected_candidate_type = "relay 10.0.0.1:3478 token secret";
  REQUIRE(writer.append(unsafe));
  shareme::core::DriftSummary summary;
  summary.complete = true;
  summary.pause_intervals = {{.start_capture_time_ms = 100,
                              .end_capture_time_ms = 200}};
  summary.errors = {"rtc-failure"};
  REQUIRE(writer.finalize(summary));

  QFile safe(sanitized_path);
  REQUIRE(safe.open(QIODevice::ReadOnly));
  const auto content = safe.readAll();
  REQUIRE(content.contains("\"candidateType\":\"unknown\""));
  REQUIRE(!content.contains("10.0.0.1"));
  REQUIRE(!content.contains("token"));
  REQUIRE(!content.contains("secret"));
  REQUIRE(!content.contains("sdp"));
  REQUIRE(content.contains("\"pauseIntervals\":[{\"endCaptureTimeMs\":200,\"startCaptureTimeMs\":100}]"));
  const auto error_summary =
      QJsonDocument::fromJson(content.split('\n').at(1)).object();
  REQUIRE(error_summary.value("errors").toArray().size() == 1);
  REQUIRE(error_summary.value("errors").toArray().at(0).toString() ==
          QStringLiteral("rtc-failure"));
}

}  // namespace

int main() {
  writes_samples_and_summary_as_atomic_sanitized_jsonl();
  rejects_existing_output_and_unsanitized_candidate_values();
  return EXIT_SUCCESS;
}
