#include "shareme/rtc/desktop_capture_source.hpp"

#include "desktop_frame_converter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <string>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "rtc_base/time_utils.h"

namespace shareme::rtc {
namespace {

using Microsoft::WRL::ComPtr;

class AcquiredFrame final {
public:
  explicit AcquiredFrame(IDXGIOutputDuplication *duplication)
      : duplication_(duplication) {}
  ~AcquiredFrame() {
    if (duplication_ != nullptr)
      duplication_->ReleaseFrame();
  }

  AcquiredFrame(const AcquiredFrame &) = delete;
  AcquiredFrame &operator=(const AcquiredFrame &) = delete;

private:
  IDXGIOutputDuplication *duplication_;
};

class MappedTexture final {
public:
  MappedTexture(ID3D11DeviceContext *context, ID3D11Texture2D *texture,
                const D3D11_MAPPED_SUBRESOURCE &mapping)
      : context_(context), texture_(texture), mapping_(mapping) {}
  ~MappedTexture() { context_->Unmap(texture_, 0); }

  [[nodiscard]] const D3D11_MAPPED_SUBRESOURCE &mapping() const noexcept {
    return mapping_;
  }

  MappedTexture(const MappedTexture &) = delete;
  MappedTexture &operator=(const MappedTexture &) = delete;

private:
  ID3D11DeviceContext *context_;
  ID3D11Texture2D *texture_;
  D3D11_MAPPED_SUBRESOURCE mapping_;
};

[[nodiscard]] int rotation_degrees(DXGI_MODE_ROTATION rotation) noexcept {
  switch (rotation) {
  case DXGI_MODE_ROTATION_ROTATE90:
    return 90;
  case DXGI_MODE_ROTATION_ROTATE180:
    return 180;
  case DXGI_MODE_ROTATION_ROTATE270:
    return 270;
  default:
    return 0;
  }
}

[[nodiscard]] webrtc::VideoRotation
video_rotation(int degrees) noexcept {
  switch (degrees) {
  case 90:
    return webrtc::kVideoRotation_90;
  case 180:
    return webrtc::kVideoRotation_180;
  case 270:
    return webrtc::kVideoRotation_270;
  default:
    return webrtc::kVideoRotation_0;
  }
}

} // namespace

class DesktopCaptureSource::Impl final {
public:
  explicit Impl(DesktopCaptureSource &owner) : owner_(owner) {}
  ~Impl() { stop(); }

  [[nodiscard]] bool start() {
    if (!initialize())
      return false;
    worker_ = std::jthread([this](std::stop_token token) { capture(token); });
    return true;
  }

  void stop() noexcept {
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
    release_device();
  }

private:
  [[nodiscard]] bool initialize() {
    release_device();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      set_error("desktop-dxgi-unavailable");
      return false;
    }

    ComPtr<IDXGIAdapter1> selected_adapter;
    ComPtr<IDXGIOutput> selected_output;
    for (UINT adapter_index = 0;; ++adapter_index) {
      ComPtr<IDXGIAdapter1> adapter;
      const auto adapter_result =
          factory->EnumAdapters1(adapter_index, &adapter);
      if (adapter_result == DXGI_ERROR_NOT_FOUND)
        break;
      if (FAILED(adapter_result))
        continue;

      for (UINT output_index = 0;; ++output_index) {
        ComPtr<IDXGIOutput> output;
        const auto output_result =
            adapter->EnumOutputs(output_index, &output);
        if (output_result == DXGI_ERROR_NOT_FOUND)
          break;
        if (FAILED(output_result))
          continue;

        DXGI_OUTPUT_DESC description{};
        if (FAILED(output->GetDesc(&description)) ||
            !description.AttachedToDesktop) {
          continue;
        }
        if (selected_output == nullptr) {
          selected_adapter = adapter;
          selected_output = output;
        }
        const auto &area = description.DesktopCoordinates;
        if (area.left <= 0 && area.right > 0 && area.top <= 0 &&
            area.bottom > 0) {
          selected_adapter = adapter;
          selected_output = output;
          break;
        }
      }
      if (selected_output != nullptr) {
        DXGI_OUTPUT_DESC description{};
        if (SUCCEEDED(selected_output->GetDesc(&description))) {
          const auto &area = description.DesktopCoordinates;
          if (area.left <= 0 && area.right > 0 && area.top <= 0 &&
              area.bottom > 0)
            break;
        }
      }
    }

    if (selected_output == nullptr || selected_adapter == nullptr) {
      set_error("desktop-output-unavailable");
      return false;
    }

    constexpr std::array feature_levels{D3D_FEATURE_LEVEL_11_1,
                                        D3D_FEATURE_LEVEL_11_0};
    auto device_result = D3D11CreateDevice(
        selected_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels.data(),
        static_cast<UINT>(feature_levels.size()), D3D11_SDK_VERSION, &device_,
        nullptr, &context_);
    if (device_result == E_INVALIDARG) {
      device_result = D3D11CreateDevice(
          selected_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels.data() + 1, 1,
          D3D11_SDK_VERSION, &device_, nullptr, &context_);
    }
    if (FAILED(device_result)) {
      set_error("desktop-d3d11-unavailable");
      return false;
    }

    HRESULT duplication_result = E_NOINTERFACE;
    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(selected_output.As(&output5))) {
      constexpr std::array supported_formats{
          DXGI_FORMAT_B8G8R8A8_UNORM,
      };
      duplication_result = output5->DuplicateOutput1(
          device_.Get(), 0, static_cast<UINT>(supported_formats.size()),
          supported_formats.data(), &duplication_);
    }
    if (FAILED(duplication_result)) {
      ComPtr<IDXGIOutput1> output1;
      if (SUCCEEDED(selected_output.As(&output1)))
        duplication_result =
            output1->DuplicateOutput(device_.Get(), &duplication_);
    }
    if (FAILED(duplication_result)) {
      set_error("desktop-duplication-unavailable");
      return false;
    }

    DXGI_OUTDUPL_DESC duplication_description{};
    duplication_->GetDesc(&duplication_description);
    rotation_ = rotation_degrees(duplication_description.Rotation);
    return true;
  }

  void release_device() noexcept {
    staging_.Reset();
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
    staging_width_ = 0;
    staging_height_ = 0;
    staging_format_ = DXGI_FORMAT_UNKNOWN;
  }

  [[nodiscard]] bool ensure_staging(const D3D11_TEXTURE2D_DESC &source) {
    if (staging_ != nullptr && staging_width_ == source.Width &&
        staging_height_ == source.Height && staging_format_ == source.Format)
      return true;

    D3D11_TEXTURE2D_DESC description = source;
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging))) {
      set_error("desktop-staging-create-failed");
      return false;
    }
    staging_ = std::move(staging);
    staging_width_ = source.Width;
    staging_height_ = source.Height;
    staging_format_ = source.Format;
    return true;
  }

  void capture(std::stop_token stop_token) {
    using namespace std::chrono_literals;
    const auto minimum_interval = std::chrono::microseconds(
        1'000'000 / owner_.config_.max_frames_per_second);
    auto last_delivery = std::chrono::steady_clock::time_point::min();
    bool rebuilt_after_access_loss = false;

    while (!stop_token.stop_requested()) {
      DXGI_OUTDUPL_FRAME_INFO frame_info{};
      ComPtr<IDXGIResource> resource;
      const auto acquire_result = duplication_->AcquireNextFrame(
          50, &frame_info, &resource);
      if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT)
        continue;
      if (acquire_result == DXGI_ERROR_ACCESS_LOST) {
        if (rebuilt_after_access_loss || !initialize()) {
          if (owner_.error().empty())
            set_error("desktop-access-lost");
          break;
        }
        rebuilt_after_access_loss = true;
        continue;
      }
      if (FAILED(acquire_result)) {
        set_error("desktop-frame-acquire-failed");
        break;
      }

      AcquiredFrame acquired(duplication_.Get());
      const auto now = std::chrono::steady_clock::now();
      if (last_delivery != std::chrono::steady_clock::time_point::min() &&
          now - last_delivery + 1ms < minimum_interval) {
        owner_.dropped_count_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }

      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(resource.As(&texture))) {
        set_error("desktop-frame-resource-invalid");
        break;
      }
      D3D11_TEXTURE2D_DESC description{};
      texture->GetDesc(&description);
      if (!ensure_staging(description))
        break;

      context_->CopyResource(staging_.Get(), texture.Get());
      D3D11_MAPPED_SUBRESOURCE mapping{};
      if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0,
                               &mapping))) {
        set_error("desktop-frame-map-failed");
        break;
      }
      MappedTexture mapped(context_.Get(), staging_.Get(), mapping);
      webrtc::scoped_refptr<webrtc::I420Buffer> buffer;
      if (description.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        buffer = detail::convert_mapped_bgra_to_i420(
            {static_cast<const std::uint8_t *>(mapped.mapping().pData),
             static_cast<int>(description.Width),
             static_cast<int>(description.Height), mapped.mapping().RowPitch});
      } else if (description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        buffer = detail::convert_mapped_rgba16_float_to_i420(
            {static_cast<const std::uint8_t *>(mapped.mapping().pData),
             static_cast<int>(description.Width),
             static_cast<int>(description.Height), mapped.mapping().RowPitch});
      } else {
        set_error("desktop-format-unsupported-" +
                  std::to_string(
                      static_cast<unsigned int>(description.Format)));
        break;
      }
      if (buffer == nullptr) {
        set_error("desktop-frame-conversion-failed");
        break;
      }

      owner_.deliver_frame(std::move(buffer), rotation_);
      last_delivery = now;
      rebuilt_after_access_loss = false;
    }
    owner_.running_.store(false, std::memory_order_release);
  }

  void set_error(std::string category) {
    std::lock_guard lock(owner_.error_mutex_);
    if (owner_.error_.empty())
      owner_.error_ = std::move(category);
  }

  DesktopCaptureSource &owner_;
  std::jthread worker_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGIOutputDuplication> duplication_;
  ComPtr<ID3D11Texture2D> staging_;
  UINT staging_width_{0};
  UINT staging_height_{0};
  DXGI_FORMAT staging_format_{DXGI_FORMAT_UNKNOWN};
  int rotation_{0};
};

webrtc::scoped_refptr<DesktopCaptureSource>
DesktopCaptureSource::create(DesktopCaptureConfig config) {
  if (!valid_desktop_capture_config(config))
    return nullptr;
  return webrtc::scoped_refptr<DesktopCaptureSource>(
      new DesktopCaptureSource(config));
}

DesktopCaptureSource::DesktopCaptureSource(DesktopCaptureConfig config)
    : config_(config), impl_(std::make_unique<Impl>(*this)) {}

DesktopCaptureSource::~DesktopCaptureSource() { stop(); }

bool DesktopCaptureSource::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel))
    return true;
  {
    std::lock_guard lock(error_mutex_);
    error_.clear();
  }
  if (!impl_->start()) {
    running_.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

void DesktopCaptureSource::stop() noexcept {
  running_.store(false, std::memory_order_release);
  impl_->stop();
}

std::uint64_t DesktopCaptureSource::generated_count() const noexcept {
  return generated_count_.load(std::memory_order_relaxed);
}

std::uint64_t DesktopCaptureSource::dropped_count() const noexcept {
  return dropped_count_.load(std::memory_order_relaxed);
}

std::string DesktopCaptureSource::error() const {
  std::lock_guard lock(error_mutex_);
  return error_;
}

int DesktopCaptureSource::last_width() const noexcept {
  return last_width_.load(std::memory_order_relaxed);
}

int DesktopCaptureSource::last_height() const noexcept {
  return last_height_.load(std::memory_order_relaxed);
}

bool DesktopCaptureSource::is_screencast() const { return true; }

std::optional<bool> DesktopCaptureSource::needs_denoising() const {
  return false;
}

webrtc::MediaSourceInterface::SourceState DesktopCaptureSource::state() const {
  return running_.load(std::memory_order_acquire) ? kLive : kEnded;
}

bool DesktopCaptureSource::remote() const { return false; }

void DesktopCaptureSource::deliver_frame(
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer, int rotation) {
  auto timestamp_us = webrtc::TimeMicros();
  int output_width = 0;
  int output_height = 0;
  int crop_width = 0;
  int crop_height = 0;
  int crop_x = 0;
  int crop_y = 0;
  if (!AdaptFrame(buffer->width(), buffer->height(), timestamp_us,
                  &output_width, &output_height, &crop_width, &crop_height,
                  &crop_x, &crop_y)) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  webrtc::scoped_refptr<webrtc::I420Buffer> output = buffer;
  if (output_width != buffer->width() || output_height != buffer->height() ||
      crop_width != buffer->width() || crop_height != buffer->height() ||
      crop_x != 0 || crop_y != 0) {
    output = webrtc::I420Buffer::Create(output_width, output_height);
    output->CropAndScaleFrom(*buffer, crop_x, crop_y, crop_width, crop_height);
  }

  const auto rtp_timestamp = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(timestamp_us) * 90U / 1'000U);
  const auto frame = webrtc::VideoFrame::Builder()
                         .set_video_frame_buffer(output)
                         .set_timestamp_us(timestamp_us)
                         .set_rtp_timestamp(rtp_timestamp)
                         .set_rotation(video_rotation(rotation))
                         .build();
  OnFrame(frame);
  last_width_.store(output_width, std::memory_order_relaxed);
  last_height_.store(output_height, std::memory_order_relaxed);
  generated_count_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace shareme::rtc
