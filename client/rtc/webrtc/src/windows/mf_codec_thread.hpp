#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>

namespace shareme::rtc {

// All COM/MF object creation, use and destruction for one adapter runs here.
// invoke() is re-entrant so a client callback may safely call Release().
class MfCodecThread final {
 public:
  MfCodecThread() : worker_([this] { run(); }) {
    std::unique_lock lock(mutex_);
    started_.wait(lock, [this] { return started_ready_; });
  }

  ~MfCodecThread() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
      worker_.join();
  }

  MfCodecThread(const MfCodecThread &) = delete;
  MfCodecThread &operator=(const MfCodecThread &) = delete;

  template <typename F>
  auto invoke(F &&function) -> std::invoke_result_t<F> {
    using Result = std::invoke_result_t<F>;
    if (std::this_thread::get_id() == owner_thread_)
      return std::forward<F>(function)();

    auto completion = std::make_shared<std::promise<Result>>();
    auto future = completion->get_future();
    {
      std::lock_guard lock(mutex_);
      work_.emplace([function = std::forward<F>(function), completion]() mutable {
        if constexpr (std::is_void_v<Result>) {
          function();
          completion->set_value();
        } else {
          completion->set_value(function());
        }
      });
    }
    wake_.notify_one();
    if constexpr (std::is_void_v<Result>) {
      future.get();
    } else {
      return future.get();
    }
  }

 private:
  void run() {
    {
      std::lock_guard lock(mutex_);
      owner_thread_ = std::this_thread::get_id();
      started_ready_ = true;
    }
    started_.notify_one();
    for (;;) {
      std::function<void()> work;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || !work_.empty(); });
        if (stopping_ && work_.empty())
          return;
        work = std::move(work_.front());
        work_.pop();
      }
      work();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable started_;
  std::queue<std::function<void()>> work_;
  std::thread worker_;
  std::thread::id owner_thread_{};
  bool started_ready_{false};
  bool stopping_{false};
};

} // namespace shareme::rtc
