// A fixed pool of reusable frames.

#ifndef MINICAM_FRAME_POOL_H_
#define MINICAM_FRAME_POOL_H_

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "minicam/frame.h"

namespace minicam {

// Owns a fixed number of frames and lends them out, so no memory is allocated
// per frame. A 12 MP frame holds about 216 MB of buffers; allocating that for
// every frame means mmap/munmap and page-zeroing on first touch, which costs
// time and causes latency spikes.
//
// Frames are lent as move-only Handles that give the frame back when they are
// destroyed (RAII), including on error paths, so frames cannot leak. When all
// frames are out, Acquire() blocks: the pool caps the number of frames in
// flight, and therefore memory.
//
// The pool must outlive every Handle; destroying it while frames are lent out
// aborts the program.
class FramePool {
 public:
  class Handle {
   public:
    Handle() = default;
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { Release(); }

    Frame& operator*() const { return *frame_; }
    Frame* operator->() const { return frame_; }
    Frame* get() const { return frame_; }
    explicit operator bool() const { return frame_ != nullptr; }

    // Returns the frame to the pool now. Safe to call more than once.
    void Release();

   private:
    friend class FramePool;
    Handle(FramePool* pool, Frame* frame) : pool_(pool), frame_(frame) {}

    FramePool* pool_ = nullptr;
    Frame* frame_ = nullptr;
  };

  explicit FramePool(size_t num_frames);
  FramePool(const FramePool&) = delete;
  FramePool& operator=(const FramePool&) = delete;
  ~FramePool();

  // Blocks until a frame is free. Returns an empty Handle if the pool is or
  // becomes closed. The frame's metadata is reset; its pixel buffers keep
  // their memory.
  Handle Acquire();

  // Like Acquire() but never blocks: returns an empty Handle if no frame is
  // free.
  Handle TryAcquire();

  // Wakes every blocked Acquire(), which then returns an empty Handle. Frames
  // already lent out can still be returned.
  void Close();

  // Undoes Close(), so the pool can be used for another run.
  void Reopen();

  size_t size() const { return frames_.size(); }
  size_t available() const;

 private:
  void Return(Frame* frame);
  Handle TakeLocked();  // Requires mu_ held and free_ non-empty.

  std::vector<std::unique_ptr<Frame>> frames_;  // Stable addresses.
  mutable std::mutex mu_;
  std::condition_variable frame_returned_;
  // Free frames, used as a LIFO stack: the most recently returned frame is
  // reused first because its memory is most likely still in cache.
  std::vector<Frame*> free_;
  bool closed_ = false;
};

}  // namespace minicam

#endif  // MINICAM_FRAME_POOL_H_
