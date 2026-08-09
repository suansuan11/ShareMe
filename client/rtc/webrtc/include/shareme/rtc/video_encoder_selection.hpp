#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "api/video_codecs/video_encoder_factory.h"
#include "shareme/core/screen_stream_profile.hpp"

namespace shareme::rtc {

struct VideoEncoderDiagnostics {
  std::string requested_codec;
  std::string negotiated_codec;
  std::string encoder_implementation;
  bool hardware_active{false};
  bool fallback_active{false};
  std::string fallback_reason;
};

struct VideoEncoderSelection {
  std::unique_ptr<webrtc::VideoEncoderFactory> factory;
  VideoEncoderDiagnostics diagnostics;
  int max_width{0};
  int max_height{0};
  core::ScreenStreamProfile capture_profile{
      core::ScreenStreamProfile::standard};
};

using PlatformH264Probe =
    std::function<bool(int width, int height, std::string &reason)>;
using PlatformH264Factory =
    std::function<std::unique_ptr<webrtc::VideoEncoderFactory>()>;

[[nodiscard]] VideoEncoderSelection select_screen_video_encoder(
    core::ScreenStreamProfile profile, PlatformH264Probe probe = {},
    PlatformH264Factory factory = {},
    std::string_view hardware_implementation = {});

[[nodiscard]] bool probe_platform_h264_encoder(
    int width, int height, std::string &reason);
[[nodiscard]] std::unique_ptr<webrtc::VideoEncoderFactory>
create_platform_h264_encoder_factory();

} // namespace shareme::rtc
