#pragma once

#include <string>

namespace shareme::rtc {

// Returns success only when the hardware encoder and a native MF decoder can
// both configure the requested even geometry.
[[nodiscard]] bool probe_windows_media_foundation_h264_codecs(
    int width, int height, int frames_per_second, std::string &reason);

} // namespace shareme::rtc
