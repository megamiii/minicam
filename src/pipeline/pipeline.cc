#include "minicam/pipeline.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minicam {

Pipeline& Pipeline::Add(std::unique_ptr<Stage> stage) {
  stages_.push_back(std::move(stage));
  return *this;
}

Status Pipeline::Process(Frame& frame, std::vector<double>* stage_ms) {
  if (stage_ms != nullptr) stage_ms->clear();
  for (const std::unique_ptr<Stage>& stage : stages_) {
    const auto start = std::chrono::steady_clock::now();
    Status status = stage->Process(frame);
    if (stage_ms != nullptr) {
      stage_ms->push_back(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count());
    }
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
