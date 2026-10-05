#include "minicam/frame_pool.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <utility>

namespace minicam {

FramePool::Handle::Handle(Handle&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      frame_(std::exchange(other.frame_, nullptr)) {}

FramePool::Handle& FramePool::Handle::operator=(Handle&& other) noexcept {
  if (this != &other) {
    Release();
    pool_ = std::exchange(other.pool_, nullptr);
    frame_ = std::exchange(other.frame_, nullptr);
  }
  return *this;
}

void FramePool::Handle::Release() {
  if (frame_ != nullptr) {
    pool_->Return(frame_);
    frame_ = nullptr;
    pool_ = nullptr;
  }
}

FramePool::FramePool(size_t num_frames) {
  frames_.reserve(num_frames);
  free_.reserve(num_frames);
  for (size_t i = 0; i < num_frames; ++i) {
    frames_.push_back(std::make_unique<Frame>());
    free_.push_back(frames_.back().get());
  }
}

FramePool::~FramePool() {
  // A Handle outliving its pool would later write to freed memory. Fail
  // loudly instead.
  if (free_.size() != frames_.size()) {
    std::fprintf(stderr,
                 "FramePool destroyed while %zu of %zu frames are lent out\n",
                 frames_.size() - free_.size(), frames_.size());
    std::abort();
  }
}

FramePool::Handle FramePool::TakeLocked() {
  Frame* frame = free_.back();
  free_.pop_back();
  frame->meta = FrameMetadata{};
  return Handle(this, frame);
}

FramePool::Handle FramePool::Acquire() {
  std::unique_lock<std::mutex> lock(mu_);
  frame_returned_.wait(lock, [&] { return !free_.empty() || closed_; });
  if (closed_) return {};
  return TakeLocked();
}

FramePool::Handle FramePool::TryAcquire() {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_ || free_.empty()) return {};
  return TakeLocked();
}

void FramePool::Close() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
  }
  frame_returned_.notify_all();
}

size_t FramePool::available() const {
  std::lock_guard<std::mutex> lock(mu_);
  return free_.size();
}

void FramePool::Return(Frame* frame) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    free_.push_back(frame);
  }
  frame_returned_.notify_one();
}

}  // namespace minicam
