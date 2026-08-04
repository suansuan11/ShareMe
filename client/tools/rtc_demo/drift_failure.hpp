#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace shareme::tools {

[[nodiscard]] QByteArray encode_drift_failure(const QString& category);
[[nodiscard]] std::optional<QString> decode_drift_failure(
    const QByteArray& message);

}  // namespace shareme::tools
