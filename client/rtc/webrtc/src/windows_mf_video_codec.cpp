#include "shareme/rtc/video_encoder_selection.hpp"

#include <memory>
#include <string>
#include <vector>

#include "api/environment/environment.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "windows_mf_h264_decoder.hpp"
#include "windows_mf_h264_encoder.hpp"
#include "windows/windows_h264_codecs.hpp"

namespace shareme::rtc {
namespace {

class WindowsMfH264EncoderFactory final
    : public webrtc::VideoEncoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {webrtc::SdpVideoFormat(
        "H264", {{"profile-level-id", "42e01f"},
                  {"level-asymmetry-allowed", "1"},
                  {"packetization-mode", "1"}})};
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment &,
      const webrtc::SdpVideoFormat &format) override {
    if (format.name != "H264")
      return nullptr;
    return create_windows_mf_h264_encoder();
  }
};

class WindowsMfH264DecoderFactory final
    : public webrtc::VideoDecoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::string reason;
    if (!probe_windows_mf_h264_decoder(3'840, 2'160, reason))
      return {};
    return {webrtc::SdpVideoFormat(
        "H264", {{"profile-level-id", "42e01f"},
                  {"level-asymmetry-allowed", "1"},
                  {"packetization-mode", "1"}})};
  }

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment &,
      const webrtc::SdpVideoFormat &format) override {
    std::string reason;
    return format.name == "H264" &&
                   probe_windows_mf_h264_decoder(3'840, 2'160, reason)
               ? create_windows_mf_h264_decoder()
               : nullptr;
  }
};

} // namespace

bool probe_platform_h264_encoder(int width, int height, std::string &reason) {
  return probe_windows_mf_h264_encoder(width, height, reason);
}

bool probe_platform_h264_codecs(int width, int height, int frames_per_second,
                                std::string &reason) {
  if (!probe_windows_media_foundation_h264_codecs(
          width, height, frames_per_second, reason)) {
    return false;
  }
  // The shared runtime decoder factory is profile-independent and advertises
  // H.264 only after its conservative cinema-bound readiness gate passes.
  return probe_windows_mf_h264_decoder(3'840, 2'160, reason);
}

std::unique_ptr<webrtc::VideoEncoderFactory>
create_platform_h264_encoder_factory() {
  return std::make_unique<WindowsMfH264EncoderFactory>();
}

std::unique_ptr<webrtc::VideoDecoderFactory>
create_platform_video_decoder_factory() {
  return std::make_unique<WindowsMfH264DecoderFactory>();
}

} // namespace shareme::rtc
