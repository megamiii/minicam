#include "minicam/frame_pool.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "minicam/bounded_queue.h"

namespace minicam {
namespace {

using std::chrono_literals::operator""ms;

TEST(FramePoolTest, LendsDistinctFramesUpToItsSize) {
  FramePool pool(3);
  EXPECT_EQ(pool.size(), 3u);
  std::vector<FramePool::Handle> handles;
  std::set<Frame*> frames;
  for (int i = 0; i < 3; ++i) {
    handles.push_back(pool.Acquire());
    ASSERT_TRUE(handles.back());
    frames.insert(handles.back().get());
  }
  EXPECT_EQ(frames.size(), 3u);
  EXPECT_EQ(pool.available(), 0u);
  EXPECT_FALSE(pool.TryAcquire());
}

TEST(FramePoolTest, DestroyingAHandleReturnsTheFrame) {
  FramePool pool(1);
  Frame* first = nullptr;
  {
    FramePool::Handle handle = pool.Acquire();
    first = handle.get();
    EXPECT_EQ(pool.available(), 0u);
  }
  EXPECT_EQ(pool.available(), 1u);
  FramePool::Handle again = pool.Acquire();
  EXPECT_EQ(again.get(), first);  // The same frame is reused.
}

TEST(FramePoolTest, MovingAHandleTransfersOwnershipWithoutDoubleReturn) {
  FramePool pool(2);
  FramePool::Handle a = pool.Acquire();
  Frame* frame = a.get();
  FramePool::Handle b = std::move(a);
  EXPECT_FALSE(a);  // NOLINT(bugprone-use-after-move): checking moved-from.
  EXPECT_EQ(b.get(), frame);
  b.Release();
  b.Release();  // Second release is a no-op.
  EXPECT_EQ(pool.available(), 2u);

  // Move assignment releases the frame the target held.
  FramePool::Handle c = pool.Acquire();
  FramePool::Handle d = pool.Acquire();
  EXPECT_EQ(pool.available(), 0u);
  c = std::move(d);
  EXPECT_EQ(pool.available(), 1u);
}

TEST(FramePoolTest, ResetsMetadataButKeepsBufferMemory) {
  FramePool pool(1);
  const float* buffer = nullptr;
  {
    FramePool::Handle handle = pool.Acquire();
    handle->meta.frame_id = 99;
    handle->meta.wb_gains = {2.0f, 1.0f, 3.0f};
    handle->rgb.Resize(64, 48, 3);
    buffer = handle->rgb.pixels().data();
  }
  FramePool::Handle handle = pool.Acquire();
  EXPECT_EQ(handle->meta.frame_id, 0u);
  EXPECT_EQ(handle->meta.wb_gains[0], 1.0f);
  handle->rgb.Resize(64, 48, 3);
  EXPECT_EQ(handle->rgb.pixels().data(), buffer);  // No reallocation.
}

TEST(FramePoolTest, AcquireBlocksUntilAFrameIsReturned) {
  FramePool pool(1);
  FramePool::Handle held = pool.Acquire();
  std::atomic<bool> acquired{false};
  std::thread waiter([&] {
    FramePool::Handle h = pool.Acquire();
    acquired = static_cast<bool>(h);
  });
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(acquired);
  held.Release();
  waiter.join();
  EXPECT_TRUE(acquired);
}

TEST(FramePoolTest, CloseWakesBlockedAcquire) {
  FramePool pool(1);
  FramePool::Handle held = pool.Acquire();
  bool got_frame = true;
  std::thread waiter([&] { got_frame = static_cast<bool>(pool.Acquire()); });
  std::this_thread::sleep_for(20ms);
  pool.Close();
  waiter.join();
  EXPECT_FALSE(got_frame);
  EXPECT_FALSE(pool.TryAcquire());
  held.Release();  // Lent-out frames can still come back after Close().
  EXPECT_EQ(pool.available(), 1u);
}

// Frames flow producer -> queue -> consumers, as in the pipeline. The number
// of frames out at once must never exceed the pool size, and every frame
// must come back. Run under ThreadSanitizer.
TEST(FramePoolTest, StressNeverLendsMoreThanItsSize) {
  constexpr size_t kPoolSize = 4;
  constexpr int kFrames = 20000;
  constexpr int kConsumers = 3;
  FramePool pool(kPoolSize);
  BoundedQueue<FramePool::Handle> queue(2);
  std::atomic<int> outstanding{0};
  std::atomic<int> max_outstanding{0};
  std::atomic<int> consumed{0};

  std::vector<std::thread> consumers;
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&] {
      while (std::optional<FramePool::Handle> handle = queue.Pop()) {
        (*handle)->meta.frame_id += 1;  // Touch the frame.
        // Decrement before the handle returns the frame, so the counter
        // never under-reports frames that are out.
        outstanding.fetch_sub(1);
        handle->Release();
        consumed.fetch_add(1);
      }
    });
  }
  for (int i = 0; i < kFrames; ++i) {
    FramePool::Handle handle = pool.Acquire();
    ASSERT_TRUE(handle);
    const int now = outstanding.fetch_add(1) + 1;
    int prev = max_outstanding.load();
    while (now > prev && !max_outstanding.compare_exchange_weak(prev, now)) {
    }
    ASSERT_TRUE(queue.Push(std::move(handle)));
  }
  queue.Close();
  for (std::thread& t : consumers) t.join();

  EXPECT_EQ(consumed.load(), kFrames);
  EXPECT_LE(max_outstanding.load(), static_cast<int>(kPoolSize));
  EXPECT_EQ(pool.available(), kPoolSize);
}

}  // namespace
}  // namespace minicam
