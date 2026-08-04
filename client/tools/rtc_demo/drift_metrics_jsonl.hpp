#pragma once

#include "shareme/core/drift_metrics.hpp"

#include <QFile>
#include <QString>

#include <cstdint>

namespace shareme::tools {

class DriftMetricsJsonlWriter final {
public:
  explicit DriftMetricsJsonlWriter(QString final_path);

  [[nodiscard]] bool open();
  [[nodiscard]] bool append(const shareme::core::DriftSample& sample);
  [[nodiscard]] bool finalize(const shareme::core::DriftSummary& summary);
  [[nodiscard]] QString failure_category() const;

private:
  [[nodiscard]] bool fail(QString category);

  QString final_path_;
  QString temporary_path_;
  QString failure_category_;
  QFile file_;
  bool opened_ = false;
  bool finalized_ = false;
  bool have_last_sample_ = false;
  std::uint64_t last_sample_index_ = 0;
};

}  // namespace shareme::tools
