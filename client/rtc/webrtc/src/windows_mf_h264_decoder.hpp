#pragma once

#include <memory>
#include <string>

#include "api/video_codecs/video_decoder.h"

namespace shareme::rtc {

[[nodiscard]] bool probe_windows_mf_h264_decoder(int width, int height,
                                                  std::string &reason);

[[nodiscard]] std::unique_ptr<webrtc::VideoDecoder>
create_windows_mf_h264_decoder();

} // namespace shareme::rtc
