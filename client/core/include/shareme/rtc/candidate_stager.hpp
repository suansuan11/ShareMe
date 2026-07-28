#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace shareme::rtc {

template <typename T, std::size_t Capacity>
  requires(Capacity > 0)
class CandidateStager {
public:
  CandidateStager() = default;

  CandidateStager(const CandidateStager&) = delete;
  CandidateStager& operator=(const CandidateStager&) = delete;
  CandidateStager(CandidateStager&&) = default;
  CandidateStager& operator=(CandidateStager&&) = default;

  [[nodiscard]] bool stage(T value) {
    if (size_ == Capacity) {
      if (overflow_count_ != std::numeric_limits<std::uint64_t>::max()) {
        ++overflow_count_;
      }
      return false;
    }

    const auto tail = (head_ + size_) % Capacity;
    entries_[tail].emplace(std::move(value));
    ++size_;
    return true;
  }

  [[nodiscard]] std::vector<T> drain() {
    std::vector<T> drained;
    drained.reserve(size_);
    while (size_ > 0) {
      auto& entry = entries_[head_];
      drained.push_back(std::move(*entry));
      entry.reset();
      head_ = (head_ + 1) % Capacity;
      --size_;
    }
    head_ = 0;
    return drained;
  }

  void clear() noexcept {
    while (size_ > 0) {
      entries_[head_].reset();
      head_ = (head_ + 1) % Capacity;
      --size_;
    }
    head_ = 0;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] std::uint64_t overflow_count() const noexcept {
    return overflow_count_;
  }

private:
  std::array<std::optional<T>, Capacity> entries_;
  std::size_t head_{0};
  std::size_t size_{0};
  std::uint64_t overflow_count_{0};
};

}  // namespace shareme::rtc
