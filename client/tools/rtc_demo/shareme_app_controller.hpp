#pragma once

#include "app_session_config.hpp"
#include "call_session.hpp"
#include "app_preferences.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace shareme::tools {

using CallSessionFactory =
    std::function<std::unique_ptr<CallSession>(const AppSessionConfig &,
                                               QObject *)>;

class ShareMeAppController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString page READ page NOTIFY pageChanged)
  Q_PROPERTY(QString preflight READ preflight NOTIFY pageChanged)
  Q_PROPERTY(QObject *activeController READ activeController NOTIFY activeControllerChanged)
  Q_PROPERTY(QString roomCode READ roomCode WRITE setRoomCode NOTIFY roomCodeChanged)
  Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
  Q_PROPERTY(QString screenProfile READ screenProfile WRITE setScreenProfile NOTIFY screenProfileChanged)
  Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE setMicrophoneEnabled NOTIFY microphoneEnabledChanged)
  Q_PROPERTY(bool speakerEnabled READ speakerEnabled WRITE setSpeakerEnabled NOTIFY speakerEnabledChanged)
  Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY errorChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
  Q_PROPERTY(QString recentRoom READ recentRoom NOTIFY recentRoomChanged)
  Q_PROPERTY(QString formattedRecentRoom READ formattedRecentRoom NOTIFY recentRoomChanged)

public:
  explicit ShareMeAppController(CallSessionFactory factory,
                                AppPreferences *preferences = nullptr,
                                QObject *parent = nullptr);
  ~ShareMeAppController() override;

  [[nodiscard]] AppPage pageState() const noexcept;
  [[nodiscard]] QString page() const;
  [[nodiscard]] PreflightMode preflightMode() const noexcept;
  [[nodiscard]] QString preflight() const;
  [[nodiscard]] QObject *activeController() const noexcept;
  [[nodiscard]] QString roomCode() const;
  [[nodiscard]] QString serverUrl() const;
  [[nodiscard]] QString screenProfile() const;
  [[nodiscard]] bool microphoneEnabled() const noexcept;
  [[nodiscard]] bool speakerEnabled() const noexcept;
  [[nodiscard]] QString errorCategory() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] QString recentRoom() const;
  [[nodiscard]] QString formattedRecentRoom() const;

  void setRoomCode(QString room);
  void setServerUrl(QString server);
  void setScreenProfile(QString profile);
  void setMicrophoneEnabled(bool enabled);
  void setSpeakerEnabled(bool enabled);

  Q_INVOKABLE void showCreateRoom();
  Q_INVOKABLE void showJoinRoom();
  Q_INVOKABLE void returnHome();
  Q_INVOKABLE bool startCall();
  Q_INVOKABLE void leaveCall();
  Q_INVOKABLE bool retryCall();
  Q_INVOKABLE void forgetRecentRoom();
  Q_INVOKABLE void joinRecentRoom();
  bool startConfiguredCall(AppSessionConfig config);

signals:
  void pageChanged();
  void activeControllerChanged();
  void roomCodeChanged();
  void serverUrlChanged();
  void screenProfileChanged();
  void microphoneEnabledChanged();
  void speakerEnabledChanged();
  void errorChanged();
  void recentRoomChanged();

private:
  void setPage(AppPage page);
  void setError(QString category, QString message);
  [[nodiscard]] AppSessionConfig buildConfig() const;
  bool startSession(AppSessionConfig config, bool validate_interactive);

  CallSessionFactory factory_;
  AppPreferences *preferences_{};
  std::unique_ptr<CallSession> active_session_;
  AppPage page_{AppPage::home};
  PreflightMode preflight_mode_{PreflightMode::create_room};
  QString room_code_;
  QString server_url_{QStringLiteral("ws://127.0.0.1:8080/v1/ws")};
  QString screen_profile_{QStringLiteral("standard")};
  bool microphone_enabled_{true};
  bool speaker_enabled_{true};
  QString error_category_;
  QString error_message_;
};

} // namespace shareme::tools
