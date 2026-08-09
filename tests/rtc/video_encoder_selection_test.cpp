#include "shareme/rtc/video_encoder_selection.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/sdp_video_format.h"
#include "audio_device_factory.hpp"
#include "webrtc_runtime.hpp"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

using FakeFactory = webrtc::VideoEncoderFactoryTemplate<
    webrtc::LibvpxVp8EncoderTemplateAdapter>;

class NoopEncoder final : public webrtc::VideoEncoder {
 public:
  explicit NoopEncoder(bool can_initialize)
      : can_initialize_(can_initialize) {}

  int InitEncode(const webrtc::VideoCodec *codec_settings,
                 const Settings &settings) override {
    static_cast<void>(codec_settings);
    static_cast<void>(settings);
    return can_initialize_ ? 0 : -1;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback *callback) override {
    static_cast<void>(callback);
    return 0;
  }
  int32_t Release() override { return 0; }
  int32_t Encode(const webrtc::VideoFrame &frame,
                 const std::vector<webrtc::VideoFrameType> *frame_types)
      override {
    static_cast<void>(frame);
    static_cast<void>(frame_types);
    return 0;
  }
  void SetRates(const RateControlParameters &parameters) override {
    static_cast<void>(parameters);
  }
  EncoderInfo GetEncoderInfo() const override { return {}; }

 private:
  bool can_initialize_;
};

class H264Factory final : public webrtc::VideoEncoderFactory {
 public:
  H264Factory(bool can_create, bool can_initialize)
      : can_create_(can_create), can_initialize_(can_initialize) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {webrtc::SdpVideoFormat(
        "H264", {{"profile-level-id", "640c1f"},
                  {"packetization-mode", "1"}})};
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment &environment,
      const webrtc::SdpVideoFormat &format) override {
    static_cast<void>(environment);
    created_with_parameters = !format.parameters.empty();
    return can_create_ ? std::make_unique<NoopEncoder>(can_initialize_)
                       : nullptr;
  }

  static bool created_with_parameters;

 private:
  bool can_create_;
  bool can_initialize_;
};

bool H264Factory::created_with_parameters = false;

std::unique_ptr<webrtc::VideoEncoderFactory> fake_factory() {
  return std::make_unique<FakeFactory>();
}

std::unique_ptr<webrtc::VideoEncoderFactory> h264_factory() {
  return std::make_unique<H264Factory>(true, true);
}

std::unique_ptr<webrtc::VideoEncoderFactory> failing_h264_factory() {
  return std::make_unique<H264Factory>(false, true);
}

std::unique_ptr<webrtc::VideoEncoderFactory> failing_init_h264_factory() {
  return std::make_unique<H264Factory>(true, false);
}

void selects_hardware_only_after_a_true_probe_and_factory() {
  using shareme::core::ScreenStreamProfile;
  using shareme::rtc::VideoEncoderSelection;

  const auto unavailable = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int width, int height, int fps, std::string &reason) {
        REQUIRE(width == 2'560);
        REQUIRE(height == 1'440);
        REQUIRE(fps == 60);
        reason = "probe-rejected";
        return false;
      },
      fake_factory);
  REQUIRE(!unavailable.diagnostics.hardware_active);
  REQUIRE(unavailable.diagnostics.fallback_active);
  REQUIRE(unavailable.diagnostics.negotiated_codec == "VP8");
  REQUIRE(unavailable.diagnostics.fallback_reason == "probe-rejected");
  REQUIRE(unavailable.capture_profile == ScreenStreamProfile::standard);
  REQUIRE(unavailable.max_width == 1'920);
  REQUIRE(unavailable.max_height == 1'080);

  H264Factory::created_with_parameters = false;
  const auto selected = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int width, int height, int fps, std::string &reason) {
        REQUIRE(width == 2'560);
        REQUIRE(height == 1'440);
        REQUIRE(fps == 60);
        reason.clear();
        return true;
      },
      h264_factory, "MediaFoundation");
  REQUIRE(selected.factory != nullptr);
  REQUIRE(selected.diagnostics.requested_codec == "H264");
  REQUIRE(selected.diagnostics.negotiated_codec == "H264");
  REQUIRE(selected.diagnostics.encoder_implementation == "MediaFoundation");
  REQUIRE(selected.diagnostics.hardware_active);
  REQUIRE(!selected.diagnostics.fallback_active);
  REQUIRE(H264Factory::created_with_parameters);
  REQUIRE(selected.capture_profile == ScreenStreamProfile::quality);
  REQUIRE(selected.max_width == 2'560);
  REQUIRE(selected.max_height == 1'440);
  const auto formats = selected.factory->GetSupportedFormats();
  REQUIRE(formats.size() == 1);
  REQUIRE(formats.front().parameters.at("profile-level-id") == "640c33");
  REQUIRE(formats.front().parameters.at("packetization-mode") == "1");
}

void selects_levels_that_cover_each_screen_profile() {
  using shareme::core::ScreenStreamProfile;

  for (const auto &[profile, expected_level] : {
           std::pair{ScreenStreamProfile::standard, "640c2a"},
           std::pair{ScreenStreamProfile::cinema, "640c33"}}) {
    const auto selection = shareme::rtc::select_screen_video_encoder(
        profile,
        [](int, int, int, std::string &reason) {
          reason.clear();
          return true;
        },
        h264_factory);
    REQUIRE(selection.diagnostics.hardware_active);
    const auto formats = selection.factory->GetSupportedFormats();
    REQUIRE(formats.size() == 1);
    REQUIRE(formats.front().parameters.at("profile-level-id") ==
            expected_level);
  }
}

void rejects_a_factory_without_h264_support() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int, int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      fake_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "platform-h264-unsupported");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void rejects_a_factory_that_cannot_create_an_encoder() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int, int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      failing_h264_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "platform-h264-encoder-unavailable");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void rejects_a_factory_that_cannot_initialize_an_encoder() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int, int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      failing_init_h264_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "platform-h264-encoder-initialization-failed");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void factory_failure_is_a_bounded_software_fallback() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::cinema,
      [](int, int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      [] { return std::unique_ptr<webrtc::VideoEncoderFactory>{}; });
  REQUIRE(selection.factory != nullptr);
  REQUIRE(!selection.diagnostics.hardware_active);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.negotiated_codec == "VP8");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "platform-h264-factory-unavailable");
  REQUIRE(selection.max_width == 1'920);
  REQUIRE(selection.max_height == 1'080);
}

void runtime_accepts_the_selected_factory_without_changing_lifecycle() {
  const auto audio = shareme::rtc::create_audio_device(
      webrtc::CreateEnvironment(), shareme::rtc::AudioDeviceMode::synthetic);
  REQUIRE(audio.ok());
  auto runtime =
      shareme::rtc::WebRtcRuntime::create(audio.device, fake_factory());
  REQUIRE(runtime != nullptr);
  REQUIRE(runtime->threads_running());
  REQUIRE(runtime->stop());
}

} // namespace

int main() {
  selects_hardware_only_after_a_true_probe_and_factory();
  selects_levels_that_cover_each_screen_profile();
  rejects_a_factory_without_h264_support();
  rejects_a_factory_that_cannot_create_an_encoder();
  rejects_a_factory_that_cannot_initialize_an_encoder();
  factory_failure_is_a_bounded_software_fallback();
  runtime_accepts_the_selected_factory_without_changing_lifecycle();
  return EXIT_SUCCESS;
}
