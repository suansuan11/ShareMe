#pragma once

#include <QObject>
#include <QString>

namespace shareme::tools {

class CallSession : public QObject {
  Q_OBJECT

public:
  explicit CallSession(QObject *parent = nullptr);
  ~CallSession() override;

  [[nodiscard]] virtual QString status() const = 0;
  [[nodiscard]] virtual QString roomId() const = 0;
  [[nodiscard]] virtual bool sessionEnded() const noexcept = 0;

  Q_INVOKABLE virtual void start() = 0;
  Q_INVOKABLE virtual void stop() = 0;
  Q_INVOKABLE virtual bool setMicrophoneMuted(bool muted) = 0;
  Q_INVOKABLE virtual bool setSpeakerMuted(bool muted) = 0;

signals:
  void statusChanged();
  void roomIdChanged();
  void sessionEndedChanged();
};

} // namespace shareme::tools
