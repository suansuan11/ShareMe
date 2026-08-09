#include "windows_mf_h264_decoder.hpp"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <thread>
#include <vector>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "windows_mf_h264_buffers.hpp"
#include "windows/h264_bitstream.hpp"

namespace shareme::rtc {
namespace {

using Microsoft::WRL::ComPtr;
constexpr DWORD kInputStream = 0;
constexpr DWORD kOutputStream = 0;
constexpr std::size_t kMaxPendingFrames = 1;
constexpr std::size_t kMaxEncodedAccessUnitBytes = 32U * 1024U * 1024U;

void shutdown_transform(IMFTransform *transform) {
  if (transform == nullptr)
    return;
  ComPtr<IMFShutdown> shutdown;
  if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&shutdown))))
    static_cast<void>(shutdown->Shutdown());
}

class MfScope final {
 public:
  MfScope() {
    const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_initialized_ = com_result == S_OK || com_result == S_FALSE;
    mf_initialized_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
  }
  ~MfScope() {
    if (mf_initialized_)
      MFShutdown();
    if (com_initialized_)
      CoUninitialize();
  }
  [[nodiscard]] bool ready() const noexcept { return mf_initialized_; }

 private:
  bool com_initialized_{false};
  bool mf_initialized_{false};
};

std::vector<ComPtr<IMFActivate>> enumerate_decoders() {
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_H264};
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_NV12};
  IMFActivate **raw = nullptr;
  UINT32 count = 0;
  if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                       MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
                           MFT_ENUM_FLAG_SORTANDFILTER,
                       &input, &output, &raw, &count))) {
    return {};
  }
  std::vector<ComPtr<IMFActivate>> result;
  result.reserve(count);
  for (UINT32 index = 0; index < count; ++index) {
    ComPtr<IMFActivate> activation;
    activation.Attach(raw[index]);
    result.push_back(std::move(activation));
  }
  CoTaskMemFree(raw);
  return result;
}

bool set_input_type(IMFTransform *transform, int width, int height) {
  ComPtr<IMFMediaType> type;
  return SUCCEEDED(MFCreateMediaType(&type)) &&
         SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
         SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) &&
         SUCCEEDED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE,
                                     static_cast<UINT32>(width),
                                     static_cast<UINT32>(height))) &&
         SUCCEEDED(type->SetUINT32(MF_MT_INTERLACE_MODE,
                                   MFVideoInterlace_Progressive)) &&
         SUCCEEDED(transform->SetInputType(kInputStream, type.Get(), 0));
}

bool select_nv12_output(IMFTransform *transform, int fallback_width,
                        int fallback_height, int &width, int &height,
                        int &stride) {
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> type;
    const auto result =
        transform->GetOutputAvailableType(kOutputStream, index, &type);
    if (result == MF_E_NO_MORE_TYPES || FAILED(result))
      return false;
    GUID subtype{};
    if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
        subtype != MFVideoFormat_NV12) {
      continue;
    }
    UINT32 output_width = 0;
    UINT32 output_height = 0;
    if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &output_width,
                                  &output_height)) ||
        output_width == 0 || output_height == 0) {
      output_width = static_cast<UINT32>(fallback_width);
      output_height = static_cast<UINT32>(fallback_height);
      if (FAILED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, output_width,
                                    output_height))) {
        return false;
      }
    }
    static_cast<void>(type->SetUINT32(MF_MT_DEFAULT_STRIDE, output_width));
    if (FAILED(transform->SetOutputType(kOutputStream, type.Get(), 0)))
      continue;
    UINT32 output_stride = output_width;
    static_cast<void>(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &output_stride));
    if (output_width > INT_MAX || output_height > INT_MAX ||
        output_stride > INT_MAX) {
      return false;
    }
    width = static_cast<int>(output_width);
    height = static_cast<int>(output_height);
    stride = static_cast<int>(output_stride);
    return width > 0 && height > 0 && (width & 1) == 0 &&
           (height & 1) == 0 && stride >= width;
  }
}

struct PendingFrame {
  std::uint32_t rtp_timestamp{0};
  std::int64_t ntp_time_ms{0};
  std::optional<webrtc::ColorSpace> color_space;
};

bool can_configure_decoder(int width, int height) {
  if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0)
    return false;
  MfScope scope;
  if (!scope.ready())
    return false;
  for (const auto &activation : enumerate_decoders()) {
    ComPtr<IMFTransform> transform;
    if (FAILED(activation->ActivateObject(IID_PPV_ARGS(&transform))))
      continue;
    int output_width = width;
    int output_height = height;
    int output_stride = width;
    const bool configured =
        set_input_type(transform.Get(), width, height) &&
        select_nv12_output(transform.Get(), width, height, output_width,
                           output_height, output_stride) &&
        SUCCEEDED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
                                            0)) &&
        SUCCEEDED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
                                            0));
    static_cast<void>(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
    static_cast<void>(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING,
                                                0));
    shutdown_transform(transform.Get());
    transform.Reset();
    static_cast<void>(activation->ShutdownObject());
    if (configured)
      return true;
  }
  return false;
}

class WindowsMfH264Decoder final : public webrtc::VideoDecoder {
 public:
  ~WindowsMfH264Decoder() override { static_cast<void>(Release()); }

  bool Configure(const Settings &settings) override {
    auto *const registered_callback = callback_;
    static_cast<void>(Release());
    {
      std::lock_guard state_lock(state_mutex_);
      release_requested_ = false;
      callback_ = registered_callback;
      needs_keyframe_ = false;
    }
    if (settings.codec_type() != webrtc::kVideoCodecH264)
      return false;
    scope_ = std::make_unique<MfScope>();
    configured_ = scope_->ready();
    if (!configured_)
      scope_.reset();
    return configured_;
  }

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback *callback) override {
    std::lock_guard state_lock(state_mutex_);
    if (release_requested_)
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Decode(const webrtc::EncodedImage &input, int64_t) override {
    std::lock_guard operation_lock(operation_mutex_);
    {
      std::lock_guard state_lock(state_mutex_);
      if (release_requested_ || !configured_ || callback_ == nullptr)
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (input.data() == nullptr || input.size() == 0 ||
        input.size() > kMaxEncodedAccessUnitBytes)
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    const auto access_unit = inspect_annex_b(
        std::span<const std::uint8_t>(input.data(), input.size()));
    if (!access_unit.has_value()) {
      needs_keyframe_ = true;
      pending_frames_.clear();
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (needs_keyframe_ && !access_unit.has_idr)
      return WEBRTC_VIDEO_CODEC_ERROR;
    if (needs_keyframe_ && access_unit.has_idr) {
      static_cast<void>(transform_ == nullptr ? S_OK : transform_->ProcessMessage(
          MFT_MESSAGE_COMMAND_FLUSH, 0));
      pending_frames_.clear();
      needs_keyframe_ = false;
    }
    if (transform_ == nullptr &&
        !initialize(static_cast<int>(input._encodedWidth),
                    static_cast<int>(input._encodedHeight))) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (pending_frames_.size() >= kMaxPendingFrames)
      return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
    if (!drain_output())
      return WEBRTC_VIDEO_CODEC_ERROR;

    if (staging_buffer_ == nullptr || staging_capacity_ < input.size()) {
      staging_sample_.Reset();
      staging_buffer_.Reset();
      if (FAILED(MFCreateSample(&staging_sample_)) ||
          FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(input.size()),
                                      &staging_buffer_)))
        return WEBRTC_VIDEO_CODEC_MEMORY;
      if (FAILED(staging_sample_->AddBuffer(staging_buffer_.Get())))
        return WEBRTC_VIDEO_CODEC_ERROR;
      staging_capacity_ = input.size();
    }
    auto &sample = staging_sample_;
    auto &buffer = staging_buffer_;
    BYTE *data = nullptr;
    if (FAILED(buffer->Lock(&data, nullptr, nullptr)))
      return WEBRTC_VIDEO_CODEC_MEMORY;
    std::copy_n(input.data(), input.size(), data);
    static_cast<void>(buffer->Unlock());
    if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(input.size())))) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    const LONGLONG sample_time =
        static_cast<LONGLONG>(input.RtpTimestamp()) * 10'000'000LL / 90'000LL;
    if (FAILED(sample->SetSampleTime(sample_time)) ||
        FAILED(sample->SetSampleDuration(10'000'000LL / 60LL)) ||
        FAILED(sample->SetUINT32(
            MFSampleExtension_CleanPoint,
            input._frameType == webrtc::VideoFrameType::kVideoFrameKey)))
      return WEBRTC_VIDEO_CODEC_ERROR;
    auto result = transform_->ProcessInput(kInputStream, sample.Get(), 0);
    if (result == MF_E_NOTACCEPTING) {
      if (!drain_output())
        return WEBRTC_VIDEO_CODEC_ERROR;
      result = transform_->ProcessInput(kInputStream, sample.Get(), 0);
    }
    if (FAILED(result))
      return WEBRTC_VIDEO_CODEC_ERROR;
    pending_frames_.push_back(
        {.rtp_timestamp = input.RtpTimestamp(),
         .ntp_time_ms = input.NtpTimeMs(),
         .color_space = input.ColorSpace() == nullptr
                            ? std::optional<webrtc::ColorSpace>{}
                            : std::optional<webrtc::ColorSpace>{
                                  *input.ColorSpace()}});
    const auto final_result = drain_output() ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
    if (final_result != WEBRTC_VIDEO_CODEC_OK || !pending_frames_.empty()) {
      needs_keyframe_ = true;
      pending_frames_.clear();
      static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (release_requested())
      teardown();
    return final_result;
  }

  int32_t Release() override {
    {
      std::lock_guard state_lock(state_mutex_);
      callback_ = nullptr;
      release_requested_ = true;
      if (callback_thread_ == std::this_thread::get_id())
        return WEBRTC_VIDEO_CODEC_OK;
    }
    std::lock_guard operation_lock(operation_mutex_);
    teardown();
    return WEBRTC_VIDEO_CODEC_OK;
  }

 private:
  void teardown() {
    pending_frames_.clear();
    if (transform_ != nullptr) {
      static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,
                                                   0));
      static_cast<void>(transform_->ProcessMessage(
          MFT_MESSAGE_NOTIFY_END_STREAMING, 0));
    }
    shutdown_transform(transform_.Get());
    transform_.Reset();
    staging_sample_.Reset();
    staging_buffer_.Reset();
    staging_capacity_ = 0;
    output_sample_.Reset();
    output_buffer_.Reset();
    output_capacity_ = 0;
    needs_keyframe_ = false;
    if (activation_ != nullptr)
      static_cast<void>(activation_->ShutdownObject());
    activation_.Reset();
    scope_.reset();
    configured_ = false;
    width_ = 0;
    height_ = 0;
    stride_ = 0;
    visible_width_ = 0;
    visible_height_ = 0;
  }

  DecoderInfo GetDecoderInfo() const override {
    return {.implementation_name = "MediaFoundation",
            .is_hardware_accelerated = false};
  }

  [[nodiscard]] bool release_requested() const {
    std::lock_guard state_lock(state_mutex_);
    return release_requested_;
  }
  bool initialize(int width, int height) {
    if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0)
      return false;
    for (const auto &activation : enumerate_decoders()) {
      ComPtr<IMFTransform> candidate;
      if (FAILED(activation->ActivateObject(IID_PPV_ARGS(&candidate))))
        continue;
      int output_width = width;
      int output_height = height;
      int output_stride = width;
      ComPtr<IMFAttributes> attributes;
      if (SUCCEEDED(candidate->GetAttributes(&attributes)) &&
          attributes != nullptr) {
        static_cast<void>(attributes->SetUINT32(MF_LOW_LATENCY, TRUE));
      }
      if (!set_input_type(candidate.Get(), width, height) ||
          !select_nv12_output(candidate.Get(), width, height, output_width,
                              output_height, output_stride) ||
          FAILED(candidate->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
                                           0)) ||
          FAILED(candidate->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
                                           0))) {
        shutdown_transform(candidate.Get());
        attributes.Reset();
        candidate.Reset();
        static_cast<void>(activation->ShutdownObject());
        continue;
      }
      activation_ = activation;
      transform_ = std::move(candidate);
      width_ = output_width;
      height_ = output_height;
      stride_ = output_stride;
      visible_width_ = width;
      visible_height_ = height;
      return true;
    }
    return false;
  }

  bool drain_output() {
    while (true) {
      const auto result = process_one_output();
      if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return true;
      if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (!select_nv12_output(transform_.Get(), width_, height_, width_,
                                height_, stride_)) {
          return false;
        }
        continue;
      }
      if (FAILED(result))
        return false;
    }
  }

  HRESULT process_one_output() {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform_->GetOutputStreamInfo(kOutputStream, &info)))
      return E_FAIL;
    if (info.cbSize > kMaxEncodedAccessUnitBytes)
      return E_OUTOFMEMORY;
    const auto rows = static_cast<std::size_t>(height_) * 3U / 2U;
    if (stride_ <= 0 || static_cast<std::size_t>(stride_) >
                            std::numeric_limits<std::size_t>::max() / rows) {
      return E_OUTOFMEMORY;
    }
    const auto required = static_cast<std::size_t>(stride_) * rows;
    if (required > std::numeric_limits<DWORD>::max())
      return E_OUTOFMEMORY;
    ComPtr<IMFSample> sample;
    if ((info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
      const DWORD capacity =
          std::max(info.cbSize, static_cast<DWORD>(required));
      if (output_buffer_ == nullptr || output_capacity_ < capacity) {
        output_sample_.Reset(); output_buffer_.Reset();
        if (FAILED(MFCreateSample(&output_sample_)) ||
            FAILED(MFCreateMemoryBuffer(capacity, &output_buffer_)) ||
            FAILED(output_sample_->AddBuffer(output_buffer_.Get())))
          return E_OUTOFMEMORY;
        output_capacity_ = capacity;
      }
      if (FAILED(output_buffer_->SetCurrentLength(0))) return E_OUTOFMEMORY;
      sample = output_sample_;
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
    if (sample == nullptr && output.pSample != nullptr)
      sample.Attach(output.pSample);
    if (sample == nullptr || pending_frames_.empty())
      return E_FAIL;
    ComPtr<IMFMediaBuffer> contiguous;
    if (FAILED(sample->ConvertToContiguousBuffer(&contiguous)))
      return E_FAIL;
    BYTE *data = nullptr;
    DWORD length = 0;
    if (FAILED(contiguous->Lock(&data, nullptr, &length)))
      return E_FAIL;
    auto coded_buffer = webrtc::I420Buffer::Create(width_, height_);
    const bool copied = copy_nv12_to_i420(
        std::span<const std::uint8_t>(data, length), width_, height_, stride_,
        *coded_buffer);
    static_cast<void>(contiguous->Unlock());
    if (!copied)
      return E_FAIL;
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer =
        coded_buffer;
    if (visible_width_ != width_ || visible_height_ != height_) {
      frame_buffer = coded_buffer->CropAndScale(
          0, 0, visible_width_, visible_height_, visible_width_,
          visible_height_);
    }
    const auto metadata = std::move(pending_frames_.front());
    pending_frames_.pop_front();
    auto frame = webrtc::VideoFrame::Builder()
                     .set_video_frame_buffer(frame_buffer)
                     .set_timestamp_us(
                         static_cast<std::int64_t>(metadata.rtp_timestamp) *
                         1'000'000LL / 90'000LL)
                     .set_rtp_timestamp(metadata.rtp_timestamp)
                     .set_ntp_time_ms(metadata.ntp_time_ms)
                     .set_rotation(webrtc::kVideoRotation_0)
                     .set_color_space(metadata.color_space)
                     .build();
    webrtc::DecodedImageCallback *callback = nullptr;
    {
      std::lock_guard state_lock(state_mutex_);
      if (release_requested_ || callback_ == nullptr)
        return E_ABORT;
      callback_thread_ = std::this_thread::get_id();
      callback = callback_;
    }
    static_cast<void>(callback->Decoded(frame));
    {
      std::lock_guard state_lock(state_mutex_);
      callback_thread_ = {};
    }
    return S_OK;
  }

  std::unique_ptr<MfScope> scope_;
  ComPtr<IMFActivate> activation_;
  ComPtr<IMFTransform> transform_;
  webrtc::DecodedImageCallback *callback_{nullptr};
  std::deque<PendingFrame> pending_frames_;
  int width_{0};
  int height_{0};
  int stride_{0};
  int visible_width_{0};
  int visible_height_{0};
  bool configured_{false};
  bool needs_keyframe_{false};
  ComPtr<IMFSample> staging_sample_;
  ComPtr<IMFMediaBuffer> staging_buffer_;
  std::size_t staging_capacity_{0};
  ComPtr<IMFSample> output_sample_;
  ComPtr<IMFMediaBuffer> output_buffer_;
  DWORD output_capacity_{0};
  mutable std::mutex state_mutex_;
  std::mutex operation_mutex_;
  std::thread::id callback_thread_{};
  bool release_requested_{false};
};

} // namespace

bool probe_windows_mf_h264_decoder(int width, int height,
                                   std::string &reason) {
  if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0) {
    reason = "mf-h264-invalid-dimensions";
    return false;
  }
  if (!can_configure_decoder(width, height)) {
    reason = "mf-h264-decoder-unavailable";
    return false;
  }
  reason.clear();
  return true;
}

std::unique_ptr<webrtc::VideoDecoder> create_windows_mf_h264_decoder() {
  return std::make_unique<WindowsMfH264Decoder>();
}

} // namespace shareme::rtc
