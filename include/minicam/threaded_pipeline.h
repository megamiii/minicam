// Multi-threaded pipeline: one thread per stage, connected by bounded queues.

#ifndef MINICAM_THREADED_PIPELINE_H_
#define MINICAM_THREADED_PIPELINE_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "minicam/bounded_queue.h"
#include "minicam/frame.h"
#include "minicam/frame_pool.h"
#include "minicam/stage.h"
#include "minicam/status.h"

namespace minicam {

struct ThreadedPipelineOptions {
  // Capacity of each queue between two threads.
  size_t queue_capacity = 2;
  // Number of frames in the pool. 0 means number of stages + 2: one frame
  // per busy stage, one being filled by the source, one at the sink.
  size_t pool_size = 0;
};

// Runs stages concurrently, like a simplified camera HAL:
//
//   pool -> source thread -> q0 -> stage 1 -> q1 -> ... -> qK -> sink thread
//     ^                                                             |
//     +---------------- frames return to the pool ------------------+
//
// Several frames are in flight at once, so throughput is limited by the
// slowest stage rather than the sum of all stages. Frames leave in the order
// they entered, because every stage is one thread reading a FIFO queue.
//
// Shutdown: when the source reports end of stream, closing q0 makes each
// stage drain its input, close its output and exit, so no frame is lost.
// Errors: the first error from the source, any stage or the sink is kept;
// all queues and the pool are closed so every thread exits promptly, and
// Run() returns that error.
class ThreadedPipeline {
 public:
  // Fills `frame` (frame.raw and frame.meta) with the next input. Returns
  // true if a frame was produced, false at end of stream, or an error.
  // frame.meta.frame_id and timestamp_ns are preset to a sequence number and
  // the monotonic clock; the source may overwrite them.
  using FrameSource = std::function<StatusOr<bool>(Frame& frame)>;
  // Consumes a processed frame. The frame returns to the pool afterwards.
  using FrameSink = std::function<Status(Frame& frame)>;

  explicit ThreadedPipeline(ThreadedPipelineOptions options = {});
  // Not copyable or movable: worker threads refer to the pipeline.
  ThreadedPipeline(const ThreadedPipeline&) = delete;
  ThreadedPipeline& operator=(const ThreadedPipeline&) = delete;
  ThreadedPipeline(ThreadedPipeline&&) = delete;
  ThreadedPipeline& operator=(ThreadedPipeline&&) = delete;
  ~ThreadedPipeline();

  // Appends a stage. Must not be called while Run() is active, nor after the
  // first Run() (the pool size depends on the number of stages).
  ThreadedPipeline& Add(std::unique_ptr<Stage> stage);

  // Processes frames from `source` until end of stream or the first error,
  // passing each result to `sink`. Blocks until all threads have exited.
  // Calls to Run() must not overlap.
  Status Run(const FrameSource& source, const FrameSink& sink);

  // Stops the active Run() (if any), which then returns a CANCELLED error.
  // Thread-safe.
  void Cancel();

  size_t num_stages() const { return stages_.size(); }
  size_t pool_size() const;
  // Frames currently in the pool (all of them whenever Run() is not active).
  // For diagnostics and tests.
  size_t available_frames() const;

 private:
  using FrameQueue = BoundedQueue<FramePool::Handle>;

  void SourceLoop(const FrameSource& source, FrameQueue& out);
  void StageLoop(Stage& stage, FrameQueue& in, FrameQueue& out);
  void SinkLoop(const FrameSink& sink, FrameQueue& in);

  // Records `status` if it is the first error and aborts the run.
  void Fail(Status status);

  ThreadedPipelineOptions options_;
  std::vector<std::unique_ptr<Stage>> stages_;
  std::unique_ptr<FramePool> pool_;  // Created by the first Run().

  // Guards queues_, first_error_ and running_. Workers never hold it while
  // blocked in a queue or the pool.
  std::mutex mu_;
  std::vector<std::unique_ptr<FrameQueue>> queues_;
  Status first_error_;
  bool running_ = false;
  std::atomic<bool> aborted_{false};
};

}  // namespace minicam

#endif  // MINICAM_THREADED_PIPELINE_H_
