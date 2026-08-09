#include "shareme/rtc/video_encoder_selection.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_frame.h"
#include "webrtc_runtime.hpp"
#include "windows/windows_h264_codecs.hpp"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

class EncodedCallback final : public webrtc::EncodedImageCallback {
 public:
  Result OnEncodedImage(const webrtc::EncodedImage &image,
                        const webrtc::CodecSpecificInfo *) override {
    images.push_back(image);
    annex_b = image.size() >= 4 && image.data()[0] == 0 &&
              image.data()[1] == 0 && image.data()[2] == 0 &&
              image.data()[3] == 1;
    if (image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey)
      ++keyframes;
    return Result(Result::OK);
  }
  void OnFrameDropped(uint32_t, int, bool) override {}

  std::vector<webrtc::EncodedImage> images;
  bool annex_b{false};
  int keyframes{0};
};

class DecodedCallback final : public webrtc::DecodedImageCallback {
 public:
  int32_t Decoded(webrtc::VideoFrame &frame) override {
    ++count;
    width = frame.width();
    height = frame.height();
    rtp_timestamp = frame.rtp_timestamp();
    return 0;
  }

  int count{0};
  int width{0};
  int height{0};
  std::uint32_t rtp_timestamp{0};
};

void selects_hardware_media_foundation() {
  std::string reason;
  REQUIRE(shareme::rtc::probe_platform_h264_encoder(1'920, 1'080, reason));
  REQUIRE(reason.empty());
  const auto selection = shareme::rtc::select_screen_video_encoder(
      shareme::core::ScreenStreamProfile::standard);
  REQUIRE(selection.factory != nullptr);
  REQUIRE(selection.diagnostics.hardware_active);
  REQUIRE(selection.diagnostics.negotiated_codec == "H264");
  REQUIRE(selection.diagnostics.encoder_implementation == "MediaFoundation");
}

void probe_requires_encoder_and_decoder_initialization() {
  std::string reason;
  REQUIRE(shareme::rtc::probe_windows_media_foundation_h264_codecs(
      1'920, 1'080, 60, reason));
  REQUIRE(reason.empty());
}

void encodes_and_decodes_real_h264_frames() {
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

  EncodedCallback encoded_callback;
  REQUIRE(encoder->RegisterEncodeCompleteCallback(&encoded_callback) == 0);
  webrtc::VideoBitrateAllocation allocation;
  REQUIRE(allocation.SetBitrate(0, 0, 4'000'000));
  encoder->SetRates(
      webrtc::VideoEncoder::RateControlParameters(allocation, 30.0));
  auto buffer = webrtc::I420Buffer::Create(codec.width, codec.height);
  buffer->InitializeData();
  for (std::uint32_t sequence = 0; sequence < 20; ++sequence) {
    const std::vector<webrtc::VideoFrameType> frame_types = {
        sequence == 0 || sequence == 10 ? webrtc::VideoFrameType::kVideoFrameKey
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
  REQUIRE(encoded_callback.images.size() == 20);
  REQUIRE(encoded_callback.annex_b);
  REQUIRE(encoded_callback.keyframes >= 2);
  REQUIRE(encoder->Release() == 0);
  REQUIRE(encoder->Release() == 0);

  auto decoder_factory =
      shareme::rtc::create_platform_video_decoder_factory();
  REQUIRE(decoder_factory != nullptr);
  auto decoder = decoder_factory->Create(
      webrtc::CreateEnvironment(),
      decoder_factory->GetSupportedFormats().front());
  REQUIRE(decoder != nullptr);
  DecodedCallback decoded_callback;
  REQUIRE(decoder->RegisterDecodeCompleteCallback(&decoded_callback) == 0);
  webrtc::VideoDecoder::Settings decoder_settings;
  decoder_settings.set_codec_type(webrtc::kVideoCodecH264);
  REQUIRE(decoder->Configure(decoder_settings));
  for (std::size_t index = 0; index < encoded_callback.images.size(); ++index) {
    const auto result = decoder->Decode(encoded_callback.images[index], 0);
    if (result != 0) {
      std::cerr << "Media Foundation decode failed for access unit " << index
                << " with code " << result << '\n';
    }
    REQUIRE(result == 0);
  }
  REQUIRE(decoded_callback.count > 0);
  REQUIRE(decoded_callback.width == codec.width);
  REQUIRE(decoded_callback.height == codec.height);
  REQUIRE(decoded_callback.rtp_timestamp ==
          encoded_callback.images.back().RtpTimestamp());
  const std::uint8_t truncated_access_unit[] = {0, 0, 0, 1, 0x65};
  webrtc::EncodedImage malformed;
  malformed.SetEncodedData(webrtc::EncodedImageBuffer::Create(
      truncated_access_unit, sizeof(truncated_access_unit)));
  malformed.SetRtpTimestamp(30'000);
  const int decoded_before_corruption = decoded_callback.count;
  REQUIRE(decoder->Decode(malformed, 0) == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE(decoded_callback.count == decoded_before_corruption);
  REQUIRE(decoder->Release() == 0);
  REQUIRE(decoder->Release() == 0);
}

} // namespace

int main() {
  selects_hardware_media_foundation();
  probe_requires_encoder_and_decoder_initialization();
  encodes_and_decodes_real_h264_frames();
  return EXIT_SUCCESS;
}
