#include "shareme/core/bounded_queue.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)
#define REQUIRE_FALSE(expression) require(!(expression), "!(" #expression ")", __LINE__)

void rejects_zero_capacity() {
  bool threw = false;
  try {
    const shareme::core::BoundedQueue<int> queue{
        0, shareme::core::OverflowPolicy::drop_oldest};
  } catch (const std::invalid_argument&) {
    threw = true;
  }

  REQUIRE(threw);
}

void drops_oldest_video_item() {
  using shareme::core::BoundedQueue;
  using shareme::core::OverflowPolicy;

  BoundedQueue<int> queue{2, OverflowPolicy::drop_oldest};
  REQUIRE(queue.capacity() == 2);
  REQUIRE(queue.empty());
  REQUIRE(queue.push(1));
  REQUIRE(queue.push(2));
  REQUIRE(queue.push(3));
  REQUIRE(queue.size() == 2);
  REQUIRE(queue.pop() == std::optional<int>{2});
  REQUIRE(queue.pop() == std::optional<int>{3});
  REQUIRE(queue.dropped_count() == 1);
}

void rejects_newest_audio_item() {
  using shareme::core::BoundedQueue;
  using shareme::core::OverflowPolicy;

  BoundedQueue<int> queue{1, OverflowPolicy::reject_newest};
  REQUIRE(queue.push(7));
  REQUIRE_FALSE(queue.push(8));
  REQUIRE(queue.pop() == std::optional<int>{7});
  REQUIRE(queue.pop() == std::nullopt);
  REQUIRE(queue.dropped_count() == 1);
}

void preserves_fifo_order_and_clear_metrics() {
  using shareme::core::BoundedQueue;
  using shareme::core::OverflowPolicy;

  BoundedQueue<int> queue{2, OverflowPolicy::reject_newest};
  REQUIRE(queue.push(10));
  REQUIRE(queue.push(20));
  REQUIRE(queue.pop() == std::optional<int>{10});
  REQUIRE(queue.push(30));
  REQUIRE_FALSE(queue.push(40));

  queue.clear();

  REQUIRE(queue.empty());
  REQUIRE(queue.size() == 0);
  REQUIRE(queue.dropped_count() == 1);
}

}  // namespace

int main() {
  rejects_zero_capacity();
  drops_oldest_video_item();
  rejects_newest_audio_item();
  preserves_fifo_order_and_clear_metrics();
  return EXIT_SUCCESS;
}
