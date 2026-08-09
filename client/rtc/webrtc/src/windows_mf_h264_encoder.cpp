#include "windows_mf_h264_encoder.hpp"

#include <Windows.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "api/video/video_frame.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "windows_mf_h264_buffers.hpp"

namespace shareme::rtc {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kInputStream = 0;
constexpr DWORD kOutputStream = 0;
constexpr std::size_t kMaxPendingFrames = 8;

class MfScope final {
 public:
  MfScope() {
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_initialized_ = result == S_OK || result == S_FALSE;
    media_foundation_initialized_ =
        SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
  }

  ~MfScope() {
    if (media_foundation_initialized_)
      MFShutdown();
    if (com_initialized_)
      CoUninitialize();
  }

  [[nodiscard]] bool ready() const noexcept {
    return media_foundation_initialized_;
  }

 private:
  bool com_initialized_{false};
  bool media_foundation_initialized_{false};
};

std::vector<ComPtr<IMFActivate>> enumerate_hardware_encoders() {
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
  IMFActivate **raw_activations = nullptr;
  UINT32 count = 0;
  const auto result = MFTEnumEx(
      MFT_CATEGORY_VIDEO_ENCODER,
      MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &input, &output,
      &raw_activations, &count);
  if (FAILED(result))
    return {};

  std::vector<ComPtr<IMFActivate>> activations;
  activations.reserve(count);
  for (UINT32 index = 0; index < count; ++index) {
    ComPtr<IMFActivate> activation;
    activation.Attach(raw_activations[index]);
    activations.push_back(std::move(activation));
  }
  CoTaskMemFree(raw_activations);
  return activations;
}

HRESULT set_video_type(IMFMediaType *type, const GUID &subtype, int width,
                       int height, int frames_per_second, int bitrate) {
  if (FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(type->SetGUID(MF_MT_SUBTYPE, subtype)) ||
      FAILED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE,
                                static_cast<UINT32>(width),
                                static_cast<UINT32>(height))) ||
      FAILED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE,
                                 static_cast<UINT32>(frames_per_second), 1)) ||
      FAILED(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
      FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE,
                             MFVideoInterlace_Progressive))) {
    return E_FAIL;
  }
  if (subtype == MFVideoFormat_H264 && bitrate > 0 &&
      FAILED(type->SetUINT32(MF_MT_AVG_BITRATE,
                             static_cast<UINT32>(bitrate)))) {
    return E_FAIL;
  }
  return S_OK;
}

bool configure_transform(IMFTransform *transform, int width, int height,
                         int frames_per_second, int bitrate, bool &asynchronous,
                         ComPtr<IMFMediaEventGenerator> &event_generator) {
  ComPtr<IMFAttributes> attributes;
  UINT32 async_value = FALSE;
  if (SUCCEEDED(transform->GetAttributes(&attributes)) && attributes != nullptr) {
    static_cast<void>(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &async_value));
    if (async_value != FALSE &&
        FAILED(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE))) {
      return false;
    }
    static_cast<void>(attributes->SetUINT32(MF_LOW_LATENCY, TRUE));
  }
  asynchronous = async_value != FALSE;
  if (asynchronous && FAILED(transform->QueryInterface(IID_PPV_ARGS(
                          event_generator.ReleaseAndGetAddressOf())))) {
    return false;
  }

  ComPtr<IMFMediaType> output_type;
  ComPtr<IMFMediaType> input_type;
  if (FAILED(MFCreateMediaType(&output_type)) ||
      FAILED(set_video_type(output_type.Get(), MFVideoFormat_H264, width,
                            height, frames_per_second, bitrate)) ||
      FAILED(transform->SetOutputType(kOutputStream, output_type.Get(), 0)) ||
      FAILED(MFCreateMediaType(&input_type)) ||
      FAILED(set_video_type(input_type.Get(), MFVideoFormat_NV12, width, height,
                            frames_per_second, 0)) ||
      FAILED(input_type->SetUINT32(MF_MT_DEFAULT_STRIDE,
                                   static_cast<UINT32>(width))) ||
      FAILED(transform->SetInputType(kInputStream, input_type.Get(), 0))) {
    return false;
  }

  ComPtr<ICodecAPI> codec_api;
  if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&codec_api)))) {
    VARIANT low_latency;
    VariantInit(&low_latency);
    low_latency.vt = VT_BOOL;
    low_latency.boolVal = VARIANT_TRUE;
    static_cast<void>(
        codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &low_latency));
    VariantClear(&low_latency);
  }

  return SUCCEEDED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
                                             0)) &&
         SUCCEEDED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
                                             0));
}

struct PendingFrame {
  std::uint32_t rtp_timestamp{0};
  std::int64_t ntp_time_ms{0};
  std::int64_t capture_time_ms{0};
  std::optional<webrtc::ColorSpace> color_space;
};

class WindowsMfH264Encoder final : public webrtc::VideoEncoder {
 public:
  ~WindowsMfH264Encoder() override { static_cast<void>(Release()); }

  int InitEncode(const webrtc::VideoCodec *codec,
                 const Settings &) override {
    static_cast<void>(Release());
    if (codec == nullptr || codec->codecType != webrtc::kVideoCodecH264 ||
        codec->width == 0 || codec->height == 0 || (codec->width & 1U) != 0 ||
        (codec->height & 1U) != 0) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    scope_ = std::make_unique<MfScope>();
    if (!scope_->ready()) {
      scope_.reset();
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    width_ = codec->width;
    height_ = codec->height;
    frames_per_second_ = std::max<int>(1, codec->maxFramerate);
    bitrate_ = std::max<int>(100'000, codec->startBitrate * 1'000);
    const auto activations = enumerate_hardware_encoders();
    for (const auto &activation : activations) {
      ComPtr<IMFTransform> candidate;
      if (FAILED(activation->ActivateObject(IID_PPV_ARGS(&candidate))))
        continue;
      bool asynchronous = false;
      ComPtr<IMFMediaEventGenerator> event_generator;
      if (!configure_transform(candidate.Get(), width_, height_,
                               frames_per_second_, bitrate_, asynchronous,
                               event_generator)) {
        static_cast<void>(activation->ShutdownObject());
        continue;
      }
      transform_ = std::move(candidate);
      activation_ = activation;
      event_generator_ = std::move(event_generator);
      asynchronous_ = asynchronous;
      input_requested_ = !asynchronous_;
      initialized_ = true;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    static_cast<void>(Release());
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback *callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    callback_ = nullptr;
    pending_frames_.clear();
    if (transform_ != nullptr) {
      static_cast<void>(
          transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
      static_cast<void>(
          transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
      static_cast<void>(
          transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0));
    }
    event_generator_.Reset();
    transform_.Reset();
    if (activation_ != nullptr)
      static_cast<void>(activation_->ShutdownObject());
    activation_.Reset();
    scope_.reset();
    initialized_ = false;
    asynchronous_ = false;
    input_requested_ = false;
    width_ = 0;
    height_ = 0;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(
      const webrtc::VideoFrame &frame,
      const std::vector<webrtc::VideoFrameType> *frame_types) override {
    if (!initialized_ || transform_ == nullptr || callback_ == nullptr)
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    if (pending_frames_.size() >= kMaxPendingFrames) {
      callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
      return WEBRTC_VIDEO_CODEC_OK;
    }

    if (!pump_events())
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    if (asynchronous_ && !wait_for_input()) {
      callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
      return WEBRTC_VIDEO_CODEC_OK;
    }

    const auto i420 = frame.video_frame_buffer()->ToI420();
    if (i420 == nullptr || i420->width() != width_ ||
        i420->height() != height_) {
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    }

    bool request_keyframe = false;
    if (frame_types != nullptr) {
      request_keyframe = std::find(frame_types->begin(), frame_types->end(),
                                   webrtc::VideoFrameType::kVideoFrameKey) !=
                         frame_types->end();
    }
    if (request_keyframe)
      force_keyframe();

    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buffer;
    const auto input_size = static_cast<DWORD>(
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
        3U / 2U);
    if (FAILED(MFCreateSample(&sample)) ||
        FAILED(MFCreateMemoryBuffer(input_size, &buffer))) {
      return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    BYTE *data = nullptr;
    DWORD capacity = 0;
    if (FAILED(buffer->Lock(&data, &capacity, nullptr)))
      return WEBRTC_VIDEO_CODEC_MEMORY;
    const bool copied = capacity >= input_size && copy_i420_to_nv12(
        *i420, std::span<std::byte>(reinterpret_cast<std::byte *>(data),
                                   input_size),
        width_);
    static_cast<void>(buffer->Unlock());
    if (!copied || FAILED(buffer->SetCurrentLength(input_size)) ||
        FAILED(sample->AddBuffer(buffer.Get()))) {
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    }

    const LONGLONG sample_time = frame.timestamp_us() * 10;
    const LONGLONG duration = 10'000'000LL / frames_per_second_;
    if (FAILED(sample->SetSampleTime(sample_time)) ||
        FAILED(sample->SetSampleDuration(duration))) {
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    }
    auto input_result = transform_->ProcessInput(kInputStream, sample.Get(), 0);
    if (input_result == MF_E_NOTACCEPTING) {
      if (!pump_events())
        return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
      input_result = transform_->ProcessInput(kInputStream, sample.Get(), 0);
    }
    if (FAILED(input_result))
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    input_requested_ = !asynchronous_;
    pending_frames_.push_back(
        {.rtp_timestamp = frame.rtp_timestamp(),
         .ntp_time_ms = frame.ntp_time_ms(),
         .capture_time_ms = frame.timestamp_us() / 1'000,
         .color_space = frame.color_space()});
    return pump_events() ? WEBRTC_VIDEO_CODEC_OK
                         : WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
  }

  void SetRates(const RateControlParameters &parameters) override {
    if (transform_ == nullptr)
      return;
    const auto bitrate = parameters.bitrate.get_sum_bps();
    if (bitrate == 0 || bitrate > std::numeric_limits<ULONG>::max())
      return;
    bitrate_ = static_cast<int>(bitrate);
    if (parameters.framerate_fps > 0.0)
      frames_per_second_ = std::max(1, static_cast<int>(parameters.framerate_fps));

    ComPtr<ICodecAPI> codec_api;
    if (FAILED(transform_->QueryInterface(IID_PPV_ARGS(&codec_api))))
      return;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = static_cast<ULONG>(bitrate_);
    static_cast<void>(
        codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &value));
    VariantClear(&value);
  }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.implementation_name = "MediaFoundation";
    info.is_hardware_accelerated = true;
    info.supports_native_handle = false;
    info.requested_resolution_alignment = 2;
    info.apply_alignment_to_all_simulcast_layers = true;
    info.has_trusted_rate_controller = false;
    return info;
  }

 private:
  bool wait_for_input() {
    for (int attempt = 0; attempt < 6 && !input_requested_; ++attempt) {
      if (!pump_events())
        return false;
      if (!input_requested_)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return input_requested_;
  }

  bool pump_events() {
    if (!asynchronous_)
      return drain_synchronous_output();
    while (true) {
      ComPtr<IMFMediaEvent> event;
      const auto result =
          event_generator_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
      if (result == MF_E_NO_EVENTS_AVAILABLE)
        return true;
      if (FAILED(result))
        return false;
      MediaEventType type = MEUnknown;
      HRESULT event_status = S_OK;
      if (FAILED(event->GetType(&type)) ||
          FAILED(event->GetStatus(&event_status)) || FAILED(event_status)) {
        return false;
      }
      if (type == METransformNeedInput) {
        input_requested_ = true;
      } else if (type == METransformHaveOutput) {
        if (!process_one_output())
          return false;
      }
    }
  }

  bool drain_synchronous_output() {
    while (true) {
      const auto result = process_one_output_result();
      if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return true;
      if (FAILED(result))
        return false;
    }
  }

  bool process_one_output() {
    return SUCCEEDED(process_one_output_result());
  }

  HRESULT process_one_output_result() {
    MFT_OUTPUT_STREAM_INFO stream_info{};
    if (FAILED(transform_->GetOutputStreamInfo(kOutputStream, &stream_info)))
      return E_FAIL;

    ComPtr<IMFSample> sample;
    if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
      ComPtr<IMFMediaBuffer> buffer;
      const auto raw_frame_size = static_cast<std::size_t>(width_) *
                                  static_cast<std::size_t>(height_) * 3U / 2U;
      if (raw_frame_size > std::numeric_limits<DWORD>::max())
        return E_OUTOFMEMORY;
      const DWORD capacity = std::max<DWORD>(
          stream_info.cbSize, static_cast<DWORD>(raw_frame_size));
      if (FAILED(MFCreateSample(&sample)) ||
          FAILED(MFCreateMemoryBuffer(capacity, &buffer)) ||
          FAILED(sample->AddBuffer(buffer.Get()))) {
        return E_OUTOFMEMORY;
      }
    }

    MFT_OUTPUT_DATA_BUFFER output{};
    output.dwStreamID = kOutputStream;
    output.pSample = sample.Get();
    DWORD status = 0;
    const auto result = transform_->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents != nullptr)
      output.pEvents->Release();
    if (FAILED(result))
      return result;
    if (output.pSample != nullptr && sample == nullptr)
      sample.Attach(output.pSample);
    if (sample == nullptr || pending_frames_.empty())
      return E_FAIL;

    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
      return E_FAIL;
    BYTE *data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length)))
      return E_FAIL;
    std::vector<std::uint8_t> normalized;
    const auto normalized_result = normalize_h264_access_unit(
        std::span<const std::uint8_t>(data, length), normalized);
    static_cast<void>(buffer->Unlock());
    if (!normalized_result.valid)
      return E_FAIL;

    const auto metadata = std::move(pending_frames_.front());
    pending_frames_.pop_front();
    webrtc::EncodedImage encoded;
    encoded.SetEncodedData(webrtc::EncodedImageBuffer::Create(
        normalized.data(), normalized.size()));
    encoded._encodedWidth = static_cast<std::uint32_t>(width_);
    encoded._encodedHeight = static_cast<std::uint32_t>(height_);
    encoded.SetRtpTimestamp(metadata.rtp_timestamp);
    encoded.ntp_time_ms_ = metadata.ntp_time_ms;
    encoded.capture_time_ms_ = metadata.capture_time_ms;
    encoded.SetColorSpace(metadata.color_space);
    encoded.set_frame_type(normalized_result.keyframe
                               ? webrtc::VideoFrameType::kVideoFrameKey
                               : webrtc::VideoFrameType::kVideoFrameDelta);
    encoded.SetSimulcastIndex(0);
    encoded.set_end_of_temporal_unit(true);

    webrtc::CodecSpecificInfo codec_specific{};
    codec_specific.codecType = webrtc::kVideoCodecH264;
    codec_specific.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    codec_specific.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    codec_specific.codecSpecific.H264.idr_frame = normalized_result.keyframe;
    codec_specific.codecSpecific.H264.base_layer_sync = false;
    static_cast<void>(callback_->OnEncodedImage(encoded, &codec_specific));
    return S_OK;
  }

  void force_keyframe() {
    ComPtr<ICodecAPI> codec_api;
    if (FAILED(transform_->QueryInterface(IID_PPV_ARGS(&codec_api))))
      return;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = 1;
    static_cast<void>(
        codec_api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value));
    VariantClear(&value);
  }

  std::unique_ptr<MfScope> scope_;
  ComPtr<IMFActivate> activation_;
  ComPtr<IMFTransform> transform_;
  ComPtr<IMFMediaEventGenerator> event_generator_;
  webrtc::EncodedImageCallback *callback_{nullptr};
  std::deque<PendingFrame> pending_frames_;
  int width_{0};
  int height_{0};
  int frames_per_second_{30};
  int bitrate_{1'000'000};
  bool initialized_{false};
  bool asynchronous_{false};
  bool input_requested_{false};
};

bool can_configure_hardware_encoder(int width, int height) {
  MfScope scope;
  if (!scope.ready())
    return false;
  const auto activations = enumerate_hardware_encoders();
  for (const auto &activation : activations) {
    ComPtr<IMFTransform> transform;
    if (FAILED(activation->ActivateObject(IID_PPV_ARGS(&transform))))
      continue;
    bool asynchronous = false;
    ComPtr<IMFMediaEventGenerator> events;
    const bool configured = configure_transform(transform.Get(), width, height,
                                                60, 8'000'000, asynchronous,
                                                events);
    if (configured) {
      static_cast<void>(
          transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
      static_cast<void>(activation->ShutdownObject());
      return true;
    }
    static_cast<void>(activation->ShutdownObject());
  }
  return false;
}

} // namespace

bool probe_windows_mf_h264_encoder(int width, int height,
                                   std::string &reason) {
  if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0) {
    reason = "mf-h264-invalid-dimensions";
    return false;
  }
  if (!can_configure_hardware_encoder(width, height)) {
    reason = "mf-h264-hardware-unavailable";
    return false;
  }
  reason.clear();
  return true;
}

std::unique_ptr<webrtc::VideoEncoder> create_windows_mf_h264_encoder() {
  return std::make_unique<WindowsMfH264Encoder>();
}

} // namespace shareme::rtc
