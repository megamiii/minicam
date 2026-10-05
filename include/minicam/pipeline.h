// Single-threaded pipeline: runs an ordered list of stages on one frame.

#ifndef MINICAM_PIPELINE_H_
#define MINICAM_PIPELINE_H_

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "minicam/frame.h"
#include "minicam/stage.h"
#include "minicam/status.h"

namespace minicam {

class Pipeline {
 public:
  Pipeline() = default;
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&) = default;
  Pipeline& operator=(Pipeline&&) = default;

  // Appends a stage. Returns *this so calls can be chained.
  Pipeline& Add(std::unique_ptr<Stage> stage);

  // Runs every stage in order. Stops at the first failing stage and returns
  // its error, prefixed with the frame id and stage name.
  Status Process(Frame& frame);

  size_t size() const { return stages_.size(); }
  std::vector<std::string_view> StageNames() const;

 private:
  std::vector<std::unique_ptr<Stage>> stages_;
};

}  // namespace minicam

#endif  // MINICAM_PIPELINE_H_
