#pragma once

#include "shareme/media/playback_session.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <qqmlintegration.h>

#include <cstdint>
#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QAudioSink;
class QIODevice;
QT_END_NAMESPACE

class PlaybackController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("PlaybackController is created by the application")
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
  Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
  explicit PlaybackController(QObject* parent = nullptr);
  ~PlaybackController() override;

  [[nodiscard]] QString state() const;
  [[nodiscard]] qint64 positionMs() const;
  [[nodiscard]] qint64 durationMs() const;
  [[nodiscard]] QString errorMessage() const;

  Q_INVOKABLE void open(const QUrl& url);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void seek(qint64 target_ms);
  Q_INVOKABLE void setVideoSink(QVideoSink* sink);

signals:
  void stateChanged();
  void positionChanged();
  void durationChanged();
  void errorChanged();

private slots:
  void poll();

private:
  void setState(const QString& state);
  void setPosition(qint64 position_ms);
  void setDuration(qint64 duration_ms);
  void setError(const QString& message);
  void clearError();
  void resetAudio();
  void startOrResumeAudio();
  void appendDecodedAudio();
  void writePendingAudio();
  void updatePositionFromClock();
  void presentDueVideo();
  void syncSessionState();

  shareme::media::PlaybackSession session_;
  QTimer poll_timer_;
  QPointer<QVideoSink> video_sink_;
  std::unique_ptr<QAudioSink> audio_sink_;
  QIODevice* audio_device_{nullptr};
  QByteArray pending_audio_;
  std::optional<shareme::media::VideoFrame> pending_video_;
  QElapsedTimer fallback_clock_;
  QString state_{"closed"};
  QString error_message_;
  qint64 position_ms_{0};
  qint64 duration_ms_{0};
  qint64 clock_base_ms_{0};
};
