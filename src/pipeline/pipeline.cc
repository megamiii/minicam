#include "minicam/pipeline.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minicam {

Pipeline& Pipeline::Add(std::unique_ptr<Stage> stage) {
  stages_.push_back(std::move(stage));
  return *this;
}

Status Pipeline::Process(Frame& frame) {
  for (const std::unique_ptr<Stage>& stage : stages_) {
    Status status = stage->Process(frame);
    if (!status.ok()) {
      return {status.code(), "frame " + std::to_string(frame.meta.frame_id) +
                                 ", stage '" + std::string(stage->Name()) +
                                 "': " + status.message()};
    }
  }
  return OkStatus();
}

std::vector<std::string_view> Pipeline::StageNames() const {
  std::vector<std::string_view> names;
  names.reserve(stages_.size());
  for (const std::unique_ptr<Stage>& stage : stages_) {
    names.push_back(stage->Name());
  }
  return names;
}

}  // namespace minicam
