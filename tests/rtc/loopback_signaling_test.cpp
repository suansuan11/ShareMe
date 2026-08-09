#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "api/environment/environment_factory.h"
#include "audio_device_factory.hpp"
#include "loopback_signaling.hpp"
#include "webrtc_runtime.hpp"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition) {
    return;
  }
  std::cerr << "requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

std::shared_ptr<shareme::rtc::WebRtcRuntime> create_test_runtime() {
  const auto audio = shareme::rtc::create_audio_device(
      webrtc::CreateEnvironment(), shareme::rtc::AudioDeviceMode::synthetic);
  REQUIRE(audio.ok());
  return shareme::rtc::WebRtcRuntime::create(audio.device);
}

} // namespace

int main() {
  const auto started_at = std::chrono::steady_clock::now();
  auto runtime = create_test_runtime();
  REQUIRE(runtime != nullptr);
  REQUIRE(runtime->threads_running());

  {
    shareme::rtc::LoopbackSignaling overflow_probe(*runtime);
    for (std::size_t index = 0; index < 64; ++index) {
      REQUIRE(overflow_probe.stage_candidate_for_test(
          shareme::rtc::LoopbackPeer::left,
          "candidate-" + std::to_string(index)));
    }
    REQUIRE(!overflow_probe.stage_candidate_for_test(
        shareme::rtc::LoopbackPeer::left, "candidate-overflow"));
    REQUIRE(overflow_probe.failure().find("64") != std::string::npos);
  }

  {
    shareme::rtc::LoopbackSignaling cancelled(*runtime);
    const auto result = cancelled.negotiate(std::chrono::milliseconds(0));
    REQUIRE(!result.ok);
    REQUIRE(result.error.find("timed out") != std::string::npos);
    cancelled.stop();
    cancelled.stop();
  }

  {
    shareme::rtc::LoopbackSignaling signaling(*runtime);
    const auto result = signaling.negotiate(std::chrono::seconds(10));
    REQUIRE(result.ok);
    REQUIRE(result.left_ice_connected);
    REQUIRE(result.right_ice_connected);
    REQUIRE(result.left_dtls_connected);
    REQUIRE(result.right_dtls_connected);
    REQUIRE(result.drained_candidate_count > 0);
    REQUIRE(result.error.empty());
    const auto repeated = signaling.negotiate(std::chrono::seconds(1));
    REQUIRE(!repeated.ok);
    REQUIRE(repeated.error.find("only start once") != std::string::npos);
    signaling.stop();
    signaling.stop();
  }

  const auto owned_thread_stop = runtime->signaling_thread()->BlockingCall(
      [&] { return runtime->stop(); });
  REQUIRE(!owned_thread_stop);
  REQUIRE(runtime->threads_running());
  REQUIRE(runtime->stop());
  REQUIRE(runtime->stop());
  runtime.reset();

  auto runtime_first = create_test_runtime();
  REQUIRE(runtime_first != nullptr);
  auto active_signaling =
      std::make_unique<shareme::rtc::LoopbackSignaling>(*runtime_first);
  REQUIRE(active_signaling->negotiate(std::chrono::seconds(10)).ok);
  REQUIRE(runtime_first->stop());
  REQUIRE(!runtime_first->threads_running());
  active_signaling.reset();
  runtime_first.reset();

  auto concurrent_runtime = create_test_runtime();
  REQUIRE(concurrent_runtime != nullptr);
  auto concurrent_signaling =
      std::make_unique<shareme::rtc::LoopbackSignaling>(*concurrent_runtime);
  REQUIRE(concurrent_signaling->negotiate(std::chrono::seconds(10)).ok);
  std::barrier start_concurrent_shutdown(3);
  std::thread runtime_stopper([&] {
    start_concurrent_shutdown.arrive_and_wait();
    REQUIRE(concurrent_runtime->stop());
  });
  std::thread signaling_destroyer([&] {
    start_concurrent_shutdown.arrive_and_wait();
    concurrent_signaling.reset();
  });
  start_concurrent_shutdown.arrive_and_wait();
  runtime_stopper.join();
  signaling_destroyer.join();
  concurrent_runtime.reset();

  auto concurrent_stop_runtime = create_test_runtime();
  REQUIRE(concurrent_stop_runtime != nullptr);
  std::barrier start_concurrent_stop(3);
  std::atomic_bool first_stop{false};
  std::atomic_bool second_stop{false};
  std::thread first_stopper(
      [runtime = concurrent_stop_runtime, &start_concurrent_stop, &first_stop] {
        start_concurrent_stop.arrive_and_wait();
        first_stop.store(runtime->stop(), std::memory_order_release);
      });
  std::thread second_stopper([runtime = concurrent_stop_runtime,
                              &start_concurrent_stop, &second_stop] {
    start_concurrent_stop.arrive_and_wait();
    second_stop.store(runtime->stop(), std::memory_order_release);
  });
  start_concurrent_stop.arrive_and_wait();
  first_stopper.join();
  second_stopper.join();
  REQUIRE(first_stop.load(std::memory_order_acquire));
  REQUIRE(second_stop.load(std::memory_order_acquire));
  concurrent_stop_runtime.reset();

  auto self_releasing_runtime = create_test_runtime();
  REQUIRE(self_releasing_runtime != nullptr);
  auto self_release_completed = self_releasing_runtime->destruction_completed();
  auto *self_release_thread = self_releasing_runtime->signaling_thread();
  auto owned_on_signaling = self_releasing_runtime;
  self_release_thread->PostTask(
      [owned = std::move(owned_on_signaling)]() mutable { owned.reset(); });
  self_releasing_runtime.reset();
  REQUIRE(self_release_completed.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

  REQUIRE(std::chrono::steady_clock::now() - started_at <
          std::chrono::seconds(5));
  return EXIT_SUCCESS;
}
