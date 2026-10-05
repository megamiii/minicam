// The common interface implemented by every ISP stage.

#ifndef MINICAM_STAGE_H_
#define MINICAM_STAGE_H_

#include <string_view>

#include "minicam/frame.h"
#include "minicam/status.h"

namespace minicam {

// A stage reads and writes buffers of a Frame. Stages communicate only through
// the Frame, so they can be added, removed or reordered freely as long as the
// buffer a stage reads has been produced by an earlier stage.
//
// Threading contract: a Stage instance is only ever called from one thread at
// a time (one thread per stage in the threaded pipeline), so a stage may keep
// scratch buffers as members without locking.
class Stage {
 public:
  virtual ~Stage() = default;

  // Short identifier used in logs, errors and benchmarks.
  virtual std::string_view Name() const = 0;

  // Processes one frame. A non-OK result stops the frame and is reported by
  // the pipeline.
  virtual Status Process(Frame& frame) = 0;
};

}  // namespace minicam

#endif  // MINICAM_STAGE_H_
