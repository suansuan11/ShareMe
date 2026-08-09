#include "shareme/rtc/video_encoder_selection.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "webrtc_runtime.hpp"
#include "windows/windows_h264_codecs.hpp"
#include "windows/h264_bitstream.hpp"

namespace {

struct Options { std::string profile{"standard"}; int frames{600}; bool diagnostic_extra_pump{}; std::filesystem::path artifact; };

bool parse(int argc, char **argv, Options &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string value = argv[index];
    if (value == "--profile" && index + 1 < argc) options.profile = argv[++index];
    else if (value == "--frames" && index + 1 < argc) options.frames = std::atoi(argv[++index]);
    else if (value == "--diagnostic-extra-pump") options.diagnostic_extra_pump = true;
    else if (value == "--artifact" && index + 1 < argc) options.artifact = argv[++index];
    else return false;
  }
  return options.frames > 0 && !options.artifact.empty() &&
         (options.profile == "standard" || options.profile == "quality" || options.profile == "cinema");
}

std::string json_escape(const std::string &value) {
  std::string escaped;
  for (const char character : value) {
    if (character == '\\' || character == '"') escaped += '\\';
    if (static_cast<unsigned char>(character) < 0x20) { escaped += '?'; continue; }
    escaped += character;
  }
  return escaped;
}

class Encoded final : public webrtc::EncodedImageCallback {
 public:
  Result OnEncodedImage(const webrtc::EncodedImage &image,
                        const webrtc::CodecSpecificInfo *) override {
    if (released) {
      ++after_release;
    } else {
      images.push_back(image);
      ++count;
      if (image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey) {
        ++idr;
        const auto info = shareme::rtc::inspect_annex_b(
            std::span<const std::uint8_t>(image.data(), image.size()));
        if (info.has_value() && info.has_sps && info.has_pps && info.has_idr)
          ++sps_pps_idr;
      }
    }
    return Result(Result::OK);
  }
  void OnFrameDropped(uint32_t, int, bool) override { ++dropped; }
  std::vector<webrtc::EncodedImage> images; int count{}; int idr{}; int sps_pps_idr{}; int dropped{}; int after_release{}; bool released{};
};

std::uint8_t expected_y(int x, int y, std::uint32_t sequence, int width,
                        int height) {
  const int block_x = static_cast<int>((sequence * 11U) % (width - 160));
  const int block_y = static_cast<int>((sequence * 7U) % (height - 96));
  return x >= block_x && x < block_x + 160 && y >= block_y && y < block_y + 96
             ? 210 : 80;
}

std::uint8_t expected_u(int x, int y, std::uint32_t sequence, int width,
                        int height) {
  const int block_x = static_cast<int>((sequence * 11U) % (width - 160)) / 2;
  const int block_y = static_cast<int>((sequence * 7U) % (height - 96)) / 2;
  return x >= block_x && x < block_x + 80 && y >= block_y && y < block_y + 48
             ? 92 : 128;
}

std::uint8_t expected_v(int x, int y, std::uint32_t sequence, int width,
                        int height) {
  const int block_x = static_cast<int>((sequence * 11U) % (width - 160)) / 2;
  const int block_y = static_cast<int>((sequence * 7U) % (height - 96)) / 2;
  return x >= block_x && x < block_x + 80 && y >= block_y && y < block_y + 48
             ? 172 : 128;
}

void fill_pattern(webrtc::I420Buffer &buffer, std::uint32_t sequence) {
  for (int y = 0; y < buffer.height(); ++y)
    for (int x = 0; x < buffer.width(); ++x)
      buffer.MutableDataY()[y * buffer.StrideY() + x] =
          expected_y(x, y, sequence, buffer.width(), buffer.height());
  for (int y = 0; y < buffer.height() / 2; ++y)
    for (int x = 0; x < buffer.width() / 2; ++x) {
      buffer.MutableDataU()[y * buffer.StrideU() + x] =
          expected_u(x, y, sequence, buffer.width(), buffer.height());
      buffer.MutableDataV()[y * buffer.StrideV() + x] =
          expected_v(x, y, sequence, buffer.width(), buffer.height());
    }
}

class DecodedCallback final : public webrtc::DecodedImageCallback {
 public:
  DecodedCallback(int value_width, int value_height, int value_fps)
      : expected_width(value_width), expected_height(value_height), fps(value_fps) {}
  int32_t Decoded(webrtc::VideoFrame &frame) override {
    if (released) { ++after_release; return 0; }
    ++count; width = frame.width(); height = frame.height();
    if (has_timestamp && frame.rtp_timestamp() <= last_timestamp)
      timestamps_monotonic = false;
    has_timestamp = true;
    last_timestamp = frame.rtp_timestamp();
    const auto image = frame.video_frame_buffer()->ToI420();
    const auto timestamp = frame.rtp_timestamp();
    const auto sequence = timestamp / (90'000U / fps);
    if (timestamp % (90'000U / fps) != 0 || !seen_timestamps.insert(timestamp).second)
      timestamps_monotonic = false;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) observe(image->DataY()[y * image->StrideY() + x], expected_y(x, y, sequence, expected_width, expected_height), squared_y, samples_y, true);
    for (int y = 0; y < height / 2; ++y) for (int x = 0; x < width / 2; ++x) { observe(image->DataU()[y * image->StrideU() + x], expected_u(x, y, sequence, expected_width, expected_height), squared_u, samples_u, false); observe(image->DataV()[y * image->StrideV() + x], expected_v(x, y, sequence, expected_width, expected_height), squared_v, samples_v, false); }
    return 0;
  }
  void observe(double actual, double expected, double &squared, std::uint64_t &samples, bool luma) { const auto delta = actual - expected; squared += delta * delta; ++samples; if (luma) { sum_actual += actual; sum_expected += expected; sum_actual2 += actual * actual; sum_expected2 += expected * expected; sum_cross += actual * expected; } }
  double psnr(double squared, std::uint64_t samples) const { return samples == 0 || squared == 0 ? 99.0 : 10.0 * std::log10((255.0 * 255.0) / (squared / samples)); }
  double ssim_y() const { const double n = static_cast<double>(samples_y); if (n == 0) return 0; const double mx=sum_actual/n,my=sum_expected/n; const double vx=sum_actual2/n-mx*mx,vy=sum_expected2/n-my*my,cov=sum_cross/n-mx*my; return ((2*mx*my+6.5025)*(2*cov+58.5225))/((mx*mx+my*my+6.5025)*(vx+vy+58.5225)); }
  int expected_width{}; int expected_height{}; int fps{}; int count{}; int width{}; int height{}; int after_release{}; bool released{}; bool has_timestamp{}; bool timestamps_monotonic{true}; uint32_t last_timestamp{}; std::unordered_set<uint32_t> seen_timestamps; double squared_y{}; double squared_u{}; double squared_v{}; std::uint64_t samples_y{}; std::uint64_t samples_u{}; std::uint64_t samples_v{}; double sum_actual{}; double sum_expected{}; double sum_actual2{}; double sum_expected2{}; double sum_cross{};
};

bool write_artifact(const Options &options, bool verified, const std::string &reason,
                    int submitted, int extra_pump_accepted, int callbacks_before_release,
                    const Encoded &encoded, const DecodedCallback &decoded,
                    int width, int height, int frames_per_second) {
  try {
  std::filesystem::create_directories(options.artifact.parent_path());
  const auto temporary = options.artifact.string() + ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output << "{\n  \"schema\": \"windows-h264-probe-v1\",\n"
         << "  \"status\": \"" << (verified ? "verified" : "blocked") << "\",\n"
         << "  \"profile\": \"" << options.profile << "\",\n"
         << "  \"implementation\": \"MediaFoundation\",\n  \"hardware\": true,\n"
         << "  \"reason\": \"" << json_escape(reason) << "\",\n"
         << "  \"framesSubmitted\": " << submitted << ",\n  \"framesEncoded\": " << encoded.count
         << ",\n  \"framesDecoded\": " << decoded.count << ",\n"
         << "  \"diagnosticExtraPumpAccepted\": " << extra_pump_accepted << ",\n"
         << "  \"callbacksBeforeRelease\": " << callbacks_before_release << ",\n"
         << "  \"decodedGeometryExact\": " << ((decoded.width == width && decoded.height == height) ? "true" : "false") << ",\n"
         << "  \"matchedQualityFrames\": " << decoded.count << ",\n"
         << "  \"psnrY\": " << decoded.psnr(decoded.squared_y, decoded.samples_y) << ",\n"
         << "  \"psnrU\": " << decoded.psnr(decoded.squared_u, decoded.samples_u) << ",\n"
         << "  \"psnrV\": " << decoded.psnr(decoded.squared_v, decoded.samples_v) << ",\n"
         << "  \"ssimY\": " << decoded.ssim_y() << ",\n"
         << "  \"idrFrames\": " << encoded.idr << ",\n"
         << "  \"spsPpsIdrFrames\": " << encoded.sps_pps_idr << ",\n"
         << "  \"timestampsMonotonic\": " << (decoded.timestamps_monotonic && submitted > 0 && decoded.last_timestamp == static_cast<uint32_t>((submitted - 1) * (90'000 / frames_per_second)) ? "true" : "false") << ",\n"
         << "  \"encoderCallbacksAfterRelease\": " << encoded.after_release << ",\n  \"decoderCallbacksAfterRelease\": " << decoded.after_release << "\n}\n";
  output.flush();
  if (!output) return false;
  output.close();
  if (!output) return false;
  std::filesystem::rename(temporary, options.artifact);
  return true;
  } catch (const std::filesystem::filesystem_error &) { return false; }
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse(argc, argv, options)) return EXIT_FAILURE;
  const int width = options.profile == "standard" ? 1920 :
                    options.profile == "quality" ? 2560 : 3840;
  const int height = options.profile == "standard" ? 1080 :
                     options.profile == "quality" ? 1440 : 2160;
  const int frames_per_second = options.profile == "cinema" ? 30 : 60;
  std::string reason;
  if (!shareme::rtc::probe_windows_media_foundation_h264_codecs(width, height, frames_per_second, reason)) {
    Encoded encoded; DecodedCallback decoded(width, height, frames_per_second); static_cast<void>(write_artifact(options, false, reason, 0, 0, 0, encoded, decoded, width, height, frames_per_second)); return EXIT_FAILURE;
  }
  auto encoder_factory = shareme::rtc::create_platform_h264_encoder_factory();
  auto decoder_factory = shareme::rtc::create_platform_video_decoder_factory();
  auto encoder = encoder_factory->Create(webrtc::CreateEnvironment(), encoder_factory->GetSupportedFormats().front());
  auto decoder = decoder_factory->Create(webrtc::CreateEnvironment(), decoder_factory->GetSupportedFormats().front());
  webrtc::VideoCodec codec; codec.codecType = webrtc::kVideoCodecH264; codec.width = width; codec.height = height; codec.startBitrate = options.profile == "standard" ? 8000 : options.profile == "quality" ? 16000 : 25000; codec.maxBitrate = codec.startBitrate * 2; codec.maxFramerate = frames_per_second; codec.active = true; *codec.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
  Encoded encoded; DecodedCallback decoded(width, height, frames_per_second);
  const webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1200);
  webrtc::VideoDecoder::Settings decoder_settings; decoder_settings.set_codec_type(webrtc::kVideoCodecH264);
  bool ok = encoder && decoder && encoder->InitEncode(&codec, settings) == 0 && encoder->RegisterEncodeCompleteCallback(&encoded) == 0 && decoder->RegisterDecodeCompleteCallback(&decoded) == 0 && decoder->Configure(decoder_settings);
  auto buffer = webrtc::I420Buffer::Create(width, height); buffer->InitializeData();
  int submitted = 0;
  for (; ok && submitted < options.frames; ++submitted) {
    fill_pattern(*buffer, submitted);
    const auto frame = webrtc::VideoFrame::Builder().set_video_frame_buffer(buffer).set_timestamp_us(submitted * (1'000'000LL / frames_per_second)).set_rtp_timestamp(submitted * (90'000U / frames_per_second)).set_rotation(webrtc::kVideoRotation_0).build();
    const std::vector<webrtc::VideoFrameType> types = {submitted == 0 ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta};
    ok = encoder->Encode(frame, &types) == 0;
    for (std::size_t index = 0; ok && index < encoded.images.size(); ++index) ok = decoder->Decode(encoded.images[index], 0) == 0;
    encoded.images.clear(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  int extra_pump_accepted = 0;
  if (ok && options.diagnostic_extra_pump) {
    const auto frame = webrtc::VideoFrame::Builder().set_video_frame_buffer(buffer).set_timestamp_us(submitted * (1'000'000LL / frames_per_second)).set_rtp_timestamp(submitted * (90'000U / frames_per_second)).set_rotation(webrtc::kVideoRotation_0).build();
    const std::vector<webrtc::VideoFrameType> types = {webrtc::VideoFrameType::kVideoFrameDelta};
    if (encoder->Encode(frame, &types) == 0) {
      extra_pump_accepted = 1;
      for (std::size_t index = 0; ok && index < encoded.images.size(); ++index) ok = decoder->Decode(encoded.images[index], 0) == 0;
      encoded.images.clear();
    }
  }
  const int callbacks_before_release = encoded.count;
  encoded.released = true; decoded.released = true;
  if (encoder) encoder->Release(); if (decoder) decoder->Release();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const bool verified = ok && submitted == options.frames && encoded.count == options.frames && decoded.count == options.frames && encoded.idr > 0 && encoded.sps_pps_idr > 0 && decoded.timestamps_monotonic && decoded.width == width && decoded.height == height && decoded.psnr(decoded.squared_y, decoded.samples_y) >= 35.0 && decoded.psnr(decoded.squared_u, decoded.samples_u) >= 32.0 && decoded.psnr(decoded.squared_v, decoded.samples_v) >= 32.0 && decoded.ssim_y() >= 0.95 && encoded.after_release == 0 && decoded.after_release == 0;
  const bool artifact_written = write_artifact(options, verified, verified ? "" : "mf-h264-roundtrip-failed", submitted, extra_pump_accepted, callbacks_before_release, encoded, decoded, width, height, frames_per_second);
  return verified && artifact_written ? EXIT_SUCCESS : EXIT_FAILURE;
}
