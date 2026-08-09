#include "shareme_app_controller.hpp"

namespace shareme::tools {

ShareMeAppController::ShareMeAppController(CallSessionFactory factory,
                                           AppPreferences *preferences,
                                           QObject *parent)
    : QObject(parent), factory_(std::move(factory)),
      preferences_(preferences) {}

ShareMeAppController::~ShareMeAppController() { leaveCall(); }

AppPage ShareMeAppController::pageState() const noexcept { return page_; }

QString ShareMeAppController::page() const {
  switch (page_) {
  case AppPage::home:
    return QStringLiteral("home");
  case AppPage::preflight:
    return QStringLiteral("preflight");
  case AppPage::calling:
    return QStringLiteral("calling");
  case AppPage::result:
    return QStringLiteral("result");
  }
  return QStringLiteral("result");
}

PreflightMode ShareMeAppController::preflightMode() const noexcept {
  return preflight_mode_;
}

QString ShareMeAppController::preflight() const {
  return preflight_mode_ == PreflightMode::create_room
             ? QStringLiteral("create")
             : QStringLiteral("join");
}

QObject *ShareMeAppController::activeController() const noexcept {
  return active_session_.get();
}

QString ShareMeAppController::roomCode() const { return room_code_; }
QString ShareMeAppController::serverUrl() const { return server_url_; }
QString ShareMeAppController::screenProfile() const { return screen_profile_; }
bool ShareMeAppController::microphoneEnabled() const noexcept {
  return microphone_enabled_;
}
bool ShareMeAppController::speakerEnabled() const noexcept {
  return speaker_enabled_;
}
QString ShareMeAppController::errorCategory() const { return error_category_; }
QString ShareMeAppController::errorMessage() const { return error_message_; }
QString ShareMeAppController::recentRoom() const {
  return preferences_ ? preferences_->recent_room() : QString{};
}
QString ShareMeAppController::formattedRecentRoom() const {
  return format_room_code(recentRoom());
}

void ShareMeAppController::setRoomCode(QString room) {
  if (room_code_ == room)
    return;
  room_code_ = std::move(room);
  emit roomCodeChanged();
}

void ShareMeAppController::setServerUrl(QString server) {
  if (server_url_ == server)
    return;
  server_url_ = std::move(server);
  emit serverUrlChanged();
}

void ShareMeAppController::setScreenProfile(QString profile) {
  if (!shareme::core::parse_screen_stream_profile(profile.toStdString()) ||
      screen_profile_ == profile)
    return;
  screen_profile_ = std::move(profile);
  emit screenProfileChanged();
}

void ShareMeAppController::setMicrophoneEnabled(bool enabled) {
  if (microphone_enabled_ == enabled)
    return;
  microphone_enabled_ = enabled;
  emit microphoneEnabledChanged();
}

void ShareMeAppController::setSpeakerEnabled(bool enabled) {
  if (speaker_enabled_ == enabled)
    return;
  speaker_enabled_ = enabled;
  emit speakerEnabledChanged();
}

void ShareMeAppController::showCreateRoom() {
  preflight_mode_ = PreflightMode::create_room;
  setPage(AppPage::preflight);
}

void ShareMeAppController::showJoinRoom() {
  preflight_mode_ = PreflightMode::join_room;
  setPage(AppPage::preflight);
}

void ShareMeAppController::returnHome() { leaveCall(); }

bool ShareMeAppController::startCall() {
  return startSession(buildConfig(), true);
}

bool ShareMeAppController::startConfiguredCall(AppSessionConfig config) {
  return startSession(std::move(config), false);
}

bool ShareMeAppController::startSession(AppSessionConfig config,
                                        bool validate_interactive) {
  if (active_session_)
    return false;
  if (validate_interactive) {
    const auto validation = validate_interactive_config(config);
    if (!validation.accepted) {
      setError(validation.category, validation.message);
      return false;
    }
  }
  auto session = factory_ ? factory_(config, this) : nullptr;
  if (!session) {
    setError(QStringLiteral("session-unavailable"),
             QStringLiteral("无法启动通话，请检查设备和权限"));
    setPage(AppPage::result);
    return false;
  }
  connect(session.get(), &CallSession::sessionEndedChanged, this, [this] {
    if (active_session_ && active_session_->sessionEnded())
      leaveCall();
  });
  active_session_ = std::move(session);
  emit activeControllerChanged();
  setError({}, {});
  setPage(AppPage::calling);
  active_session_->start();
  if (preferences_ && config.role == InteractiveRole::viewer &&
      !config.requested_room.isEmpty()) {
    preferences_->remember_room(config.requested_room);
    emit recentRoomChanged();
  }
  if (!microphone_enabled_)
    static_cast<void>(active_session_->setMicrophoneMuted(true));
  if (!speaker_enabled_)
    static_cast<void>(active_session_->setSpeakerMuted(true));
  return true;
}

void ShareMeAppController::leaveCall() {
  if (active_session_) {
    disconnect(active_session_.get(), nullptr, this, nullptr);
    active_session_->stop();
    active_session_.reset();
    emit activeControllerChanged();
  }
  setError({}, {});
  setPage(AppPage::home);
}

bool ShareMeAppController::retryCall() {
  leaveCall();
  setPage(AppPage::preflight);
  return startCall();
}

void ShareMeAppController::forgetRecentRoom() {
  if (!preferences_ || preferences_->recent_room().isEmpty())
    return;
  preferences_->forget_recent_room();
  emit recentRoomChanged();
}

void ShareMeAppController::joinRecentRoom() {
  if (!preferences_)
    return;
  const auto recent = preferences_->recent_room();
  if (recent.isEmpty())
    return;
  showJoinRoom();
  setRoomCode(recent);
}

void ShareMeAppController::setPage(AppPage page) {
  if (page_ == page)
    return;
  page_ = page;
  emit pageChanged();
}

void ShareMeAppController::setError(QString category, QString message) {
  if (error_category_ == category && error_message_ == message)
    return;
  error_category_ = std::move(category);
  error_message_ = std::move(message);
  emit errorChanged();
}

AppSessionConfig ShareMeAppController::buildConfig() const {
  AppSessionConfig config;
  config.server_url = QUrl(server_url_);
  config.role = preflight_mode_ == PreflightMode::create_room
                    ? InteractiveRole::host
                    : InteractiveRole::viewer;
  config.requested_room = normalize_room_code(room_code_);
  config.video_source = SessionVideoSource::screen;
  config.screen_profile =
      *shareme::core::parse_screen_stream_profile(screen_profile_.toStdString());
  config.native_audio_playout = speaker_enabled_;
  return config;
}

} // namespace shareme::tools
