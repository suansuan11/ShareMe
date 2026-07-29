#include "microphone_permission.hpp"

#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

namespace shareme::rtc {

MicrophonePermissionStatus platform_microphone_permission_status() noexcept {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
  case AVAuthorizationStatusAuthorized:
    return MicrophonePermissionStatus::granted;
  case AVAuthorizationStatusDenied:
  case AVAuthorizationStatusRestricted:
    return MicrophonePermissionStatus::denied;
  case AVAuthorizationStatusNotDetermined: {
    __block BOOL granted = NO;
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL allowed) {
                               granted = allowed;
                               dispatch_semaphore_signal(completed);
                             }];
    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
    return granted ? MicrophonePermissionStatus::granted
                   : MicrophonePermissionStatus::denied;
  }
  }
  return MicrophonePermissionStatus::unknown;
}

} // namespace shareme::rtc
