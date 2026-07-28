#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace shareme::core {

enum class OverflowPolicy {
  drop_oldest,
  reject_newest,
};

template <typename T>
class BoundedQueue {
public:
  BoundedQueue(std::size_t capacity, OverflowPolicy policy)
      : capacity_{capacity}, policy_{policy} {
    if (capacity_ == 0) {
      throw std::invalid_argument{"BoundedQueue capacity must be positive"};
    }
  }

  [[nodiscard]] bool push(T item) {
    std::scoped_lock lock{mutex_};
    if (items_.size() == capacity_) {
      ++dropped_count_;
      if (policy_ == OverflowPolicy::reject_newest) {
        return false;
      }
      items_.pop_front();
    }

    items_.push_back(std::move(item));
    return true;
  }

  [[nodiscard]] std::optional<T> pop() {
    std::scoped_lock lock{mutex_};
    if (items_.empty()) {
      return std::nullopt;
    }

    auto item = std::move(items_.front());
    items_.pop_front();
    return item;
  }

  void clear() {
    std::scoped_lock lock{mutex_};
    items_.clear();
  }

  [[nodiscard]] std::size_t size() const {
    std::scoped_lock lock{mutex_};
    return items_.size();
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] bool empty() const {
    std::scoped_lock lock{mutex_};
    return items_.empty();
  }

  [[nodiscard]] std::uint64_t dropped_count() const {
    std::scoped_lock lock{mutex_};
    return dropped_count_;
  }

private:
  const std::size_t capacity_;
  const OverflowPolicy policy_;
  mutable std::mutex mutex_;
  std::deque<T> items_;
  std::uint64_t dropped_count_{0};
};

}  // namespace shareme::core
