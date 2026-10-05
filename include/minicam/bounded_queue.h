// A bounded, blocking, thread-safe FIFO queue.

#ifndef MINICAM_BOUNDED_QUEUE_H_
#define MINICAM_BOUNDED_QUEUE_H_

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace minicam {

// Connects a producer thread to a consumer thread (any number of each).
//
// - Bounded: Push blocks while the queue is full. This back-pressure limits
//   the number of frames in flight and therefore memory and latency.
// - Close(): after it, Push fails and Pop keeps returning the remaining items,
//   then std::nullopt. This lets end-of-stream pass down a chain of queues
//   without losing frames.
// - Storage is a fixed ring buffer allocated in the constructor, so Push and
//   Pop never allocate.
//
// T only needs to be movable (e.g. a move-only frame handle).
template <typename T>
class BoundedQueue {
 public:
  // `capacity` must be at least 1.
  explicit BoundedQueue(size_t capacity)
      : slots_(capacity < 1 ? 1 : capacity) {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  // Blocks while the queue is full. Returns false (and drops `item`) if the
  // queue is or becomes closed before there is room.
  bool Push(T item) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      // The predicate is re-checked after every wake-up, which also handles
      // spurious wake-ups.
      not_full_.wait(lock, [&] { return count_ < slots_.size() || closed_; });
      if (closed_) return false;
      slots_[(head_ + count_) % slots_.size()].emplace(std::move(item));
      ++count_;
    }
    // Notify after unlocking so the woken consumer can take the lock at once.
    not_empty_.notify_one();
    return true;
  }

  // Blocks while the queue is empty and open. Returns std::nullopt once the
  // queue is closed and every remaining item has been taken.
  std::optional<T> Pop() {
    std::optional<T> item;
    {
      std::unique_lock<std::mutex> lock(mu_);
      not_empty_.wait(lock, [&] { return count_ > 0 || closed_; });
      if (count_ == 0) return std::nullopt;  // Closed and drained.
      item = std::move(slots_[head_]);
      slots_[head_].reset();
      head_ = (head_ + 1) % slots_.size();
      --count_;
    }
    not_full_.notify_one();
    return item;
  }

  // Idempotent. Wakes every blocked Push and Pop.
  void Close() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      closed_ = true;
    }
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_;
  }
  size_t size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return count_;
  }
  size_t capacity() const { return slots_.size(); }

 private:
  mutable std::mutex mu_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  std::vector<std::optional<T>> slots_;  // Ring buffer.
  size_t head_ = 0;                      // Index of the oldest item.
  size_t count_ = 0;
  bool closed_ = false;
};

}  // namespace minicam

#endif  // MINICAM_BOUNDED_QUEUE_H_
