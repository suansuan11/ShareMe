#include "screen_capture_recovery_policy.hpp"

#include <array>

namespace shareme::tools {
namespace {

constexpr std::array retry_delays_ms{250, 500, 1'000};

} // namespace

bool ScreenCaptureRecoveryPolicy::begin() noexcept {
  if (state_ != ScreenCaptureRecoveryState::inactive)
    return false;
  state_ = ScreenCaptureRecoveryState::waiting;
  return true;
}

bool ScreenCaptureRecoveryPolicy::begin_attempt() noexcept {
  if (state_ != ScreenCaptureRecoveryState::waiting ||
      attempt_ >= retry_delays_ms.size()) {
    return false;
  }
  ++attempt_;
  state_ = ScreenCaptureRecoveryState::attempting;
  return true;
}

bool ScreenCaptureRecoveryPolicy::record_success() noexcept {
  if (state_ != ScreenCaptureRecoveryState::attempting)
    return false;
  state_ = ScreenCaptureRecoveryState::recovered;
  return true;
}

bool ScreenCaptureRecoveryPolicy::record_failure() noexcept {
  if (state_ != ScreenCaptureRecoveryState::attempting)
    return false;
  state_ = attempt_ >= retry_delays_ms.size()
               ? ScreenCaptureRecoveryState::exhausted
               : ScreenCaptureRecoveryState::waiting;
  return true;
}

void ScreenCaptureRecoveryPolicy::reset() noexcept {
  state_ = ScreenCaptureRecoveryState::inactive;
  attempt_ = 0;
}

ScreenCaptureRecoveryState ScreenCaptureRecoveryPolicy::state() const noexcept {
  return state_;
}

std::size_t ScreenCaptureRecoveryPolicy::attempt() const noexcept {
  return attempt_;
}

std::optional<int> ScreenCaptureRecoveryPolicy::delay_ms() const noexcept {
  if (state_ != ScreenCaptureRecoveryState::waiting ||
      attempt_ >= retry_delays_ms.size()) {
    return std::nullopt;
  }
  return retry_delays_ms[attempt_];
}

} // namespace shareme::tools
