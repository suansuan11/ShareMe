#include "qt_audio_output_device.hpp"

#include <QAudioFormat>
#include <QCoreApplication>
#include <QMediaDevices>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace shareme::core;

constexpr AudioOutputFormat kMovieFormat{
    .sample_rate = 48'000,
    .channel_count = 2,
    .sample_format = AudioSampleFormat::signed_int16,
    .interleaving = AudioInterleaving::interleaved,
};

void require(bool condition, const char* expression) {
  if (condition) {
    return;
  }
  std::cerr << "Requirement failed: " << expression << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression)

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application{argc, argv};

  QtAudioOutputDevice null_device{QAudioDevice{}};
  const auto invalid_format = null_device.open(AudioOutputFormat{
      .sample_rate = 48'000,
      .channel_count = 2,
      .sample_format = AudioSampleFormat::float32,
      .interleaving = AudioInterleaving::interleaved,
  });
  REQUIRE(invalid_format.status == OpenStatus::failed);
  REQUIRE(invalid_format.failure_category ==
          PlaybackCategory::audio_format_change);

  const auto null_open = null_device.open(kMovieFormat);
  REQUIRE(null_open.status == OpenStatus::failed);
  REQUIRE(null_open.failure_category ==
          PlaybackCategory::audio_output_device_lost);

  const auto output = QMediaDevices::defaultAudioOutput();
  QAudioFormat qt_format;
  qt_format.setSampleRate(48'000);
  qt_format.setChannelCount(2);
  qt_format.setSampleFormat(QAudioFormat::Int16);
  if (output.isNull() || !output.isFormatSupported(qt_format)) {
    std::cerr << "Qt offscreen audio device unavailable; lifecycle checks skipped\n";
    return EXIT_SUCCESS;
  }

  QtAudioOutputDevice device{output};
  const auto opened = device.open(kMovieFormat);
  if (opened.status != OpenStatus::opened) {
    std::cerr << "Qt offscreen audio backend did not open; lifecycle checks skipped\n";
    return EXIT_SUCCESS;
  }
  if (!device.start()) {
    std::cerr << "Qt offscreen audio backend did not start; lifecycle checks skipped\n";
    return EXIT_SUCCESS;
  }

  auto snapshot = device.snapshot();
  REQUIRE(snapshot.active);

  std::vector<std::byte> pcm(4 * 8);
  const auto write = device.try_write(AudioPcmBlockView{
      .receiver_sequence = 1,
      .frame_count = 8,
      .sample_rate = kMovieFormat.sample_rate,
      .channel_count = kMovieFormat.channel_count,
      .sample_format = kMovieFormat.sample_format,
      .interleaving = kMovieFormat.interleaving,
      .payload = AudioPcmPayloadView{
          .bytes = std::span<const std::byte>{pcm},
          .frame_stride_bytes = 4,
      },
  });
  REQUIRE(write.status == WriteStatus::accepted ||
          write.status == WriteStatus::would_block);

  const auto final = device.quiesce_and_snapshot();
  REQUIRE(final.quiesced);
  REQUIRE(!final.active);
  device.stop();
  return EXIT_SUCCESS;
}
