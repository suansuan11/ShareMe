#include "audio_device_factory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <utility>

#include "api/audio/create_audio_device_module.h"
#include "modules/audio_device/include/test_audio_device.h"
#include "rtc_base/buffer.h"

namespace shareme::rtc {
namespace {

constexpr int kSampleRate = 48'000;
constexpr int kChannels = 1;
constexpr double kFrequency = 440.0;
constexpr int kAmplitude = 8'000;

#if defined(WEBRTC_WIN)
constexpr NativeAudioPlatform kCurrentAudioPlatform =
    NativeAudioPlatform::windows;
#else
constexpr NativeAudioPlatform kCurrentAudioPlatform =
    NativeAudioPlatform::other;
#endif

class ToneCapturer final : public webrtc::TestAudioDeviceModule::Capturer {
public:
  ToneCapturer(int sample_rate, int channels, double frequency, int amplitude)
      : sample_rate_(sample_rate), channels_(channels), frequency_(frequency),
        amplitude_(amplitude) {}

  [[nodiscard]] int SamplingFrequency() const override { return sample_rate_; }

  [[nodiscard]] int NumChannels() const override { return channels_; }

  bool Capture(webrtc::BufferT<std::int16_t> *buffer) override {
    const auto samples_per_channel =
        webrtc::TestAudioDeviceModule::SamplesPerFrame(sample_rate_);
    const auto sample_count =
        samples_per_channel * static_cast<std::size_t>(channels_);
    const double phase_increment =
        2.0 * std::numbers::pi * frequency_ / static_cast<double>(sample_rate_);
    buffer->SetData(sample_count, [&](std::span<std::int16_t> samples) {
      for (std::size_t frame = 0; frame < samples_per_channel; ++frame) {
        const auto rounded =
            std::lround(std::sin(phase_) * static_cast<double>(amplitude_));
        const auto clamped = std::clamp(
            rounded,
            static_cast<long>(std::numeric_limits<std::int16_t>::min()),
            static_cast<long>(std::numeric_limits<std::int16_t>::max()));
        for (int channel = 0; channel < channels_; ++channel) {
          samples[frame * static_cast<std::size_t>(channels_) +
                  static_cast<std::size_t>(channel)] =
              static_cast<std::int16_t>(clamped);
        }
        phase_ += phase_increment;
        if (phase_ >= 2.0 * std::numbers::pi) {
          phase_ -= 2.0 * std::numbers::pi;
        }
      }
      return samples.size();
    });
    return true;
  }

private:
  int sample_rate_;
  int channels_;
  double frequency_;
  int amplitude_;
  double phase_{0.0};
};

AudioSourceKind source_kind(AudioDeviceMode mode) noexcept {
  switch (mode) {
  case AudioDeviceMode::synthetic:
    return AudioSourceKind::synthetic;
  case AudioDeviceMode::microphone:
    return AudioSourceKind::microphone;
  case AudioDeviceMode::playout:
    return AudioSourceKind::movie;
  }
  return AudioSourceKind::synthetic;
}

RemotePlayoutPolicy remote_playout_policy(AudioDeviceMode mode) noexcept {
  return mode == AudioDeviceMode::playout ? RemotePlayoutPolicy::native
                                          : RemotePlayoutPolicy::discard;
}

AudioDeviceResult failure(AudioDeviceMode mode, AudioDeviceError error,
                          std::string message) {
  return {
      .device = nullptr,
      .mode = mode,
      .processing = audio_processing_policy(source_kind(mode)),
      .remote_playout = remote_playout_policy(mode),
      .error = error,
      .message = std::move(message),
  };
}

AudioDeviceResult initialize_recording_device(
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> device,
    AudioDeviceMode mode,
    NativeAudioDeviceInitializer native_initializer = {}) {
  if (device == nullptr) {
    return failure(mode, AudioDeviceError::dependency_unavailable,
                   "audio device dependency unavailable");
  }
  if (device->Init() != 0) {
    return failure(mode, AudioDeviceError::initialization_failed,
                   "audio device initialization failed");
  }

  const auto terminate_on_failure = [&device, mode](AudioDeviceError error,
                                                    std::string message) {
    static_cast<void>(device->Terminate());
    return failure(mode, error, std::move(message));
  };

  if (mode == AudioDeviceMode::microphone && !native_initializer) {
    const auto recording_devices = device->RecordingDevices();
    if (recording_devices <= 0) {
      return terminate_on_failure(
          AudioDeviceError::dependency_unavailable,
          "no native microphone recording device is available");
    }
    native_initializer = [](webrtc::AudioDeviceModule &native_device) {
      const auto selection = recording_device_selection(kCurrentAudioPlatform);
      const auto selection_result =
          selection == RecordingDeviceSelection::default_communication
              ? native_device.SetRecordingDevice(
                    webrtc::AudioDeviceModule::kDefaultCommunicationDevice)
              : native_device.SetRecordingDevice(static_cast<std::uint16_t>(0));
      if (selection_result != 0 || native_device.InitMicrophone() != 0 ||
          native_device.SetStereoRecording(false) != 0 ||
          native_device.InitRecording() != 0) {
        return NativeAudioInitializationResult::failure(
            AudioDeviceError::initialization_failed,
            "native microphone recording initialization failed");
      }
      return NativeAudioInitializationResult::success();
    };
  }

  if (mode == AudioDeviceMode::playout && !native_initializer) {
    const auto playout_devices = device->PlayoutDevices();
    if (playout_devices <= 0) {
      return terminate_on_failure(
          AudioDeviceError::dependency_unavailable,
          "no native speaker playout device is available");
    }
    native_initializer = [](webrtc::AudioDeviceModule &native_device) {
#if defined(WEBRTC_WIN)
      const auto selection_result = native_device.SetPlayoutDevice(
          webrtc::AudioDeviceModule::kDefaultDevice);
#else
      const auto selection_result =
          native_device.SetPlayoutDevice(static_cast<std::uint16_t>(0));
#endif
      if (selection_result != 0 || native_device.InitSpeaker() != 0 ||
          native_device.SetStereoPlayout(true) != 0 ||
          native_device.InitPlayout() != 0) {
        return NativeAudioInitializationResult::failure(
            AudioDeviceError::initialization_failed,
            "native speaker playout initialization failed");
      }
      return NativeAudioInitializationResult::success();
    };
  }

  if (mode == AudioDeviceMode::microphone) {
    NativeAudioInitializationResult initialization;
    try {
      initialization = native_initializer(*device);
    } catch (const std::exception &) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native microphone initializer raised an exception");
    } catch (...) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native microphone initializer raised an exception");
    }
    if (!initialization.ok()) {
      return terminate_on_failure(initialization.error,
                                  std::move(initialization.message));
    }
    if (!device->RecordingIsInitialized()) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native initializer returned success without recording readiness");
    }
  } else if (mode == AudioDeviceMode::playout) {
    NativeAudioInitializationResult initialization;
    try {
      initialization = native_initializer(*device);
    } catch (const std::exception &) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native speaker initializer raised an exception");
    } catch (...) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native speaker initializer raised an exception");
    }
    if (!initialization.ok()) {
      return terminate_on_failure(initialization.error,
                                  std::move(initialization.message));
    }
    if (!device->PlayoutIsInitialized() || device->RecordingIsInitialized()) {
      return terminate_on_failure(
          AudioDeviceError::initialization_failed,
          "native initializer returned invalid speaker playout readiness");
    }
  } else if (device->InitRecording() != 0) {
    return terminate_on_failure(AudioDeviceError::initialization_failed,
                                "synthetic recording initialization failed");
  }

  return {
      .device = std::move(device),
      .mode = mode,
      .processing = audio_processing_policy(source_kind(mode)),
      .remote_playout = remote_playout_policy(mode),
      .error = AudioDeviceError::none,
      .message = {},
  };
}

NativeAudioDeviceResult
create_native_audio_device(const webrtc::Environment &environment) {
  auto device = webrtc::CreateAudioDeviceModule(
      environment, webrtc::AudioDeviceModule::kPlatformDefaultAudio);
  if (device == nullptr) {
    return NativeAudioDeviceResult::failure(
        AudioDeviceError::dependency_unavailable,
        "platform-default audio device module creation failed");
  }
  return NativeAudioDeviceResult::success(std::move(device));
}

} // namespace

NativeAudioDeviceResult NativeAudioDeviceResult::success(
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> device) {
  return {
      .device = std::move(device),
      .error = AudioDeviceError::none,
      .message = {},
  };
}

NativeAudioDeviceResult NativeAudioDeviceResult::failure(AudioDeviceError error,
                                                         std::string message) {
  return {
      .device = nullptr,
      .error = error == AudioDeviceError::none
                   ? AudioDeviceError::initialization_failed
                   : error,
      .message = std::move(message),
  };
}

NativeAudioInitializationResult NativeAudioInitializationResult::success() {
  return {
      .error = AudioDeviceError::none,
      .message = {},
  };
}

NativeAudioInitializationResult
NativeAudioInitializationResult::failure(AudioDeviceError error,
                                         std::string message) {
  return {
      .error = error == AudioDeviceError::none
                   ? AudioDeviceError::initialization_failed
                   : error,
      .message = std::move(message),
  };
}

AudioProcessingPolicy audio_processing_policy(AudioSourceKind source) noexcept {
  return source == AudioSourceKind::microphone
             ? AudioProcessingPolicy::aec_ns_agc
             : AudioProcessingPolicy::unprocessed;
}

webrtc::AudioOptions audio_options(AudioSourceKind source) {
  const bool enable_processing = source == AudioSourceKind::microphone;
  webrtc::AudioOptions options;
  options.echo_cancellation = enable_processing;
  options.auto_gain_control = enable_processing;
  options.noise_suppression = enable_processing;
  return options;
}

RecordingDeviceSelection
recording_device_selection(NativeAudioPlatform platform) noexcept {
  return platform == NativeAudioPlatform::windows
             ? RecordingDeviceSelection::default_communication
             : RecordingDeviceSelection::index_zero;
}

AudioDeviceResult
create_audio_device(const webrtc::Environment &environment,
                    AudioDeviceMode mode,
                    NativeAudioDeviceFactory native_factory,
                    NativeAudioDeviceInitializer native_initializer,
                    MicrophonePermissionPreflight permission_preflight) {
  if (mode == AudioDeviceMode::synthetic) {
    auto device = webrtc::TestAudioDeviceModule::Create(
        environment,
        std::make_unique<ToneCapturer>(kSampleRate, kChannels, kFrequency,
                                       kAmplitude),
        webrtc::TestAudioDeviceModule::CreateDiscardRenderer(kSampleRate,
                                                             kChannels));
    return initialize_recording_device(std::move(device), mode);
  }

  if (mode == AudioDeviceMode::microphone && permission_preflight) {
    MicrophonePermissionStatus permission_status;
    try {
      permission_status = permission_preflight();
    } catch (const std::exception &) {
      return failure(mode, AudioDeviceError::initialization_failed,
                     "microphone permission preflight raised an exception");
    } catch (...) {
      return failure(mode, AudioDeviceError::initialization_failed,
                     "microphone permission preflight raised an exception");
    }
    if (permission_status == MicrophonePermissionStatus::denied) {
      return failure(mode, AudioDeviceError::permission_denied,
                     "microphone permission preflight denied");
    }
  }
  if (!native_factory) {
    native_factory = create_native_audio_device;
  }
  NativeAudioDeviceResult native;
  try {
    native = native_factory(environment);
  } catch (const std::exception &) {
    return failure(mode, AudioDeviceError::initialization_failed,
                   "native audio device factory raised an exception");
  } catch (...) {
    return failure(mode, AudioDeviceError::initialization_failed,
                   "native audio device factory raised an exception");
  }
  if (native.device == nullptr || native.error != AudioDeviceError::none) {
    const auto error = native.error == AudioDeviceError::none
                           ? AudioDeviceError::dependency_unavailable
                           : native.error;
    return failure(mode, error, std::move(native.message));
  }
  return initialize_recording_device(std::move(native.device), mode,
                                     std::move(native_initializer));
}

} // namespace shareme::rtc
