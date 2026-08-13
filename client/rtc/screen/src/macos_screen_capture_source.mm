#include "shareme/rtc/macos_screen_capture_source.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

#include <mutex>
#include <string>
#include <utility>

#include "api/make_ref_counted.h"
#include "rtc_base/time_utils.h"
#include "sdk/objc/components/video_frame_buffer/RTCCVPixelBuffer.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"

namespace {

std::string sanitized_error(const char *prefix, NSError *error) {
  return std::string(prefix) + "-" +
         std::to_string(error == nil ? 0 : error.code);
}

} // namespace

@interface ShareMeScreenCaptureDelegate : NSObject <SCStreamDelegate,
                                                     SCStreamOutput>
- (instancetype)initWithFrameHandler:(void (^)(CMSampleBufferRef))frameHandler
                        errorHandler:(void (^)(NSError *))errorHandler;
@end

namespace shareme::rtc {

class ScreenCaptureKitStream final : public MacScreenCaptureStream {
public:
  explicit ScreenCaptureKitStream(MacScreenCaptureConfig config)
      : config_(config) {}

  ~ScreenCaptureKitStream() override { stop(); }

  [[nodiscard]] bool start(FrameCallback callback) override;
  void stop() noexcept override;
  [[nodiscard]] std::string error() const override;

private:
  [[nodiscard]] bool select_display(SCShareableContent *content,
                                    SCDisplay **display);
  [[nodiscard]] bool start_with_display(SCDisplay *display);
  void handle_sample_buffer(CMSampleBufferRef sample_buffer,
                            MacScreenCaptureEventGate::Generation generation);
  void handle_stream_error(NSError *error,
                           MacScreenCaptureEventGate::Generation generation);
  void set_error(std::string value);

  const MacScreenCaptureConfig config_;
  mutable std::mutex mutex_;
  FrameCallback callback_;
  std::string error_;
  SCStream *__strong stream_{nil};
  ShareMeScreenCaptureDelegate *__strong delegate_{nil};
  bool stopped_with_error_{false};
  MacScreenCaptureEventGate event_gate_;
  MacScreenCaptureEventGate::Generation active_generation_{0};
};

} // namespace shareme::rtc

@implementation ShareMeScreenCaptureDelegate {
  void (^frame_handler_)(CMSampleBufferRef);
  void (^error_handler_)(NSError *);
}

- (instancetype)initWithFrameHandler:(void (^)(CMSampleBufferRef))frameHandler
                        errorHandler:(void (^)(NSError *))errorHandler {
  self = [super init];
  if (self) {
    frame_handler_ = [frameHandler copy];
    error_handler_ = [errorHandler copy];
  }
  return self;
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
  static_cast<void>(stream);
  if (type != SCStreamOutputTypeScreen || sampleBuffer == nullptr)
    return;
  if (frame_handler_ != nil)
    frame_handler_(sampleBuffer);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  static_cast<void>(stream);
  if (error_handler_ != nil)
    error_handler_(error);
}

@end

namespace shareme::rtc {

bool ScreenCaptureKitStream::start(FrameCallback callback) {
  {
    std::lock_guard lock(mutex_);
    if (stream_ != nil)
      return true;
    callback_ = std::move(callback);
    error_.clear();
    stopped_with_error_ = false;
  }

  __block SCShareableContent *content = nil;
  __block NSError *content_error = nil;
  dispatch_semaphore_t content_completed = dispatch_semaphore_create(0);
  [SCShareableContent
      getShareableContentExcludingDesktopWindows:NO
                          onScreenWindowsOnly:YES
                                completionHandler:^(SCShareableContent *value,
                                                    NSError *error) {
                                  content = value;
                                  content_error = error;
                                  dispatch_semaphore_signal(content_completed);
                                }];
  dispatch_semaphore_wait(content_completed, DISPATCH_TIME_FOREVER);

  if (content == nil) {
    set_error(sanitized_error("screen-shareable-content-unavailable",
                              content_error));
    std::lock_guard lock(mutex_);
    callback_ = {};
    return false;
  }

  SCDisplay *display = nil;
  if (!select_display(content, &display)) {
    std::lock_guard lock(mutex_);
    callback_ = {};
    return false;
  }
  const bool started = start_with_display(display);
  if (!started) {
    std::lock_guard lock(mutex_);
    callback_ = {};
  }
  return started;
}

void ScreenCaptureKitStream::stop() noexcept {
  SCStream *stream = nil;
  ShareMeScreenCaptureDelegate *delegate = nil;
  bool stopped_with_error = false;
  {
    std::lock_guard lock(mutex_);
    stream = stream_;
    delegate = delegate_;
    stopped_with_error = stopped_with_error_;
    event_gate_.end(active_generation_);
  }
  if (stream == nil)
    return;

  if (!stopped_with_error) {
    dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(NSError *error) {
      if (error != nil)
        set_error(sanitized_error("screen-capture-stop-failed", error));
      dispatch_semaphore_signal(stopped);
    }];
    dispatch_semaphore_wait(stopped, DISPATCH_TIME_FOREVER);
  }
  [stream removeStreamOutput:delegate type:SCStreamOutputTypeScreen error:nil];

  std::lock_guard lock(mutex_);
  stream_ = nil;
  delegate_ = nil;
  callback_ = {};
  stopped_with_error_ = false;
  active_generation_ = 0;
}

std::string ScreenCaptureKitStream::error() const {
  std::lock_guard lock(mutex_);
  return error_;
}

bool ScreenCaptureKitStream::select_display(SCShareableContent *content,
                                             SCDisplay **display) {
  if (content.displays.count == 0) {
    set_error("screen-display-unavailable");
    return false;
  }

  const auto requested_display = config_.display_id.has_value()
                                     ? static_cast<CGDirectDisplayID>(
                                           *config_.display_id)
                                     : CGMainDisplayID();
  for (SCDisplay *candidate in content.displays) {
    if (candidate.displayID == requested_display) {
      *display = candidate;
      return true;
    }
  }

  *display = content.displays.firstObject;
  return *display != nil;
}

bool ScreenCaptureKitStream::start_with_display(SCDisplay *display) {
  const auto source_width = static_cast<int>(
      CGDisplayPixelsWide(display.displayID));
  const auto source_height = static_cast<int>(
      CGDisplayPixelsHigh(display.displayID));
  const auto dimensions = core::fit_screen_dimensions(
      source_width, source_height, config_.profile);
  const auto bounds = core::screen_stream_profile_bounds(config_.profile);
  if (dimensions.width <= 0 || dimensions.height <= 0) {
    set_error("screen-display-dimensions-unavailable");
    return false;
  }

  auto *configuration = [[SCStreamConfiguration alloc] init];
  configuration.width = static_cast<size_t>(dimensions.width);
  configuration.height = static_cast<size_t>(dimensions.height);
  configuration.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
  configuration.showsCursor = config_.show_cursor;
  configuration.minimumFrameInterval =
      CMTimeMake(1, static_cast<int32_t>(bounds.max_frames_per_second));
  configuration.queueDepth = 3;
  if (@available(macOS 13.0, *))
    configuration.capturesAudio = NO;

  const auto generation = event_gate_.begin();
  auto *delegate = [[ShareMeScreenCaptureDelegate alloc]
      initWithFrameHandler:^(CMSampleBufferRef sample_buffer) {
        handle_sample_buffer(sample_buffer, generation);
      }
                 errorHandler:^(NSError *error) {
                   if (error != nil)
                     handle_stream_error(error, generation);
                 }];
  auto *filter = [[SCContentFilter alloc] initWithDisplay:display
                                          excludingWindows:@[]];
  auto *stream = [[SCStream alloc] initWithFilter:filter
                                     configuration:configuration
                                          delegate:delegate];

  NSError *add_output_error = nil;
  if (![stream addStreamOutput:delegate
                          type:SCStreamOutputTypeScreen
            sampleHandlerQueue:dispatch_get_global_queue(
                                   QOS_CLASS_USER_INTERACTIVE, 0)
                         error:&add_output_error]) {
    set_error(sanitized_error("screen-capture-output-unavailable",
                              add_output_error));
    event_gate_.end(generation);
    return false;
  }

  __block NSError *start_error = nil;
  dispatch_semaphore_t started = dispatch_semaphore_create(0);
  [stream startCaptureWithCompletionHandler:^(NSError *error) {
    start_error = error;
    dispatch_semaphore_signal(started);
  }];
  dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
  if (start_error != nil) {
    set_error(sanitized_error("screen-capture-start-failed", start_error));
    [stream removeStreamOutput:delegate type:SCStreamOutputTypeScreen error:nil];
    event_gate_.end(generation);
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    stream_ = stream;
    delegate_ = delegate;
    stopped_with_error_ = false;
    active_generation_ = generation;
  }
  return true;
}

void ScreenCaptureKitStream::handle_sample_buffer(
    CMSampleBufferRef sample_buffer,
    MacScreenCaptureEventGate::Generation generation) {
  if (!event_gate_.accepts(generation))
    return;
  if (!CMSampleBufferDataIsReady(sample_buffer))
    return;

  const auto image_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
  if (image_buffer == nullptr)
    return;

  const auto attachments = CMSampleBufferGetSampleAttachmentsArray(
      sample_buffer, /*createIfNecessary=*/false);
  if (attachments != nullptr && CFArrayGetCount(attachments) > 0) {
    NSDictionary *attachment = (__bridge NSDictionary *)
        CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status_value = attachment[SCStreamFrameInfoStatus];
    if (status_value != nil &&
        static_cast<SCFrameStatus>(status_value.integerValue) !=
            SCFrameStatusComplete) {
      return;
    }
  }

  auto *objc_buffer = [[RTC_OBJC_TYPE(RTCCVPixelBuffer) alloc]
      initWithPixelBuffer:image_buffer];
  if (objc_buffer == nil)
    return;

  auto buffer = webrtc::make_ref_counted<webrtc::ObjCFrameBuffer>(objc_buffer);
  const auto pts = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
  auto timestamp_us = webrtc::TimeMicros();
  if (CMTIME_IS_VALID(pts) && CMTIME_IS_NUMERIC(pts)) {
    const auto scaled = CMTimeConvertScale(
        pts, 1'000'000, kCMTimeRoundingMethod_QuickTime);
    if (CMTIME_IS_VALID(scaled) && scaled.value >= 0)
      timestamp_us = scaled.value;
  }

  FrameCallback callback;
  {
    std::lock_guard lock(mutex_);
    callback = callback_;
  }
  if (callback == nullptr)
    return;

  callback(ScreenFrame{.buffer = std::move(buffer),
                       .width = static_cast<int>(CVPixelBufferGetWidth(
                           image_buffer)),
                       .height = static_cast<int>(CVPixelBufferGetHeight(
                           image_buffer)),
                       .capture_timestamp_us = timestamp_us,
                       .backing = ScreenFrameBacking::native});
}

void ScreenCaptureKitStream::set_error(std::string value) {
  std::lock_guard lock(mutex_);
  error_ = std::move(value);
}

void ScreenCaptureKitStream::handle_stream_error(
    NSError *error, MacScreenCaptureEventGate::Generation generation) {
  if (!event_gate_.accepts(generation))
    return;
  std::lock_guard lock(mutex_);
  if (!event_gate_.accepts(generation))
    return;
  error_ = sanitized_error("screen-capture-stopped", error);
  stopped_with_error_ = true;
}

} // namespace shareme::rtc

namespace shareme::rtc {

std::unique_ptr<MacScreenCaptureStream>
create_screen_capture_kit_stream(const MacScreenCaptureConfig &config) {
  return std::make_unique<ScreenCaptureKitStream>(config);
}

} // namespace shareme::rtc
