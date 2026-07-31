#pragma once

namespace shareme::rtc::detail {

enum class AccessLossAction { rebuild, fail };
enum class AccessLossResult { recovered, failed };

class AccessLossRecovery final {
public:
  [[nodiscard]] AccessLossAction on_access_lost() noexcept {
    if (rebuild_attempted_)
      return AccessLossAction::fail;
    rebuild_attempted_ = true;
    return AccessLossAction::rebuild;
  }

  void on_frame_acquired() noexcept { rebuild_attempted_ = false; }

private:
  bool rebuild_attempted_{false};
};

template <typename Rebuild>
[[nodiscard]] AccessLossResult
recover_from_access_loss(AccessLossRecovery &recovery, Rebuild rebuild) {
  if (recovery.on_access_lost() == AccessLossAction::fail || !rebuild())
    return AccessLossResult::failed;
  return AccessLossResult::recovered;
}

} // namespace shareme::rtc::detail
