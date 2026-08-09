#include "windows/windows_h264_codecs.hpp"

#include "windows_mf_h264_decoder.hpp"
#include "windows_mf_h264_encoder.hpp"

namespace shareme::rtc {

bool probe_windows_media_foundation_h264_codecs(int width, int height,
                                                int frames_per_second,
                                                std::string &reason) {
  if (frames_per_second <= 0) {
    reason = "mf-h264-invalid-frame-rate";
    return false;
  }
  if (!probe_windows_mf_h264_encoder(width, height, reason))
    return false;
  if (!probe_windows_mf_h264_decoder(width, height, reason))
    return false;
  reason.clear();
  return true;
}

} // namespace shareme::rtc
