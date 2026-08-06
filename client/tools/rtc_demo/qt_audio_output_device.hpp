#pragma once

#include "shareme/core/audio_output_contract.hpp"

#include <QAudioDevice>

#include <cstdint>
#include <memory>

class QAudioSink;
class QIODevice;

class QtAudioOutputDevice final : public shareme::core::AudioOutputDevice {
 public:
  explicit QtAudioOutputDevice(QAudioDevice device);
  QtAudioOutputDevice();
  ~QtAudioOutputDevice() override;

  QtAudioOutputDevice(const QtAudioOutputDevice&) = delete;
  QtAudioOutputDevice& operator=(const QtAudioOutputDevice&) = delete;

  [[nodiscard]] shareme::core::WriteResult try_write(
      shareme::core::AudioPcmBlockView pcm) override;
  [[nodiscard]] shareme::core::AudioDeviceSnapshot snapshot() override;
  [[nodiscard]] shareme::core::FinalDeviceSnapshot quiesce_and_snapshot()
      override;
  [[nodiscard]] shareme::core::OpenResult open(
      shareme::core::AudioOutputFormat format) override;
  [[nodiscard]] bool start() override;
  void pause() override;
  void stop() override;

 private:
  [[nodiscard]] shareme::core::AudioDeviceSnapshot read_snapshot();
  [[nodiscard]] std::uint64_t processed_frames() noexcept;
  [[nodiscard]] bool has_trusted_device_facts() const noexcept;
  void refresh_sink_state() noexcept;
  void reset_runtime_state() noexcept;

  QAudioDevice device_;
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* io_device_ = nullptr;
  shareme::core::AudioOutputFormat format_{};
  std::uint64_t device_instance_id_ = 0;
  std::uint64_t snapshot_sequence_ = 0;
  std::uint64_t accepted_frames_total_ = 0;
  std::uint64_t device_consumed_frames_total_ = 0;
  std::uint64_t last_processed_usecs_ = 0;
  std::uint64_t underrun_count_ = 0;
  std::uint64_t discontinuity_count_ = 0;
  int last_audio_error_ = -1;
  bool opened_ = false;
  bool active_ = false;
  bool controlled_suspension_ = false;
  bool processed_duration_valid_ = false;
  bool queue_facts_valid_ = false;
};
