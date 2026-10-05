#include "minicam/threaded_pipeline.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace minicam {
namespace {

int64_t MonotonicNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

Status WithPrefix(const std::string& prefix, const Status& status) {
  return {status.code(), prefix + status.message()};
}

}  // namespace

ThreadedPipeline::ThreadedPipeline(ThreadedPipelineOptions options)
    : options_(options) {
  if (options_.queue_capacity < 1) options_.queue_capacity = 1;
}

// Run() joins its threads before returning, so nothing is running here.
ThreadedPipeline::~ThreadedPipeline() = default;

ThreadedPipeline& ThreadedPipeline::Add(std::unique_ptr<Stage> stage) {
  stages_.push_back(std::move(stage));
  return *this;
}

size_t ThreadedPipeline::pool_size() const {
  if (pool_ != nullptr) return pool_->size();
  return options_.pool_size > 0 ? options_.pool_size : stages_.size() + 2;
}

size_t ThreadedPipeline::available_frames() const {
  return pool_ != nullptr ? pool_->available() : pool_size();
}

Status ThreadedPipeline::Run(const FrameSource& source, const FrameSink& sink) {
  if (!source || !sink) return InvalidArgumentError("source and sink required");
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (running_) return FailedPreconditionError("Run() is already active");
    if (pool_ == nullptr) pool_ = std::make_unique<FramePool>(pool_size());
    pool_->Reopen();  // A previous run may have ended with an abort.
    running_ = true;
    first_error_ = OkStatus();
    aborted_ = false;
    queues_.clear();
    // One queue in front of every stage, plus one in front of the sink.
    for (size_t i = 0; i <= stages_.size(); ++i) {
      queues_.push_back(std::make_unique<FrameQueue>(options_.queue_capacity));
    }
  }

  {
    // queues_ is not resized until every thread has been joined, so each
    // thread can hold plain references to its two queues.
    std::vector<std::jthread> threads;
    threads.reserve(stages_.size() + 2);
    FrameQueue& first = *queues_.front();
    FrameQueue& last = *queues_.back();
    threads.emplace_back(
        [this, &source, &first] { SourceLoop(source, first); });
    for (size_t i = 0; i < stages_.size(); ++i) {
      threads.emplace_back(
          [this, &stage = *stages_[i], &in = *queues_[i],
           &out = *queues_[i + 1]] { StageLoop(stage, in, out); });
    }
    threads.emplace_back([this, &sink, &last] { SinkLoop(sink, last); });
  }  // jthread destructors join every thread here.

  std::lock_guard<std::mutex> lock(mu_);
  // After an abort, frames may still sit in the queues; destroying the queues
  // destroys their handles, which returns the frames to the pool.
  queues_.clear();
  running_ = false;
  return first_error_;
}

void ThreadedPipeline::Cancel() { Fail(CancelledError("pipeline cancelled")); }

void ThreadedPipeline::Fail(Status status) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_) return;
  if (first_error_.ok()) first_error_ = std::move(status);
  aborted_ = true;
  // Wake every thread blocked in a queue or in the pool so it can exit.
  pool_->Close();
  for (const std::unique_ptr<FrameQueue>& queue : queues_) queue->Close();
}

void ThreadedPipeline::SourceLoop(const FrameSource& source, FrameQueue& out) {
  uint64_t next_id = 0;
  while (!aborted_) {
    FramePool::Handle frame = pool_->Acquire();  // Blocks while pool is empty.
    if (!frame) break;                           // Pool closed by an abort.
    frame->meta.frame_id = next_id++;
    frame->meta.timestamp_ns = MonotonicNowNs();

    StatusOr<bool> produced = source(*frame);
    if (!produced.ok()) {
      Fail(WithPrefix("source: ", produced.status()));
      break;
    }
    if (!*produced) break;  // End of stream; the unused frame goes back.
    if (!out.Push(std::move(frame))) break;  // Queue closed by an abort.
  }
  out.Close();  // End of stream travels down the pipeline.
}

void ThreadedPipeline::StageLoop(Stage& stage, FrameQueue& in,
                                 FrameQueue& out) {
  while (std::optional<FramePool::Handle> frame = in.Pop()) {
    if (aborted_) break;  // Discard frames drained after an abort.
    Status status = stage.Process(**frame);
    if (!status.ok()) {
      Fail(WithPrefix("frame " + std::to_string((*frame)->meta.frame_id) +
                          ", stage '" + std::string(stage.Name()) + "': ",
                      status));
      break;
    }
    if (!out.Push(std::move(*frame))) break;
  }
  out.Close();
}

void ThreadedPipeline::SinkLoop(const FrameSink& sink, FrameQueue& in) {
  while (std::optional<FramePool::Handle> frame = in.Pop()) {
    if (aborted_) break;
    Status status = sink(**frame);
    if (!status.ok()) {
      Fail(WithPrefix("sink: ", status));
      break;
    }
  }  // Each handle is destroyed here, returning its frame to the pool.
}

}  // namespace minicam
