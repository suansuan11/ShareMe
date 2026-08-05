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
  using ItemSize = std::size_t (*)(const T&) noexcept;

  BoundedQueue(
      std::size_t capacity,
      OverflowPolicy policy,
      ItemSize item_size = nullptr)
      : capacity_{capacity}, policy_{policy}, item_size_{item_size} {
    if (capacity_ == 0) {
      throw std::invalid_argument{"BoundedQueue capacity must be positive"};
    }
  }

  [[nodiscard]] bool push(T item) {
    std::scoped_lock lock{mutex_};
    const auto item_bytes = bytes_for(item);
    if (items_.size() == capacity_) {
      ++dropped_count_;
      if (policy_ == OverflowPolicy::reject_newest) {
        dropped_bytes_ += item_bytes;
        return false;
      }
      const auto front_bytes = bytes_for(items_.front());
      items_.pop_front();
      bytes_ -= front_bytes;
      dropped_bytes_ += front_bytes;
    }

    items_.push_back(std::move(item));
    bytes_ += item_bytes;
    if (bytes_ > peak_bytes_) {
      peak_bytes_ = bytes_;
    }
    return true;
  }

  [[nodiscard]] std::optional<T> pop() {
    std::scoped_lock lock{mutex_};
    if (items_.empty()) {
      return std::nullopt;
    }

    bytes_ -= bytes_for(items_.front());
    auto item = std::move(items_.front());
    items_.pop_front();
    return item;
  }

  void clear() {
    std::scoped_lock lock{mutex_};
    items_.clear();
    bytes_ = 0;
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

  [[nodiscard]] std::size_t bytes() const {
    std::scoped_lock lock{mutex_};
    return bytes_;
  }

  [[nodiscard]] std::size_t peak_bytes() const {
    std::scoped_lock lock{mutex_};
    return peak_bytes_;
  }

  [[nodiscard]] std::size_t dropped_bytes() const {
    std::scoped_lock lock{mutex_};
    return dropped_bytes_;
  }

private:
  [[nodiscard]] std::size_t bytes_for(const T& item) const noexcept {
    return item_size_ == nullptr ? 0 : item_size_(item);
  }

  const std::size_t capacity_;
  const OverflowPolicy policy_;
  const ItemSize item_size_;
  mutable std::mutex mutex_;
  std::deque<T> items_;
  std::uint64_t dropped_count_{0};
  std::size_t bytes_{0};
  std::size_t peak_bytes_{0};
  std::size_t dropped_bytes_{0};
};

}  // namespace shareme::core
