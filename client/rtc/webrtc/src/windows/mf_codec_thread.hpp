#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace shareme::rtc {

// All COM/MF object creation, use and destruction for one adapter runs here.
// State outlives a self-destroyed wrapper until the worker has exited.
class MfCodecThread final {
 public:
  MfCodecThread() : state_(std::make_shared<State>()) {
    const auto state = state_;
    worker_ = std::thread([state] { run(state); });
    std::unique_lock lock(state->mutex);
    state->started.wait(lock, [&] { return state->started_ready; });
  }

  ~MfCodecThread() {
    const auto state = std::move(state_);
    if (state == nullptr)
      return;
    {
      std::lock_guard lock(state->mutex);
      state->stopping = true;
    }
    state->wake.notify_one();
    if (!worker_.joinable())
      return;
    if (worker_.get_id() == std::this_thread::get_id()) {
      worker_.detach();
    } else {
      worker_.join();
    }
  }

  MfCodecThread(const MfCodecThread &) = delete;
  MfCodecThread &operator=(const MfCodecThread &) = delete;

  template <typename F>
  auto invoke(F &&function) -> std::invoke_result_t<F> {
    using Result = std::invoke_result_t<F>;
    const auto state = state_;
    if (state == nullptr)
      throw std::runtime_error("mf-codec-thread-stopped");
    bool on_owner_thread = false;
    {
      std::lock_guard lock(state->mutex);
      if (state->stopping)
        throw std::runtime_error("mf-codec-thread-stopped");
      on_owner_thread = state->owner_thread == std::this_thread::get_id();
    }
    if (on_owner_thread)
      return std::forward<F>(function)();

    auto completion = std::make_shared<std::promise<Result>>();
    auto future = completion->get_future();
    {
      std::lock_guard lock(state->mutex);
      if (state->stopping)
        throw std::runtime_error("mf-codec-thread-stopped");
      state->work.emplace([function = std::forward<F>(function), completion]() mutable {
        try {
          if constexpr (std::is_void_v<Result>) {
            function();
            completion->set_value();
          } else {
            completion->set_value(function());
          }
        } catch (...) {
          completion->set_exception(std::current_exception());
        }
      });
    }
    state->wake.notify_one();
    if constexpr (std::is_void_v<Result>) {
      future.get();
    } else {
      return future.get();
    }
  }

 private:
  struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable started;
    std::queue<std::function<void()>> work;
    std::thread::id owner_thread{};
    bool started_ready{false};
    bool stopping{false};
  };

  static void run(const std::shared_ptr<State> &state) {
    {
      std::lock_guard lock(state->mutex);
      state->owner_thread = std::this_thread::get_id();
      state->started_ready = true;
    }
    state->started.notify_one();
    for (;;) {
      std::function<void()> work;
      {
        std::unique_lock lock(state->mutex);
        state->wake.wait(lock,
                         [&] { return state->stopping || !state->work.empty(); });
        if (state->stopping && state->work.empty())
          return;
        work = std::move(state->work.front());
        state->work.pop();
      }
      work();
    }
  }

  std::shared_ptr<State> state_;
  std::thread worker_;
};

} // namespace shareme::rtc
