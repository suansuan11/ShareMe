#include "call_session.hpp"
#include "app_preferences.hpp"
#include "shareme_app_controller.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

struct SessionCounters {
  int live{};
  int started{};
  int stopped{};
};

class FakeCallSession final : public shareme::tools::CallSession {
public:
  explicit FakeCallSession(SessionCounters &counters, QObject *parent = nullptr)
      : CallSession(parent), counters_(counters) {
    ++counters_.live;
  }
  ~FakeCallSession() override { --counters_.live; }
  QString status() const override { return QStringLiteral("connected"); }
  QString roomId() const override { return QStringLiteral("ABC234"); }
  bool sessionEnded() const noexcept override { return ended_; }
  void start() override { ++counters_.started; }
  void stop() override {
    if (ended_)
      return;
    ended_ = true;
    ++counters_.stopped;
    emit sessionEndedChanged();
  }
  bool setMicrophoneMuted(bool) override { return !ended_; }
  bool setSpeakerMuted(bool) override { return !ended_; }

private:
  SessionCounters &counters_;
  bool ended_{false};
};
} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  SessionCounters counters;
  shareme::tools::ShareMeAppController controller(
      [&counters](const shareme::tools::AppSessionConfig &,
                  QObject *parent) -> std::unique_ptr<shareme::tools::CallSession> {
        return std::make_unique<FakeCallSession>(counters, parent);
      });

  REQUIRE(controller.pageState() == shareme::tools::AppPage::home);
  controller.showJoinRoom();
  REQUIRE(controller.pageState() == shareme::tools::AppPage::preflight);
  REQUIRE(controller.preflightMode() ==
          shareme::tools::PreflightMode::join_room);
  controller.setRoomCode(QStringLiteral("abc234"));
  REQUIRE(controller.startCall());
  REQUIRE(controller.pageState() == shareme::tools::AppPage::calling);
  REQUIRE(controller.activeController() != nullptr);
  REQUIRE(counters.live == 1);
  REQUIRE(counters.started == 1);

  controller.leaveCall();
  REQUIRE(controller.pageState() == shareme::tools::AppPage::home);
  REQUIRE(controller.activeController() == nullptr);
  REQUIRE(counters.stopped == 1);
  REQUIRE(counters.live == 0);
  controller.leaveCall();
  REQUIRE(counters.stopped == 1);

  controller.showJoinRoom();
  controller.setRoomCode(QStringLiteral("bad"));
  REQUIRE(!controller.startCall());
  REQUIRE(controller.errorCategory() == QStringLiteral("invalid-room"));
  REQUIRE(counters.live == 0);

  shareme::tools::AppSessionConfig configured;
  configured.server_url = QUrl(QStringLiteral("ws://127.0.0.1:8080/v1/ws"));
  configured.role = shareme::tools::InteractiveRole::host;
  configured.video_source = shareme::tools::SessionVideoSource::screen;
  REQUIRE(controller.startConfiguredCall(configured));
  REQUIRE(controller.pageState() == shareme::tools::AppPage::calling);
  REQUIRE(counters.started == 2);
  controller.leaveCall();
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  QSettings settings(directory.filePath(QStringLiteral("shareme.ini")),
                     QSettings::IniFormat);
  shareme::tools::AppPreferences preferences(settings);
  shareme::tools::ShareMeAppController recent_controller(
      [&counters](const shareme::tools::AppSessionConfig &,
                  QObject *parent) -> std::unique_ptr<shareme::tools::CallSession> {
        return std::make_unique<FakeCallSession>(counters, parent);
      },
      &preferences);
  recent_controller.showJoinRoom();
  recent_controller.setRoomCode(QStringLiteral("abc234"));
  REQUIRE(recent_controller.startCall());
  REQUIRE(recent_controller.recentRoom() == QStringLiteral("ABC234"));
  recent_controller.leaveCall();
  recent_controller.forgetRecentRoom();
  REQUIRE(recent_controller.recentRoom().isEmpty());
  return EXIT_SUCCESS;
}
