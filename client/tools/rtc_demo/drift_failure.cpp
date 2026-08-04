#include "drift_failure.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace shareme::tools {
namespace {

[[nodiscard]] bool valid_category(const QString& category) {
  static const QRegularExpression pattern{
      QStringLiteral("^[A-Za-z0-9_.-]{1,64}$")};
  return pattern.match(category).hasMatch();
}

}  // namespace

QByteArray encode_drift_failure(const QString& category) {
  if (!valid_category(category))
    return {};
  return QJsonDocument(QJsonObject{
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("type"),
                            QStringLiteral("drift-failure")},
                           {QStringLiteral("payload"),
                            QJsonObject{{QStringLiteral("category"),
                                         category}}},
                       })
      .toJson(QJsonDocument::Compact);
}

std::optional<QString> decode_drift_failure(const QByteArray& message) {
  const auto document = QJsonDocument::fromJson(message);
  if (!document.isObject())
    return std::nullopt;
  const auto object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("type")).toString() !=
          QStringLiteral("drift-failure") ||
      !object.value(QStringLiteral("payload")).isObject()) {
    return std::nullopt;
  }
  const auto category = object.value(QStringLiteral("payload"))
                            .toObject()
                            .value(QStringLiteral("category"))
                            .toString();
  if (!valid_category(category))
    return std::nullopt;
  return category;
}

}  // namespace shareme::tools
