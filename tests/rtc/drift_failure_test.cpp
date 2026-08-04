#include "drift_failure.hpp"

#include <QByteArray>

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

void round_trips_only_stable_failure_categories() {
  const auto encoded = shareme::tools::encode_drift_failure(
      QStringLiteral("movie-audio-failure"));
  REQUIRE(!encoded.isEmpty());
  REQUIRE(shareme::tools::decode_drift_failure(encoded) ==
          QStringLiteral("movie-audio-failure"));
  REQUIRE(shareme::tools::encode_drift_failure(QStringLiteral("room SECRET"))
              .isEmpty());
  REQUIRE(shareme::tools::encode_drift_failure(QString()).isEmpty());
}

void rejects_wrong_envelopes_and_sensitive_values() {
  const QByteArray wrong_type{
      R"({"version":1,"type":"playout-report","payload":{"category":"rtc-failure"}})"};
  REQUIRE(!shareme::tools::decode_drift_failure(wrong_type));
  const QByteArray wrong_category{
      R"({"version":1,"type":"drift-failure","payload":{"category":"10.0.0.1 secret"}})"};
  REQUIRE(!shareme::tools::decode_drift_failure(wrong_category));
  REQUIRE(!shareme::tools::decode_drift_failure(QByteArray("not-json")));
}

}  // namespace

int main() {
  round_trips_only_stable_failure_categories();
  rejects_wrong_envelopes_and_sensitive_values();
  return EXIT_SUCCESS;
}
