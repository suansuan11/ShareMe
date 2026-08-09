#include "shareme/rtc/video_encoder_selection.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/sdp_video_format.h"
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

class CapturingEncodedImageCallback final
    : public webrtc::EncodedImageCallback {
 public:
  Result OnEncodedImage(
      const webrtc::EncodedImage &image,
      const webrtc::CodecSpecificInfo *) override {
    ++encoded_count;
    images.push_back(image);
    encoded_bytes.assign(image.data(), image.data() + image.size());
    encoded_width = image._encodedWidth;
    encoded_height = image._encodedHeight;
    rtp_timestamp = image.RtpTimestamp();
    output_is_annex_b = image.size() >= 4 && image.data()[0] == 0 &&
                        image.data()[1] == 0 && image.data()[2] == 0 &&
                        image.data()[3] == 1;
    return Result(Result::OK);
  }

  void OnFrameDropped(uint32_t, int, bool) override {}

  int encoded_count = 0;
  bool output_is_annex_b = false;
  std::vector<std::uint8_t> encoded_bytes;
  std::uint32_t encoded_width = 0;
  std::uint32_t encoded_height = 0;
  std::uint32_t rtp_timestamp = 0;
  std::vector<webrtc::EncodedImage> images;
};

class CapturingDecodedImageCallback final
    : public webrtc::DecodedImageCallback {
 public:
  int32_t Decoded(webrtc::VideoFrame &frame) override {
    ++decoded_count;
    width = frame.width();
    height = frame.height();
    rtp_timestamp = frame.rtp_timestamp();
    return 0;
  }

  int decoded_count = 0;
  int width = 0;
  int height = 0;
  std::uint32_t rtp_timestamp = 0;
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
      [](int width, int height, std::string &reason) {
        REQUIRE(width == 2'560);
        REQUIRE(height == 1'440);
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
      [](int width, int height, std::string &reason) {
        REQUIRE(width == 2'560);
        REQUIRE(height == 1'440);
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
        [](int, int, std::string &reason) {
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
      [](int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      fake_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "videotoolbox-h264-unsupported");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void rejects_a_factory_that_cannot_create_an_encoder() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      failing_h264_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "videotoolbox-encoder-unavailable");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void rejects_a_factory_that_cannot_initialize_an_encoder() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::quality,
      [](int, int, std::string &reason) {
        reason.clear();
        return true;
      },
      failing_init_h264_factory);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.fallback_active);
  REQUIRE(selection.diagnostics.fallback_reason ==
          "videotoolbox-encoder-initialization-failed");
  REQUIRE(selection.capture_profile == ScreenStreamProfile::standard);
}

void factory_failure_is_a_bounded_software_fallback() {
  using shareme::core::ScreenStreamProfile;

  const auto selection = shareme::rtc::select_screen_video_encoder(
      ScreenStreamProfile::cinema,
      [](int, int, std::string &reason) {
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
          "videotoolbox-factory-unavailable");
  REQUIRE(selection.max_width == 1'920);
  REQUIRE(selection.max_height == 1'080);
}

void runtime_accepts_the_selected_factory_without_changing_lifecycle() {
  auto runtime = shareme::rtc::WebRtcRuntime::create(nullptr, fake_factory());
  REQUIRE(runtime != nullptr);
  REQUIRE(runtime->threads_running());
  REQUIRE(runtime->stop());
}

void selects_the_native_windows_media_foundation_encoder() {
#if defined(_WIN32)
  std::string reason;
  REQUIRE(shareme::rtc::probe_platform_h264_encoder(1'920, 1'080, reason));
  REQUIRE(reason.empty());
  const auto selection = shareme::rtc::select_screen_video_encoder(
      shareme::core::ScreenStreamProfile::standard);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.hardware_active);
  REQUIRE(selection.diagnostics.negotiated_codec == "H264");
  REQUIRE(selection.diagnostics.encoder_implementation == "MediaFoundation");
#endif
}

void native_windows_media_foundation_encoder_produces_h264() {
#if defined(_WIN32)
  REQUIRE(shareme::rtc::create_platform_video_decoder_factory() != nullptr);
  auto factory = shareme::rtc::create_platform_h264_encoder_factory();
  REQUIRE(factory != nullptr);
  const auto formats = factory->GetSupportedFormats();
  REQUIRE(!formats.empty());
  auto encoder = factory->Create(webrtc::CreateEnvironment(), formats.front());
  REQUIRE(encoder != nullptr);

  webrtc::VideoCodec codec;
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = 1'920;
  codec.height = 1'080;
  codec.startBitrate = 8'000;
  codec.maxBitrate = 15'000;
  codec.minBitrate = 100;
  codec.maxFramerate = 60;
  codec.active = true;
  codec.mode = webrtc::VideoCodecMode::kScreensharing;
  *codec.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
  const webrtc::VideoEncoder::Settings settings(
      webrtc::VideoEncoder::Capabilities(false), 1, 1'200);
  REQUIRE(encoder->InitEncode(&codec, settings) == 0);

  CapturingEncodedImageCallback callback;
  REQUIRE(encoder->RegisterEncodeCompleteCallback(&callback) == 0);
  auto buffer = webrtc::I420Buffer::Create(codec.width, codec.height);
  buffer->InitializeData();
  for (std::uint32_t sequence = 0; sequence < 20; ++sequence) {
    const std::vector<webrtc::VideoFrameType> frame_types = {
        sequence == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                      : webrtc::VideoFrameType::kVideoFrameDelta};
    const auto frame = webrtc::VideoFrame::Builder()
                           .set_video_frame_buffer(buffer)
                           .set_timestamp_us(sequence * 16'667LL)
                           .set_rtp_timestamp(sequence * 1'500U)
                           .set_rotation(webrtc::kVideoRotation_0)
                           .build();
    REQUIRE(encoder->Encode(frame, &frame_types) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(callback.encoded_count > 0);
  REQUIRE(callback.output_is_annex_b);
  REQUIRE(encoder->Release() == 0);

  auto decoder_factory =
      shareme::rtc::create_platform_video_decoder_factory();
  auto decoder = decoder_factory->Create(webrtc::CreateEnvironment(),
                                         decoder_factory->GetSupportedFormats().front());
  REQUIRE(decoder != nullptr);
  CapturingDecodedImageCallback decoded_callback;
  REQUIRE(decoder->RegisterDecodeCompleteCallback(&decoded_callback) == 0);
  webrtc::VideoDecoder::Settings decoder_settings;
  decoder_settings.set_codec_type(webrtc::kVideoCodecH264);
  REQUIRE(decoder->Configure(decoder_settings));
  for (std::size_t index = 0; index < callback.images.size(); ++index) {
    const auto result = decoder->Decode(callback.images[index], 0);
    if (result != 0)
      std::cerr << "Media Foundation decode failed for access unit " << index
                << " with code " << result << '\n';
    REQUIRE(result == 0);
  }
  REQUIRE(decoded_callback.decoded_count > 0);
  REQUIRE(decoded_callback.width == 1'920);
  REQUIRE(decoded_callback.height == 1'080);
  REQUIRE(decoded_callback.rtp_timestamp == callback.rtp_timestamp);
  REQUIRE(decoder->Release() == 0);
#endif
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
  selects_the_native_windows_media_foundation_encoder();
  native_windows_media_foundation_encoder_produces_h264();
  return EXIT_SUCCESS;
}
