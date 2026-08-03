#include "audio_device_factory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

#include "api/audio/audio_device_defines.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_device/include/test_audio_device.h"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition) {
    return;
  }
  std::cerr << "requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

class RecordingTransport final : public webrtc::AudioTransport {
public:
  int32_t RecordedDataIsAvailable(
      const void *audio_samples, std::size_t samples_per_channel,
      std::size_t bytes_per_sample, std::size_t channels,
      std::uint32_t samples_per_second, std::uint32_t total_delay_ms,
      std::int32_t clock_drift, std::uint32_t current_mic_level,
      bool key_pressed, std::uint32_t &new_mic_level) override {
    static_cast<void>(total_delay_ms);
    static_cast<void>(clock_drift);
    static_cast<void>(key_pressed);
    new_mic_level = current_mic_level;

    const auto *samples = static_cast<const std::int16_t *>(audio_samples);
    const auto sample_count = samples_per_channel * channels;
    const bool has_non_zero_sample =
        std::any_of(samples, samples + sample_count,
                    [](std::int16_t sample) { return sample != 0; });

    std::lock_guard lock(mutex_);
    valid_format_ = valid_format_ && samples_per_channel == 480 &&
                    bytes_per_sample == sizeof(std::int16_t) && channels == 1 &&
                    samples_per_second == 48'000;
    has_non_zero_sample_ = has_non_zero_sample_ || has_non_zero_sample;
    first_samples_.push_back(samples[0]);
    if (first_frame_.empty()) {
      first_frame_.assign(samples, samples + sample_count);
    }
    ++recorded_frames_;
    recorded_frame_available_.notify_all();
    return 0;
  }

  int32_t NeedMorePlayData(std::size_t samples_per_channel,
                           std::size_t bytes_per_sample, std::size_t channels,
                           std::uint32_t samples_per_second,
                           void *audio_samples, std::size_t &samples_out,
                           std::int64_t *elapsed_time_ms,
                           std::int64_t *ntp_time_ms) override {
    static_cast<void>(samples_per_second);
    static_cast<void>(bytes_per_sample);
    auto *samples = static_cast<std::int16_t *>(audio_samples);
    std::fill_n(samples, samples_per_channel * channels, std::int16_t{0});
    samples_out = samples_per_channel;
    if (elapsed_time_ms != nullptr) {
      *elapsed_time_ms = 0;
    }
    if (ntp_time_ms != nullptr) {
      *ntp_time_ms = 0;
    }
    ++playout_callbacks_;
    return 0;
  }

  void PullRenderData(int bits_per_sample, int sample_rate,
                      std::size_t channels, std::size_t frames,
                      void *audio_data, std::int64_t *elapsed_time_ms,
                      std::int64_t *ntp_time_ms) override {
    static_cast<void>(sample_rate);
    const auto bytes_per_sample = static_cast<std::size_t>(bits_per_sample / 8);
    std::fill_n(static_cast<std::uint8_t *>(audio_data),
                frames * channels * bytes_per_sample, std::uint8_t{0});
    if (elapsed_time_ms != nullptr) {
      *elapsed_time_ms = 0;
    }
    if (ntp_time_ms != nullptr) {
      *ntp_time_ms = 0;
    }
  }

  [[nodiscard]] std::size_t recorded_frames() const {
    std::lock_guard lock(mutex_);
    return recorded_frames_;
  }

  [[nodiscard]] bool
  wait_for_recorded_frames(std::size_t count,
                           std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return recorded_frame_available_.wait_for(
        lock, timeout, [&] { return recorded_frames_ >= count; });
  }

  [[nodiscard]] bool valid_format() const {
    std::lock_guard lock(mutex_);
    return valid_format_;
  }

  [[nodiscard]] bool has_non_zero_sample() const {
    std::lock_guard lock(mutex_);
    return has_non_zero_sample_;
  }

  [[nodiscard]] bool matches_continuous_440_hz_tone() const {
    std::lock_guard lock(mutex_);
    if (first_frame_.size() != 480 || first_samples_.size() < 2) {
      return false;
    }
    const auto expected_sample = [](std::size_t sample_index) {
      constexpr double sample_rate = 48'000.0;
      constexpr double frequency = 440.0;
      constexpr double amplitude = 8'000.0;
      return static_cast<std::int16_t>(std::lround(
          std::sin(2.0 * std::numbers::pi * frequency *
                   static_cast<double>(sample_index) / sample_rate) *
          amplitude));
    };
    constexpr std::size_t probe_indices[]{0, 1, 120, 240, 479};
    const auto first_frame_matches =
        std::all_of(std::begin(probe_indices), std::end(probe_indices),
                    [&](std::size_t index) {
                      return first_frame_[index] == expected_sample(index);
                    });
    return first_frame_matches &&
           first_samples_[1] == expected_sample(first_frame_.size()) &&
           first_samples_[1] != first_samples_.front();
  }

  [[nodiscard]] std::size_t playout_callbacks() const {
    return playout_callbacks_.load(std::memory_order_acquire);
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable recorded_frame_available_;
  std::size_t recorded_frames_{0};
  bool valid_format_{true};
  bool has_non_zero_sample_{false};
  std::vector<std::int16_t> first_frame_;
  std::vector<std::int16_t> first_samples_;
  std::atomic_size_t playout_callbacks_{0};
};

void synthetic_audio_is_continuous_and_recording_ready() {
  using namespace std::chrono_literals;
  const auto environment = webrtc::CreateEnvironment();
  auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::synthetic);

  REQUIRE(result.ok());
  REQUIRE(result.device != nullptr);
  REQUIRE(result.error == shareme::rtc::AudioDeviceError::none);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::synthetic);
  REQUIRE(result.processing ==
          shareme::rtc::AudioProcessingPolicy::unprocessed);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::discard);
  REQUIRE(result.device->Initialized());
  REQUIRE(result.device->RecordingIsInitialized());
  REQUIRE(!result.device->Playing());

  RecordingTransport transport;
  REQUIRE(result.device->RegisterAudioCallback(&transport) == 0);
  REQUIRE(result.device->StartRecording() == 0);
  REQUIRE(transport.wait_for_recorded_frames(10, 150ms));
  REQUIRE(result.device->StopRecording() == 0);

  REQUIRE(transport.recorded_frames() >= 10);
  REQUIRE(transport.valid_format());
  REQUIRE(transport.has_non_zero_sample());
  REQUIRE(transport.matches_continuous_440_hz_tone());

  REQUIRE(transport.playout_callbacks() == 0);
  REQUIRE(!result.device->PlayoutIsInitialized());
  REQUIRE(!result.device->Playing());
  REQUIRE(result.device->Terminate() == 0);
}

shareme::rtc::NativeAudioDeviceFactory test_native_device_factory() {
  return [](const webrtc::Environment &factory_env) {
    auto device = webrtc::TestAudioDeviceModule::Create(
        factory_env,
        webrtc::TestAudioDeviceModule::CreatePulsedNoiseCapturer(1'000, 48'000,
                                                                 1),
        webrtc::TestAudioDeviceModule::CreateDiscardRenderer(48'000, 1));
    return shareme::rtc::NativeAudioDeviceResult::success(std::move(device));
  };
}

void require_safe_microphone_failure(
    const shareme::rtc::AudioDeviceResult &result) {
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::microphone);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
}

void microphone_dependency_failure_is_typed_and_never_falls_back() {
  const auto environment = webrtc::CreateEnvironment();
  bool native_factory_called = false;
  const auto missing_factory =
      [&native_factory_called](const webrtc::Environment &) {
        native_factory_called = true;
        return shareme::rtc::NativeAudioDeviceResult::failure(
            shareme::rtc::AudioDeviceError::dependency_unavailable,
            "native audio dependency unavailable");
      };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone, missing_factory);

  REQUIRE(native_factory_called);
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::microphone);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::dependency_unavailable);
  REQUIRE(result.processing == shareme::rtc::AudioProcessingPolicy::aec_ns_agc);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::discard);
}

void microphone_initialization_failure_is_typed_and_never_falls_back() {
  const auto environment = webrtc::CreateEnvironment();
  bool native_initializer_called = false;
  const auto failing_initializer =
      [&native_initializer_called](webrtc::AudioDeviceModule &) {
        native_initializer_called = true;
        return shareme::rtc::NativeAudioInitializationResult::failure(
            shareme::rtc::AudioDeviceError::initialization_failed,
            "recording initialization failed");
      };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone,
      test_native_device_factory(), failing_initializer);

  REQUIRE(native_initializer_called);
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::microphone);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
  REQUIRE(result.processing == shareme::rtc::AudioProcessingPolicy::aec_ns_agc);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::discard);
}

void native_initializer_preserves_typed_failures_without_fallback() {
  const auto environment = webrtc::CreateEnvironment();
  constexpr shareme::rtc::AudioDeviceError expected_errors[]{
      shareme::rtc::AudioDeviceError::dependency_unavailable,
      shareme::rtc::AudioDeviceError::permission_denied,
      shareme::rtc::AudioDeviceError::initialization_failed,
  };
  for (const auto expected_error : expected_errors) {
    const auto initializer = [expected_error](webrtc::AudioDeviceModule &) {
      return shareme::rtc::NativeAudioInitializationResult::failure(
          expected_error, "typed native initialization failure");
    };
    const auto result = shareme::rtc::create_audio_device(
        environment, shareme::rtc::AudioDeviceMode::microphone,
        test_native_device_factory(), initializer);
    REQUIRE(!result.ok());
    REQUIRE(result.device == nullptr);
    REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::microphone);
    REQUIRE(result.error == expected_error);
  }
}

void initializer_success_requires_recording_ready_device() {
  const auto environment = webrtc::CreateEnvironment();
  const auto incomplete_initializer = [](webrtc::AudioDeviceModule &) {
    return shareme::rtc::NativeAudioInitializationResult::success();
  };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone,
      test_native_device_factory(), incomplete_initializer);

  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
}

void microphone_permission_failure_is_typed_and_never_falls_back() {
  const auto environment = webrtc::CreateEnvironment();
  bool native_factory_called = false;
  const auto denied_factory =
      [&native_factory_called](const webrtc::Environment &) {
        native_factory_called = true;
        return shareme::rtc::NativeAudioDeviceResult::failure(
            shareme::rtc::AudioDeviceError::permission_denied,
            "microphone permission denied");
      };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone, denied_factory);

  REQUIRE(native_factory_called);
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::microphone);
  REQUIRE(result.error == shareme::rtc::AudioDeviceError::permission_denied);
  REQUIRE(result.processing == shareme::rtc::AudioProcessingPolicy::aec_ns_agc);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::discard);
}

void permission_preflight_denial_avoids_native_adm_creation() {
  const auto environment = webrtc::CreateEnvironment();
  bool native_factory_called = false;
  const auto native_factory =
      [&native_factory_called](const webrtc::Environment &factory_env) {
        native_factory_called = true;
        return test_native_device_factory()(factory_env);
      };
  const auto denied_preflight = [] {
    return shareme::rtc::MicrophonePermissionStatus::denied;
  };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone, native_factory,
      {}, denied_preflight);

  REQUIRE(!native_factory_called);
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.error == shareme::rtc::AudioDeviceError::permission_denied);
}

void native_playout_initializes_stereo_without_microphone_preflight() {
  const auto environment = webrtc::CreateEnvironment();
  bool native_factory_called = false;
  bool permission_preflight_called = false;
  const auto native_factory =
      [&native_factory_called](const webrtc::Environment &factory_env) {
        native_factory_called = true;
        return test_native_device_factory()(factory_env);
      };
  const auto native_initializer = [](webrtc::AudioDeviceModule &device) {
    REQUIRE(device.SetPlayoutDevice(static_cast<std::uint16_t>(0)) == 0);
    REQUIRE(device.InitSpeaker() == 0);
    REQUIRE(device.SetStereoPlayout(true) == 0);
    REQUIRE(device.InitPlayout() == 0);
    return shareme::rtc::NativeAudioInitializationResult::success();
  };
  const auto denied_preflight = [&permission_preflight_called] {
    permission_preflight_called = true;
    return shareme::rtc::MicrophonePermissionStatus::denied;
  };

  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::playout, native_factory,
      native_initializer, denied_preflight);

  REQUIRE(native_factory_called);
  REQUIRE(!permission_preflight_called);
  REQUIRE(result.ok());
  REQUIRE(result.device != nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::playout);
  REQUIRE(result.processing ==
          shareme::rtc::AudioProcessingPolicy::unprocessed);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::native);
  REQUIRE(result.device->Initialized());
  REQUIRE(result.device->PlayoutIsInitialized());
  REQUIRE(!result.device->RecordingIsInitialized());
  REQUIRE(result.device->Terminate() == 0);
}

void native_playout_failure_is_typed_and_never_falls_back() {
  const auto environment = webrtc::CreateEnvironment();
  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::playout,
      [](const webrtc::Environment &) {
        return shareme::rtc::NativeAudioDeviceResult::failure(
            shareme::rtc::AudioDeviceError::dependency_unavailable,
            "native output device unavailable");
      });

  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.mode == shareme::rtc::AudioDeviceMode::playout);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::dependency_unavailable);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::native);
}

void native_playout_initializer_failures_are_typed_without_fallback() {
  const auto environment = webrtc::CreateEnvironment();
  bool initializer_called = false;
  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::playout,
      test_native_device_factory(), [&initializer_called](
                                        webrtc::AudioDeviceModule &) {
        initializer_called = true;
        return shareme::rtc::NativeAudioInitializationResult::failure(
            shareme::rtc::AudioDeviceError::dependency_unavailable,
            "native speaker initialization unavailable");
      });

  REQUIRE(initializer_called);
  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::dependency_unavailable);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::native);
}

void native_playout_initializer_success_requires_playout_only_readiness() {
  const auto environment = webrtc::CreateEnvironment();
  const auto result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::playout,
      test_native_device_factory(), [](webrtc::AudioDeviceModule &) {
        return shareme::rtc::NativeAudioInitializationResult::success();
      });

  REQUIRE(!result.ok());
  REQUIRE(result.device == nullptr);
  REQUIRE(result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
  REQUIRE(result.remote_playout == shareme::rtc::RemotePlayoutPolicy::native);
}

void seam_exceptions_become_sanitized_typed_failures() {
  const auto environment = webrtc::CreateEnvironment();

  for (const bool throw_standard_exception : {true, false}) {
    const auto throwing_preflight = [throw_standard_exception]()
        -> shareme::rtc::MicrophonePermissionStatus {
      if (throw_standard_exception) {
        throw std::runtime_error("private permission diagnostic");
      }
      throw 7;
    };
    const auto preflight_result = shareme::rtc::create_audio_device(
        environment, shareme::rtc::AudioDeviceMode::microphone, {}, {},
        throwing_preflight);
    require_safe_microphone_failure(preflight_result);
  }

  for (const bool throw_standard_exception : {true, false}) {
    const auto throwing_factory =
        [throw_standard_exception](const webrtc::Environment &)
        -> shareme::rtc::NativeAudioDeviceResult {
      if (throw_standard_exception) {
        throw std::runtime_error("private factory diagnostic");
      }
      throw 11;
    };
    const auto factory_result = shareme::rtc::create_audio_device(
        environment, shareme::rtc::AudioDeviceMode::microphone,
        throwing_factory);
    require_safe_microphone_failure(factory_result);
  }

  for (const bool throw_standard_exception : {true, false}) {
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> created_device;
    const auto retaining_factory =
        [&created_device](const webrtc::Environment &factory_env) {
          auto result = test_native_device_factory()(factory_env);
          created_device = result.device;
          return result;
        };
    const auto throwing_initializer =
        [throw_standard_exception](webrtc::AudioDeviceModule &)
        -> shareme::rtc::NativeAudioInitializationResult {
      if (throw_standard_exception) {
        throw std::runtime_error("private initializer diagnostic");
      }
      throw 13;
    };
    const auto initializer_result = shareme::rtc::create_audio_device(
        environment, shareme::rtc::AudioDeviceMode::microphone,
        retaining_factory, throwing_initializer);
    require_safe_microphone_failure(initializer_result);
    REQUIRE(created_device != nullptr);
    REQUIRE(!created_device->Initialized());
  }
}

void invalid_typed_factory_results_never_report_success() {
  const auto environment = webrtc::CreateEnvironment();
  const auto missing_without_error = [](const webrtc::Environment &) {
    return shareme::rtc::NativeAudioDeviceResult{
        .device = nullptr,
        .error = shareme::rtc::AudioDeviceError::none,
        .message = "invalid missing device success",
    };
  };
  const auto missing_result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone,
      missing_without_error);
  REQUIRE(!missing_result.ok());
  REQUIRE(missing_result.device == nullptr);

  const auto device_with_error = [](const webrtc::Environment &factory_env) {
    auto native = test_native_device_factory()(factory_env);
    native.error = shareme::rtc::AudioDeviceError::initialization_failed;
    native.message = "invalid device and error";
    return native;
  };
  const auto error_result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone,
      device_with_error);
  REQUIRE(!error_result.ok());
  REQUIRE(error_result.device == nullptr);
  REQUIRE(error_result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
}

void failure_factories_reject_none_error_invariants() {
  const auto environment = webrtc::CreateEnvironment();
  const auto invalid_initializer = [](webrtc::AudioDeviceModule &device) {
    REQUIRE(device.InitRecording() == 0);
    return shareme::rtc::NativeAudioInitializationResult::failure(
        shareme::rtc::AudioDeviceError::none,
        "invalid initializer failure without an error");
  };
  const auto initializer_result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone,
      test_native_device_factory(), invalid_initializer);
  REQUIRE(!initializer_result.ok());
  REQUIRE(initializer_result.device == nullptr);
  REQUIRE(initializer_result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);

  const auto invalid_factory = [](const webrtc::Environment &) {
    return shareme::rtc::NativeAudioDeviceResult::failure(
        shareme::rtc::AudioDeviceError::none,
        "invalid native factory failure without an error");
  };
  const auto factory_result = shareme::rtc::create_audio_device(
      environment, shareme::rtc::AudioDeviceMode::microphone, invalid_factory);
  REQUIRE(!factory_result.ok());
  REQUIRE(factory_result.device == nullptr);
  REQUIRE(factory_result.error ==
          shareme::rtc::AudioDeviceError::initialization_failed);
}

void recording_device_selection_is_platform_explicit() {
  REQUIRE(shareme::rtc::recording_device_selection(
              shareme::rtc::NativeAudioPlatform::windows) ==
          shareme::rtc::RecordingDeviceSelection::default_communication);
  REQUIRE(shareme::rtc::recording_device_selection(
              shareme::rtc::NativeAudioPlatform::other) ==
          shareme::rtc::RecordingDeviceSelection::index_zero);
}

void audio_options_are_explicit_for_every_source() {
  const auto microphone =
      shareme::rtc::audio_options(shareme::rtc::AudioSourceKind::microphone);
  REQUIRE(microphone.echo_cancellation == true);
  REQUIRE(microphone.auto_gain_control == true);
  REQUIRE(microphone.noise_suppression == true);

  for (const auto source : {shareme::rtc::AudioSourceKind::synthetic,
                            shareme::rtc::AudioSourceKind::movie}) {
    const auto unprocessed = shareme::rtc::audio_options(source);
    REQUIRE(unprocessed.echo_cancellation == false);
    REQUIRE(unprocessed.auto_gain_control == false);
    REQUIRE(unprocessed.noise_suppression == false);
  }
}

void processing_is_reserved_for_microphone_sources() {
  REQUIRE(shareme::rtc::audio_processing_policy(
              shareme::rtc::AudioSourceKind::microphone) ==
          shareme::rtc::AudioProcessingPolicy::aec_ns_agc);
  REQUIRE(shareme::rtc::audio_processing_policy(
              shareme::rtc::AudioSourceKind::synthetic) ==
          shareme::rtc::AudioProcessingPolicy::unprocessed);
  REQUIRE(shareme::rtc::audio_processing_policy(
              shareme::rtc::AudioSourceKind::movie) ==
          shareme::rtc::AudioProcessingPolicy::unprocessed);
}

} // namespace

int main() {
  synthetic_audio_is_continuous_and_recording_ready();
  microphone_dependency_failure_is_typed_and_never_falls_back();
  microphone_initialization_failure_is_typed_and_never_falls_back();
  native_initializer_preserves_typed_failures_without_fallback();
  initializer_success_requires_recording_ready_device();
  microphone_permission_failure_is_typed_and_never_falls_back();
  permission_preflight_denial_avoids_native_adm_creation();
  native_playout_initializes_stereo_without_microphone_preflight();
  native_playout_failure_is_typed_and_never_falls_back();
  native_playout_initializer_failures_are_typed_without_fallback();
  native_playout_initializer_success_requires_playout_only_readiness();
  seam_exceptions_become_sanitized_typed_failures();
  invalid_typed_factory_results_never_report_success();
  failure_factories_reject_none_error_invariants();
  recording_device_selection_is_platform_explicit();
  audio_options_are_explicit_for_every_source();
  processing_is_reserved_for_microphone_sources();
  return EXIT_SUCCESS;
}
