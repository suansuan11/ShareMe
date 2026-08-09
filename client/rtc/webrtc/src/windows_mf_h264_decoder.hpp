#pragma once

#include <memory>

#include "api/video_codecs/video_decoder.h"

namespace shareme::rtc {

[[nodiscard]] std::unique_ptr<webrtc::VideoDecoder>
create_windows_mf_h264_decoder();

} // namespace shareme::rtc
