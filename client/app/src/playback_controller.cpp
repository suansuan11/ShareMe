#include "playback_controller.hpp"

#include "shareme/media/ffmpeg_media_source.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QImage>
#include <QIODevice>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>

namespace {

[[nodiscard]] QString state_name(shareme::media::PlaybackState state) {
  using shareme::media::PlaybackState;
  switch (state) {
    case PlaybackState::closed:
      return QStringLiteral("closed");
    case PlaybackState::paused:
      return QStringLiteral("paused");
    case PlaybackState::playing:
      return QStringLiteral("playing");
    case PlaybackState::ended:
      return QStringLiteral("ended");
    case PlaybackState::failed:
      return QStringLiteral("failed");
  }
  return QStringLiteral("failed");
}

[[nodiscard]] std::filesystem::path local_path(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toStdString()};
#endif
}

}  // namespace

PlaybackController::PlaybackController(QObject* parent)
    : QObject{parent},
      session_{std::make_unique<shareme::media::FfmpegMediaSource>()} {
  poll_timer_.setInterval(10);
  poll_timer_.setTimerType(Qt::PreciseTimer);
  connect(&poll_timer_, &QTimer::timeout, this, &PlaybackController::poll);
  poll_timer_.start();
}

PlaybackController::~PlaybackController() {
  session_.close();
  if (audio_sink_ != nullptr) {
    audio_sink_->stop();
  }
}

QString PlaybackController::state() const {
  return state_;
}

qint64 PlaybackController::positionMs() const {
  return position_ms_;
}

qint64 PlaybackController::durationMs() const {
  return duration_ms_;
}

QString PlaybackController::errorMessage() const {
  return error_message_;
}

void PlaybackController::open(const QUrl& url) {
  clearError();
  if (!url.isLocalFile()) {
    setError(QStringLiteral("Choose a local movie file."));
    return;
  }

  try {
    resetAudio();
    pending_video_.reset();
    const auto info = session_.open(local_path(url.toLocalFile()));
    setDuration(info.duration_ms);
    setPosition(0);
    clock_base_ms_ = 0;
    setState(QStringLiteral("paused"));
  } catch (const std::exception& error) {
    setState(QStringLiteral("failed"));
    setError(QString::fromUtf8(error.what()));
  }
}

void PlaybackController::play() {
  clearError();
  if (state_ != QStringLiteral("paused")) {
    return;
  }

  startOrResumeAudio();
  fallback_clock_.restart();
  clock_base_ms_ = position_ms_;
  session_.play();
  setState(QStringLiteral("playing"));
}

void PlaybackController::pause() {
  if (state_ != QStringLiteral("playing")) {
    return;
  }

  updatePositionFromClock();
  session_.pause();
  if (audio_sink_ != nullptr) {
    audio_sink_->suspend();
  }
  fallback_clock_.invalidate();
  setState(QStringLiteral("paused"));
}

void PlaybackController::seek(qint64 target_ms) {
  if (state_ == QStringLiteral("closed") ||
      state_ == QStringLiteral("failed")) {
    return;
  }

  const auto bounded_target = std::clamp(target_ms, qint64{0}, duration_ms_);
  const auto resume = state_ == QStringLiteral("playing");
  try {
    session_.seek(bounded_target);
    pending_video_.reset();
    pending_audio_.clear();
    setPosition(bounded_target);
    clock_base_ms_ = bounded_target;
    resetAudio();
    if (resume) {
      startOrResumeAudio();
      fallback_clock_.restart();
      setState(QStringLiteral("playing"));
    } else {
      setState(QStringLiteral("paused"));
    }
  } catch (const std::exception& error) {
    setState(QStringLiteral("failed"));
    setError(QString::fromUtf8(error.what()));
  }
}

void PlaybackController::setVideoSink(QVideoSink* sink) {
  video_sink_ = sink;
}

void PlaybackController::poll() {
  if (state_ != QStringLiteral("playing")) {
    syncSessionState();
    return;
  }

  appendDecodedAudio();
  writePendingAudio();
  updatePositionFromClock();
  session_.set_playhead_ms(position_ms_);
  presentDueVideo();
  syncSessionState();
}

void PlaybackController::setState(const QString& state) {
  if (state_ == state) {
    return;
  }
  state_ = state;
  emit stateChanged();
}

void PlaybackController::setPosition(qint64 position_ms) {
  const auto bounded = std::clamp(position_ms, qint64{0}, duration_ms_);
  if (position_ms_ == bounded) {
    return;
  }
  position_ms_ = bounded;
  emit positionChanged();
}

void PlaybackController::setDuration(qint64 duration_ms) {
  const auto bounded = std::max(duration_ms, qint64{0});
  if (duration_ms_ == bounded) {
    return;
  }
  duration_ms_ = bounded;
  emit durationChanged();
}

void PlaybackController::setError(const QString& message) {
  if (error_message_ == message) {
    return;
  }
  error_message_ = message;
  emit errorChanged();
}

void PlaybackController::clearError() {
  setError({});
}

void PlaybackController::resetAudio() {
  pending_audio_.clear();
  audio_device_ = nullptr;
  if (audio_sink_ != nullptr) {
    audio_sink_->stop();
    audio_sink_.reset();
  }
}

void PlaybackController::startOrResumeAudio() {
  if (audio_sink_ != nullptr) {
    audio_sink_->resume();
    return;
  }

  QAudioFormat format;
  format.setSampleRate(48'000);
  format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Int16);

  const auto device = QMediaDevices::defaultAudioOutput();
  if (device.isNull() || !device.isFormatSupported(format)) {
    setError(QStringLiteral(
        "The default audio device does not support 48 kHz stereo PCM."));
    return;
  }

  audio_sink_ = std::make_unique<QAudioSink>(device, format);
  audio_sink_->setBufferSize(48'000 * 2 * 2 / 5);
  audio_device_ = audio_sink_->start();
  if (audio_device_ == nullptr) {
    setError(QStringLiteral("The default audio device could not be opened."));
    audio_sink_.reset();
  }
}

void PlaybackController::appendDecodedAudio() {
  constexpr qsizetype maximum_pending_bytes = 96'000;
  while (pending_audio_.size() < maximum_pending_bytes) {
    auto frame = session_.pop_audio();
    if (!frame.has_value()) {
      break;
    }

    const auto byte_count =
        frame->interleaved_samples.size() * sizeof(std::int16_t);
    pending_audio_.append(
        reinterpret_cast<const char*>(frame->interleaved_samples.data()),
        static_cast<qsizetype>(byte_count));
  }
}

void PlaybackController::writePendingAudio() {
  if (audio_sink_ == nullptr || audio_device_ == nullptr ||
      pending_audio_.isEmpty()) {
    return;
  }

  const auto writable =
      std::min<qint64>(audio_sink_->bytesFree(), pending_audio_.size());
  if (writable <= 0) {
    return;
  }

  const auto written = audio_device_->write(pending_audio_.constData(), writable);
  if (written > 0) {
    pending_audio_.remove(0, static_cast<qsizetype>(written));
  }
}

void PlaybackController::updatePositionFromClock() {
  qint64 elapsed_ms = 0;
  if (audio_sink_ != nullptr && audio_sink_->processedUSecs() > 0) {
    elapsed_ms = audio_sink_->processedUSecs() / 1'000;
  } else if (fallback_clock_.isValid()) {
    elapsed_ms = fallback_clock_.elapsed();
  }
  setPosition(clock_base_ms_ + elapsed_ms);
}

void PlaybackController::presentDueVideo() {
  if (video_sink_.isNull()) {
    return;
  }
  if (!pending_video_.has_value()) {
    pending_video_ = session_.pop_video();
  }
  if (!pending_video_.has_value() ||
      pending_video_->pts_ms > position_ms_ + 15) {
    return;
  }

  const auto& frame = *pending_video_;
  QVideoFrame video_frame;
  if (frame.pixel_format == shareme::media::VideoPixelFormat::i420) {
    video_frame = QVideoFrame{QVideoFrameFormat{
        QSize(frame.width, frame.height), QVideoFrameFormat::Format_YUV420P}};
    if (!video_frame.map(QVideoFrame::WriteOnly)) {
      setError(QStringLiteral("Could not map decoded video frame."));
      pending_video_.reset();
      return;
    }
    const auto chroma_width = (frame.width + 1) / 2;
    const auto chroma_height = (frame.height + 1) / 2;
    const std::byte* planes[] = {
        frame.i420_y.data(), frame.i420_u.data(), frame.i420_v.data()};
    const int source_strides[] = {frame.stride_y, frame.stride_u,
                                  frame.stride_v};
    const int plane_heights[] = {frame.height, chroma_height, chroma_height};
    const int plane_widths[] = {frame.width, chroma_width, chroma_width};
    for (int plane = 0; plane < 3; ++plane) {
      for (int row = 0; row < plane_heights[plane]; ++row) {
        std::memcpy(video_frame.bits(plane) +
                        row * video_frame.bytesPerLine(plane),
                    planes[plane] + row * source_strides[plane],
                    static_cast<std::size_t>(plane_widths[plane]));
      }
    }
    video_frame.unmap();
  } else {
    const QImage borrowed_image{
        reinterpret_cast<const uchar*>(frame.rgba.data()), frame.width,
        frame.height, frame.stride, QImage::Format_RGBA8888};
    video_frame = QVideoFrame{borrowed_image.copy()};
  }
  video_frame.setStartTime(frame.pts_ms * 1'000);
  video_frame.setEndTime((frame.pts_ms + 33) * 1'000);
  video_sink_->setVideoFrame(video_frame);
  pending_video_.reset();
}

void PlaybackController::syncSessionState() {
  const auto current = session_.state();
  const auto current_name = state_name(current);
  if (current == shareme::media::PlaybackState::ended) {
    setPosition(duration_ms_);
    fallback_clock_.invalidate();
  }
  if (current == shareme::media::PlaybackState::failed &&
      error_message_.isEmpty()) {
    setError(QStringLiteral("Playback stopped because decoding failed."));
  }
  setState(current_name);
}
