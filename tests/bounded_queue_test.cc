#include "minicam/bounded_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace minicam {
namespace {

using std::chrono_literals::operator""ms;

TEST(BoundedQueueTest, FifoOrder) {
  BoundedQueue<int> queue(4);
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(queue.Push(i));
  EXPECT_EQ(queue.size(), 4u);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(queue.Pop(), i);
  EXPECT_EQ(queue.size(), 0u);
}

TEST(BoundedQueueTest, WrapsAroundTheRingBuffer) {
  BoundedQueue<int> queue(3);
  for (int round = 0; round < 10; ++round) {
    EXPECT_TRUE(queue.Push(round * 2));
    EXPECT_TRUE(queue.Push(round * 2 + 1));
    EXPECT_EQ(queue.Pop(), round * 2);
    EXPECT_EQ(queue.Pop(), round * 2 + 1);
  }
}

TEST(BoundedQueueTest, SupportsMoveOnlyTypes) {
  BoundedQueue<std::unique_ptr<int>> queue(2);
  EXPECT_TRUE(queue.Push(std::make_unique<int>(7)));
  std::optional<std::unique_ptr<int>> item = queue.Pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(**item, 7);
}

TEST(BoundedQueueTest, PushBlocksWhileFull) {
  BoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.Push(1));

  std::atomic<bool> pushed{false};
  std::thread producer([&] {
    queue.Push(2);
    pushed = true;
  });
  // A sleep can only make this check miss a bug, never fail a correct
  // implementation.
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(pushed);

  EXPECT_EQ(queue.Pop(), 1);  // Makes room; the producer can finish.
  producer.join();
  EXPECT_TRUE(pushed);
  EXPECT_EQ(queue.Pop(), 2);
}

TEST(BoundedQueueTest, CloseWakesBlockedPop) {
  BoundedQueue<int> queue(1);
  std::optional<int> result = 42;
  std::thread consumer([&] { result = queue.Pop(); });
  std::this_thread::sleep_for(20ms);
  queue.Close();
  consumer.join();
  EXPECT_FALSE(result.has_value());
}

TEST(BoundedQueueTest, CloseWakesBlockedPushWhichFails) {
  BoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.Push(1));
  bool push_result = true;
  std::thread producer([&] { push_result = queue.Push(2); });
  std::this_thread::sleep_for(20ms);
  queue.Close();
  producer.join();
  EXPECT_FALSE(push_result);
}

TEST(BoundedQueueTest, CloseDrainsRemainingItemsThenEnds) {
  BoundedQueue<int> queue(4);
  ASSERT_TRUE(queue.Push(1));
  ASSERT_TRUE(queue.Push(2));
  queue.Close();
  EXPECT_TRUE(queue.closed());
  EXPECT_FALSE(queue.Push(3));
  EXPECT_EQ(queue.Pop(), 1);
  EXPECT_EQ(queue.Pop(), 2);
  EXPECT_EQ(queue.Pop(), std::nullopt);
  EXPECT_EQ(queue.Pop(), std::nullopt);
}

// Several producers and consumers: every item must arrive exactly once.
// Run under ThreadSanitizer to check for data races.
TEST(BoundedQueueTest, StressManyProducersAndConsumers) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kItemsPerProducer = 20000;
  BoundedQueue<int> queue(8);

  std::vector<std::vector<int>> received(kConsumers);
  std::vector<std::thread> consumers;
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&queue, &out = received[c]] {
      while (std::optional<int> item = queue.Pop()) out.push_back(*item);
    });
  }
  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&queue, p] {
      for (int i = 0; i < kItemsPerProducer; ++i) {
        ASSERT_TRUE(queue.Push(p * kItemsPerProducer + i));
      }
    });
  }
  for (std::thread& t : producers) t.join();
  queue.Close();
  for (std::thread& t : consumers) t.join();

  std::vector<int> all;
  for (const std::vector<int>& part : received) {
    // Items from one producer reach one consumer in the order pushed.
    for (int p = 0; p < kProducers; ++p) {
      int last = -1;
      for (int v : part) {
        if (v / kItemsPerProducer != p) continue;
        ASSERT_GT(v, last);
        last = v;
      }
    }
    all.insert(all.end(), part.begin(), part.end());
  }
  std::sort(all.begin(), all.end());
  ASSERT_EQ(all.size(), static_cast<size_t>(kProducers * kItemsPerProducer));
  for (size_t i = 0; i < all.size(); ++i) ASSERT_EQ(all[i], static_cast<int>(i));
}

}  // namespace
}  // namespace minicam
