#include "shareme/rtc/video_encoder_selection.hpp"

#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include "sdk/objc/native/api/video_encoder_factory.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#import "sdk/objc/components/video_codec/RTCVideoEncoderFactoryH264.h"
#import "sdk/objc/components/video_codec/RTCVideoDecoderFactoryH264.h"

namespace shareme::rtc {

bool probe_platform_video_toolbox_encoder(int width, int height,
                                          std::string &reason) {
  NSDictionary *specification = @{
    (__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES
  };
  VTCompressionSessionRef session = nullptr;
  const auto status = VTCompressionSessionCreate(
      kCFAllocatorDefault, width, height, kCMVideoCodecType_H264, nullptr,
      (__bridge CFDictionaryRef)specification, nullptr, nullptr, nullptr,
      &session);
  if (status != noErr || session == nullptr) {
    reason = "videotoolbox-session-unavailable";
    return false;
  }

  CFTypeRef property = nullptr;
  const auto property_status = VTSessionCopyProperty(
      session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
      kCFAllocatorDefault, &property);
  const bool hardware_active =
      property_status == noErr && property != nullptr &&
      CFGetTypeID(property) == CFBooleanGetTypeID() &&
      CFBooleanGetValue(static_cast<CFBooleanRef>(property));
  if (property != nullptr)
    CFRelease(property);
  VTCompressionSessionInvalidate(session);
  CFRelease(session);

  if (!hardware_active) {
    reason = "videotoolbox-hardware-not-active";
    return false;
  }
  reason.clear();
  return true;
}

std::unique_ptr<webrtc::VideoEncoderFactory>
create_platform_video_toolbox_encoder_factory() {
  auto *objc_factory =
      [[RTC_OBJC_TYPE(RTCVideoEncoderFactoryH264) alloc] init];
  if (objc_factory == nil)
    return nullptr;
  return webrtc::ObjCToNativeVideoEncoderFactory(objc_factory);
}

std::unique_ptr<webrtc::VideoDecoderFactory>
create_platform_video_decoder_factory() {
  auto *objc_factory =
      [[RTC_OBJC_TYPE(RTCVideoDecoderFactoryH264) alloc] init];
  if (objc_factory == nil)
    return nullptr;
  return webrtc::ObjCToNativeVideoDecoderFactory(objc_factory);
}

} // namespace shareme::rtc
