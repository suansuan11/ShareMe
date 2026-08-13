#include "session_lifecycle_monitor.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <memory>
#include <utility>

namespace shareme::tools {
namespace {

class MacSessionLifecycleMonitor final
    : public SessionLifecycleNativeMonitor {
  struct CallbackState {
    Callback callback;
    std::atomic_bool accepting{false};

    void notify(NSNotification *notification) const {
      if (!accepting.load(std::memory_order_acquire) || notification == nil)
        return;
      const char *name = notification.name.UTF8String;
      if (name == nullptr)
        return;
      if (const auto event = session_lifecycle_event_for_notification(name))
        callback(*event);
    }
  };

public:
  explicit MacSessionLifecycleMonitor(Callback callback)
      : state_{std::make_shared<CallbackState>()} {
    state_->callback = std::move(callback);
  }

  ~MacSessionLifecycleMonitor() override { stop(); }

  bool start() override {
    if (started_ || !state_->callback)
      return false;
    state_->accepting.store(true, std::memory_order_release);
    const auto state = state_;
    auto handler = ^(NSNotification *notification) {
      state->notify(notification);
    };

    NSNotificationCenter *workspace_center =
        NSWorkspace.sharedWorkspace.notificationCenter;
    will_sleep_token_ = [workspace_center
        addObserverForName:NSWorkspaceWillSleepNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:handler];
    did_wake_token_ = [workspace_center
        addObserverForName:NSWorkspaceDidWakeNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:handler];

    NSDistributedNotificationCenter *distributed_center =
        NSDistributedNotificationCenter.defaultCenter;
    locked_token_ = [distributed_center
        addObserverForName:@"com.apple.screenIsLocked"
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:handler];
    unlocked_token_ = [distributed_center
        addObserverForName:@"com.apple.screenIsUnlocked"
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:handler];

    started_ = will_sleep_token_ != nil && did_wake_token_ != nil &&
               locked_token_ != nil && unlocked_token_ != nil;
    if (!started_)
      stop();
    return started_;
  }

  void stop() noexcept override {
    state_->accepting.store(false, std::memory_order_release);
    NSNotificationCenter *workspace_center =
        NSWorkspace.sharedWorkspace.notificationCenter;
    if (will_sleep_token_ != nil)
      [workspace_center removeObserver:will_sleep_token_];
    if (did_wake_token_ != nil)
      [workspace_center removeObserver:did_wake_token_];
    NSDistributedNotificationCenter *distributed_center =
        NSDistributedNotificationCenter.defaultCenter;
    if (locked_token_ != nil)
      [distributed_center removeObserver:locked_token_];
    if (unlocked_token_ != nil)
      [distributed_center removeObserver:unlocked_token_];
    will_sleep_token_ = nil;
    did_wake_token_ = nil;
    locked_token_ = nil;
    unlocked_token_ = nil;
    started_ = false;
  }

private:
  std::shared_ptr<CallbackState> state_;
  __strong id will_sleep_token_{nil};
  __strong id did_wake_token_{nil};
  __strong id locked_token_{nil};
  __strong id unlocked_token_{nil};
  bool started_{false};
};

} // namespace

std::unique_ptr<SessionLifecycleNativeMonitor>
create_session_lifecycle_native_monitor(
    SessionLifecycleNativeMonitor::Callback callback) {
  return std::make_unique<MacSessionLifecycleMonitor>(std::move(callback));
}

} // namespace shareme::tools
