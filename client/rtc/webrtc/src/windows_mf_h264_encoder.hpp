#pragma once

#include <memory>
#include <string>

#include "api/video_codecs/video_encoder.h"

namespace shareme::rtc {

[[nodiscard]] bool probe_windows_mf_h264_encoder(int width, int height,
                                                 std::string &reason);
[[nodiscard]] std::unique_ptr<webrtc::VideoEncoder>
create_windows_mf_h264_encoder();

} // namespace shareme::rtc
