#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace shareme::tools {

enum class ScreenCaptureRecoveryState {
  inactive,
  waiting,
  attempting,
  recovered,
  exhausted,
};

[[nodiscard]] bool
is_recoverable_screen_capture_error(std::string_view category) noexcept;

class ScreenCaptureRecoveryPolicy {
public:
  [[nodiscard]] bool begin() noexcept;
  [[nodiscard]] bool begin_attempt() noexcept;
  [[nodiscard]] bool record_success() noexcept;
  [[nodiscard]] bool record_failure() noexcept;
  void reset() noexcept;

  [[nodiscard]] ScreenCaptureRecoveryState state() const noexcept;
  [[nodiscard]] std::size_t attempt() const noexcept;
  [[nodiscard]] std::optional<int> delay_ms() const noexcept;

private:
  ScreenCaptureRecoveryState state_{ScreenCaptureRecoveryState::inactive};
  std::size_t attempt_{0};
};

} // namespace shareme::tools
