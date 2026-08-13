#pragma once

#include <functional>
#include <optional>
#include <string>

#include "api/audio/audio_device.h"
#include "api/audio_options.h"
#include "api/environment/environment.h"
#include "api/scoped_refptr.h"

namespace shareme::rtc {

enum class AudioDeviceMode {
  synthetic,
  microphone,
  playout,
};

enum class AudioSourceKind {
  synthetic,
  microphone,
  movie,
};

enum class AudioProcessingPolicy {
  unprocessed,
  aec_ns_agc,
};

enum class RemotePlayoutPolicy {
  discard,
  native,
};

enum class AudioDeviceError {
  none,
  dependency_unavailable,
  permission_denied,
  initialization_failed,
};

enum class NativeAudioPlatform {
  windows,
  other,
};

enum class RecordingDeviceSelection {
  default_communication,
  index_zero,
};

enum class MicrophonePermissionStatus {
  unknown,
  granted,
  denied,
};

struct NativeAudioDeviceResult {
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> device;
  AudioDeviceError error{AudioDeviceError::none};
  std::string message;

  [[nodiscard]] static NativeAudioDeviceResult
  success(webrtc::scoped_refptr<webrtc::AudioDeviceModule> device);
  [[nodiscard]] static NativeAudioDeviceResult failure(AudioDeviceError error,
                                                       std::string message);
};

struct NativeAudioInitializationResult {
  AudioDeviceError error{AudioDeviceError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept {
    return error == AudioDeviceError::none;
  }

  [[nodiscard]] static NativeAudioInitializationResult success();
  [[nodiscard]] static NativeAudioInitializationResult
  failure(AudioDeviceError error, std::string message);
};

using NativeAudioDeviceFactory =
    std::function<NativeAudioDeviceResult(const webrtc::Environment &)>;
using NativeAudioDeviceInitializer =
    std::function<NativeAudioInitializationResult(webrtc::AudioDeviceModule &)>;
using MicrophonePermissionPreflight =
    std::function<MicrophonePermissionStatus()>;

struct AudioDeviceResult {
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> device;
  AudioDeviceMode mode{AudioDeviceMode::synthetic};
  AudioProcessingPolicy processing{AudioProcessingPolicy::unprocessed};
  RemotePlayoutPolicy remote_playout{RemotePlayoutPolicy::discard};
  AudioDeviceError error{AudioDeviceError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept {
    return device != nullptr && error == AudioDeviceError::none;
  }
};

[[nodiscard]] AudioProcessingPolicy
audio_processing_policy(AudioSourceKind source) noexcept;

[[nodiscard]] webrtc::AudioOptions audio_options(AudioSourceKind source);

[[nodiscard]] RecordingDeviceSelection
recording_device_selection(NativeAudioPlatform platform) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
speaker_volume_native_value(int percent, std::uint32_t minimum,
                            std::uint32_t maximum) noexcept;

[[nodiscard]] std::optional<int>
speaker_volume_percent(std::uint32_t value, std::uint32_t minimum,
                       std::uint32_t maximum) noexcept;

[[nodiscard]] AudioDeviceResult
create_audio_device(const webrtc::Environment &environment,
                    AudioDeviceMode mode,
                    NativeAudioDeviceFactory native_factory = {},
                    NativeAudioDeviceInitializer native_initializer = {},
                    MicrophonePermissionPreflight permission_preflight = {});

} // namespace shareme::rtc
