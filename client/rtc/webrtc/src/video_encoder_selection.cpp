#include "shareme/rtc/video_encoder_selection.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"

namespace shareme::rtc {
namespace {

std::unique_ptr<webrtc::VideoEncoderFactory> create_vp8_factory() {
  return std::make_unique<webrtc::VideoEncoderFactoryTemplate<
      webrtc::LibvpxVp8EncoderTemplateAdapter>>();
}

std::string_view default_platform_h264_implementation() {
#if defined(__APPLE__)
  return "VideoToolbox";
#elif defined(_WIN32)
  return "MediaFoundation";
#else
  return "PlatformH264";
#endif
}

std::optional<webrtc::H264Level> required_h264_level(
    core::ScreenStreamProfile profile) {
  switch (profile) {
    case core::ScreenStreamProfile::standard:
      return webrtc::H264Level::kLevel4_2;
    case core::ScreenStreamProfile::quality:
    case core::ScreenStreamProfile::cinema:
      return webrtc::H264Level::kLevel5_1;
  }
  return std::nullopt;
}

webrtc::SdpVideoFormat adapt_h264_level(
    const webrtc::SdpVideoFormat &format, core::ScreenStreamProfile profile) {
  if (format.name != "H264")
    return format;

  const auto profile_level = format.parameters.find("profile-level-id");
  const auto level = required_h264_level(profile);
  if (profile_level == format.parameters.end() || !level.has_value())
    return format;

  const auto parsed = webrtc::ParseH264ProfileLevelId(
      profile_level->second.c_str());
  if (!parsed.has_value())
    return format;

  const auto adapted_id = webrtc::H264ProfileLevelIdToString(
      {parsed->profile, *level});
  if (!adapted_id.has_value())
    return format;

  auto adapted = format;
  adapted.parameters["profile-level-id"] = *adapted_id;
  return adapted;
}

class ScreenH264Factory final : public webrtc::VideoEncoderFactory {
 public:
  ScreenH264Factory(std::unique_ptr<webrtc::VideoEncoderFactory> factory,
                    core::ScreenStreamProfile profile)
      : factory_(std::move(factory)), profile_(profile) {
    for (const auto &format : factory_->GetSupportedFormats())
      formats_.push_back(adapt_h264_level(format, profile_));
  }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return formats_;
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment &environment,
      const webrtc::SdpVideoFormat &format) override {
    return factory_->Create(environment, adapt_h264_level(format, profile_));
  }

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> factory_;
  core::ScreenStreamProfile profile_;
  std::vector<webrtc::SdpVideoFormat> formats_;
};

std::optional<webrtc::SdpVideoFormat> supported_h264_format(
    const webrtc::VideoEncoderFactory &factory) {
  const auto formats = factory.GetSupportedFormats();
  const auto found = std::find_if(
      formats.begin(), formats.end(), [](const auto &format) {
        return format.name == "H264";
      });
  return found == formats.end()
      ? std::nullopt
      : std::optional<webrtc::SdpVideoFormat>{*found};
}

bool initializes_h264_encoder(webrtc::VideoEncoder &encoder,
                              core::ScreenStreamProfile profile) {
  const auto bounds = core::screen_stream_profile_bounds(profile);
  webrtc::VideoCodec codec;
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = static_cast<uint16_t>(bounds.max_width);
  codec.height = static_cast<uint16_t>(bounds.max_height);
  codec.startBitrate = 1'000;
  codec.maxBitrate = 20'000;
  codec.minBitrate = 100;
  codec.maxFramerate = static_cast<uint32_t>(bounds.max_frames_per_second);
  codec.active = true;
  codec.mode = webrtc::VideoCodecMode::kScreensharing;
  *codec.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
  const webrtc::VideoEncoder::Settings settings(
      webrtc::VideoEncoder::Capabilities(false), 1, 1'200);
  const auto result = encoder.InitEncode(&codec, settings);
  static_cast<void>(encoder.Release());
  return result == 0;
}

} // namespace

#if !defined(__APPLE__) && !defined(_WIN32)
bool probe_platform_h264_encoder(int, int, std::string &reason) {
  reason = "platform-unavailable";
  return false;
}

std::unique_ptr<webrtc::VideoEncoderFactory>
create_platform_h264_encoder_factory() {
  return nullptr;
}
#endif

VideoEncoderSelection select_screen_video_encoder(
    core::ScreenStreamProfile profile, PlatformH264Probe probe,
    PlatformH264Factory factory, std::string_view hardware_implementation) {
  const auto bounds = core::screen_stream_profile_bounds(profile);
  VideoEncoderSelection selection{
      .factory = nullptr,
      .diagnostics = {.requested_codec = "H264"},
      .max_width = bounds.max_width,
      .max_height = bounds.max_height,
      .capture_profile = profile};

  std::string probe_reason;
  const bool hardware_probe_passed =
      probe ? probe(bounds.max_width, bounds.max_height, probe_reason)
            : probe_platform_h264_encoder(bounds.max_width, bounds.max_height,
                                          probe_reason);
  if (hardware_probe_passed) {
    auto hardware_factory =
        factory ? factory() : create_platform_h264_encoder_factory();
    if (hardware_factory != nullptr) {
      auto screen_factory = std::make_unique<ScreenH264Factory>(
          std::move(hardware_factory), profile);
      const auto h264_format = supported_h264_format(*screen_factory);
      if (h264_format.has_value()) {
        auto encoder = screen_factory->Create(webrtc::CreateEnvironment(),
                                              *h264_format);
        if (encoder != nullptr && initializes_h264_encoder(*encoder, profile)) {
          selection.factory = std::move(screen_factory);
          selection.diagnostics.encoder_implementation =
              hardware_implementation.empty()
                  ? std::string(default_platform_h264_implementation())
                  : std::string(hardware_implementation);
          selection.diagnostics.negotiated_codec = "H264";
          selection.diagnostics.hardware_active = true;
          return selection;
        }
        probe_reason = encoder == nullptr
            ? "videotoolbox-encoder-unavailable"
            : "videotoolbox-encoder-initialization-failed";
      } else {
        probe_reason = "videotoolbox-h264-unsupported";
      }
    } else {
      probe_reason = "videotoolbox-factory-unavailable";
    }
  }

  selection.factory = create_vp8_factory();
  selection.max_width = std::min(selection.max_width, 1'920);
  selection.max_height = std::min(selection.max_height, 1'080);
  selection.capture_profile = core::ScreenStreamProfile::standard;
  selection.diagnostics.encoder_implementation = "VP8Template";
  selection.diagnostics.negotiated_codec = "VP8";
  selection.diagnostics.fallback_active = true;
  selection.diagnostics.fallback_reason =
      probe_reason.empty() ? "videotoolbox-unavailable" : std::move(probe_reason);
  return selection;
}

} // namespace shareme::rtc
