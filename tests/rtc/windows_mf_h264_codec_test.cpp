#include "shareme/rtc/video_encoder_selection.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_frame.h"
#include "webrtc_runtime.hpp"
#include "windows/mf_codec_thread.hpp"
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

// Release is intentionally called by the callback, while the adapter owns its
// operation lock.  The adapter must defer teardown until the callback returns.
class ReentrantEncodedCallback final : public webrtc::EncodedImageCallback {
 public:
  Result OnEncodedImage(const webrtc::EncodedImage &image,
                        const webrtc::CodecSpecificInfo *) override {
    images.push_back(image);
    REQUIRE(encoder != nullptr);
    release_result = encoder->Release();
    released_from_callback = true;
    return Result(Result::OK);
  }
  void OnFrameDropped(uint32_t, int, bool) override {}

  webrtc::VideoEncoder *encoder{nullptr};
  std::vector<webrtc::EncodedImage> images;
  int32_t release_result{-1};
  bool released_from_callback{false};
};

class ReentrantDecodedCallback final : public webrtc::DecodedImageCallback {
 public:
  int32_t Decoded(webrtc::VideoFrame &) override {
    ++count;
    REQUIRE(decoder != nullptr);
    release_result = decoder->Release();
    released_from_callback = true;
    return 0;
  }

  webrtc::VideoDecoder *decoder{nullptr};
  int count{0};
  int32_t release_result{-1};
  bool released_from_callback{false};
};

class BlockingEncodedCallback final : public webrtc::EncodedImageCallback {
 public:
  Result OnEncodedImage(const webrtc::EncodedImage &,
                        const webrtc::CodecSpecificInfo *) override {
    entered.store(true, std::memory_order_release);
    while (!allow_return.load(std::memory_order_acquire))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++count;
    return Result(Result::OK);
  }
  void OnFrameDropped(uint32_t, int, bool) override {}

  std::atomic<bool> entered{false};
  std::atomic<bool> allow_return{false};
  std::atomic<int> count{0};
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

void codec_thread_propagates_failures_and_survives_self_destruction() {
  shareme::rtc::MfCodecThread thread;
  bool threw = false;
  try {
    static_cast<void>(thread.invoke([]() -> int {
      throw std::runtime_error("expected-task-failure");
    }));
  } catch (const std::runtime_error &) {
    threw = true;
  }
  REQUIRE(threw);

  auto self_destroying = std::make_shared<shareme::rtc::MfCodecThread>();
  std::promise<void> destroyed;
  auto destroyed_future = destroyed.get_future();
  self_destroying->invoke([&] {
    self_destroying.reset();
    destroyed.set_value();
  });
  REQUIRE(destroyed_future.wait_for(std::chrono::seconds(1)) ==
          std::future_status::ready);
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
  REQUIRE(decoder->Decode(malformed, 0) != 0);
  REQUIRE(decoder->Decode(encoded_callback.images[1], 0) != 0);
  REQUIRE(decoder->Decode(encoded_callback.images[0], 0) == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE(decoded_callback.count > decoded_before_corruption);
  REQUIRE(decoder->Release() == 0);
  REQUIRE(decoder->Release() == 0);
}

void callback_reentrant_release_defers_teardown() {
  auto encoder_factory = shareme::rtc::create_platform_h264_encoder_factory();
  REQUIRE(encoder_factory != nullptr);
  auto encoder = encoder_factory->Create(webrtc::CreateEnvironment(),
                                         encoder_factory->GetSupportedFormats().front());
  REQUIRE(encoder != nullptr);

  webrtc::VideoCodec codec;
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = 640;
  codec.height = 360;
  codec.startBitrate = 2'000;
  codec.maxBitrate = 4'000;
  codec.minBitrate = 100;
  codec.maxFramerate = 30;
  codec.active = true;
  *codec.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
  const webrtc::VideoEncoder::Settings settings(
      webrtc::VideoEncoder::Capabilities(false), 1, 1'200);
  REQUIRE(encoder->InitEncode(&codec, settings) == 0);

  ReentrantEncodedCallback encoded_callback;
  encoded_callback.encoder = encoder.get();
  REQUIRE(encoder->RegisterEncodeCompleteCallback(&encoded_callback) == 0);
  auto input = webrtc::I420Buffer::Create(codec.width, codec.height);
  input->InitializeData();
  const auto frame = webrtc::VideoFrame::Builder()
                         .set_video_frame_buffer(input)
                         .set_timestamp_us(0)
                         .set_rtp_timestamp(7'000)
                         .set_rotation(webrtc::kVideoRotation_0)
                         .build();
  const std::vector<webrtc::VideoFrameType> keyframe = {
      webrtc::VideoFrameType::kVideoFrameKey};
  REQUIRE(encoder->Encode(frame, &keyframe) == 0);
  REQUIRE(encoded_callback.released_from_callback);
  REQUIRE(encoded_callback.release_result == 0);
  REQUIRE(encoded_callback.images.size() == 1);
  REQUIRE(encoder->Release() == 0);

  auto decoder_factory = shareme::rtc::create_platform_video_decoder_factory();
  REQUIRE(decoder_factory != nullptr);
  auto decoder = decoder_factory->Create(webrtc::CreateEnvironment(),
                                         decoder_factory->GetSupportedFormats().front());
  REQUIRE(decoder != nullptr);
  ReentrantDecodedCallback decoded_callback;
  decoded_callback.decoder = decoder.get();
  REQUIRE(decoder->RegisterDecodeCompleteCallback(&decoded_callback) == 0);
  webrtc::VideoDecoder::Settings decoder_settings;
  decoder_settings.set_codec_type(webrtc::kVideoCodecH264);
  REQUIRE(decoder->Configure(decoder_settings));
  REQUIRE(decoder->Decode(encoded_callback.images.front(), 0) == 0);
  REQUIRE(decoded_callback.released_from_callback);
  REQUIRE(decoded_callback.release_result == 0);
  REQUIRE(decoded_callback.count == 1);
  REQUIRE(decoder->Release() == 0);
}

void concurrent_release_waits_for_active_callback() {
  auto factory = shareme::rtc::create_platform_h264_encoder_factory();
  REQUIRE(factory != nullptr);
  auto encoder = factory->Create(webrtc::CreateEnvironment(),
                                 factory->GetSupportedFormats().front());
  REQUIRE(encoder != nullptr);
  webrtc::VideoCodec codec;
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = 640;
  codec.height = 360;
  codec.startBitrate = 2'000;
  codec.maxFramerate = 30;
  codec.active = true;
  *codec.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
  REQUIRE(encoder->InitEncode(
              &codec, webrtc::VideoEncoder::Settings(
                          webrtc::VideoEncoder::Capabilities(false), 1,
                          1'200)) == 0);
  BlockingEncodedCallback callback;
  REQUIRE(encoder->RegisterEncodeCompleteCallback(&callback) == 0);
  auto input = webrtc::I420Buffer::Create(codec.width, codec.height);
  input->InitializeData();
  const auto frame = webrtc::VideoFrame::Builder()
                         .set_video_frame_buffer(input)
                         .set_timestamp_us(0)
                         .set_rtp_timestamp(8'000)
                         .set_rotation(webrtc::kVideoRotation_0)
                         .build();
  const std::vector<webrtc::VideoFrameType> keyframe = {
      webrtc::VideoFrameType::kVideoFrameKey};
  std::atomic<int32_t> encode_result{-1};
  std::thread encoding([&] {
    encode_result.store(encoder->Encode(frame, &keyframe),
                        std::memory_order_release);
  });
  for (int attempt = 0; attempt < 100 &&
                        !callback.entered.load(std::memory_order_acquire);
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(callback.entered.load(std::memory_order_acquire));
  std::atomic<int32_t> release_result{-1};
  std::thread releasing([&] {
    release_result.store(encoder->Release(), std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  REQUIRE(release_result.load(std::memory_order_acquire) == -1);
  callback.allow_return.store(true, std::memory_order_release);
  encoding.join();
  releasing.join();
  REQUIRE(encode_result.load(std::memory_order_acquire) == 0);
  REQUIRE(release_result.load(std::memory_order_acquire) == 0);
  REQUIRE(callback.count.load(std::memory_order_acquire) == 1);
  REQUIRE(encoder->Release() == 0);
}

} // namespace

int main() {
  selects_hardware_media_foundation();
  codec_thread_propagates_failures_and_survives_self_destruction();
  probe_requires_encoder_and_decoder_initialization();
  encodes_and_decodes_real_h264_frames();
  callback_reentrant_release_defers_teardown();
  concurrent_release_waits_for_active_callback();
  return EXIT_SUCCESS;
}
