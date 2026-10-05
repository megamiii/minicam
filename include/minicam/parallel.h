// Data-parallel helper for splitting one image across threads.

#ifndef MINICAM_PARALLEL_H_
#define MINICAM_PARALLEL_H_

#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

namespace minicam {

// Number of threads to use when a stage is asked for `requested` threads:
// 0 means one per hardware thread.
inline int ResolveThreadCount(int requested) {
  if (requested > 0) return requested;
  return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

// Splits [0, count) into `num_threads` contiguous bands and calls
// fn(begin, end) for each band on its own thread (the calling thread takes
// the last band). Returns when every band is done. `fn` must only write data
// that belongs to its own band.
//
// Threads are created per call. For work items of several milliseconds, as
// for an image stage, the ~20-50 us per thread is negligible; a persistent
// pool would matter only for much smaller work items.
inline void ParallelFor(int count, int num_threads,
                        const std::function<void(int begin, int end)>& fn) {
  const int threads = std::clamp(num_threads, 1, std::max(count, 1));
  if (threads == 1) {
    fn(0, count);
    return;
  }
  std::vector<std::jthread> workers;
  workers.reserve(threads - 1);
  for (int t = 0; t < threads - 1; ++t) {
    const int begin = count * t / threads;
    const int end = count * (t + 1) / threads;
    workers.emplace_back([&fn, begin, end] { fn(begin, end); });
  }
  fn(count * (threads - 1) / threads, count);
}  // jthread destructors join here.

}  // namespace minicam

#endif  // MINICAM_PARALLEL_H_
