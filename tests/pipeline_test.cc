#include "minicam/pipeline.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace minicam {
namespace {

// Records its name into a shared log so tests can check the call order.
class RecordingStage : public Stage {
 public:
  RecordingStage(std::string name, std::vector<std::string>* log)
      : name_(std::move(name)), log_(log) {}
  std::string_view Name() const override { return name_; }
  Status Process(Frame& /*frame*/) override {
    log_->push_back(name_);
    return OkStatus();
  }

 private:
  std::string name_;
  std::vector<std::string>* log_;
};

class FailingStage : public Stage {
 public:
  std::string_view Name() const override { return "failing"; }
  Status Process(Frame& /*frame*/) override {
    return InvalidArgumentError("broken input");
  }
};

TEST(PipelineTest, RunsStagesInOrder) {
  std::vector<std::string> log;
  Pipeline pipeline;
  pipeline.Add(std::make_unique<RecordingStage>("a", &log))
      .Add(std::make_unique<RecordingStage>("b", &log))
      .Add(std::make_unique<RecordingStage>("c", &log));

  Frame frame;
  ASSERT_TRUE(pipeline.Process(frame).ok());
  EXPECT_EQ(log, (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(pipeline.StageNames(),
            (std::vector<std::string_view>{"a", "b", "c"}));
}

TEST(PipelineTest, ReportsOneTimingPerStage) {
  std::vector<std::string> log;
  Pipeline pipeline;
  pipeline.Add(std::make_unique<RecordingStage>("a", &log))
      .Add(std::make_unique<RecordingStage>("b", &log));
  Frame frame;
  std::vector<double> stage_ms = {123.0};  // Stale content is replaced.
  ASSERT_TRUE(pipeline.Process(frame, &stage_ms).ok());
  ASSERT_EQ(stage_ms.size(), 2u);
  for (double ms : stage_ms) EXPECT_GE(ms, 0.0);
}

TEST(PipelineTest, EmptyPipelineIsOk) {
  Pipeline pipeline;
  Frame frame;
  EXPECT_TRUE(pipeline.Process(frame).ok());
}

TEST(PipelineTest, StopsAtFirstErrorAndNamesTheStage) {
  std::vector<std::string> log;
  Pipeline pipeline;
  pipeline.Add(std::make_unique<RecordingStage>("before", &log))
      .Add(std::make_unique<FailingStage>())
      .Add(std::make_unique<RecordingStage>("after", &log));

  Frame frame;
  frame.meta.frame_id = 7;
  Status status = pipeline.Process(frame);

  EXPECT_EQ(status.code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "frame 7, stage 'failing': broken input");
  EXPECT_EQ(log, (std::vector<std::string>{"before"}));
}

}  // namespace
}  // namespace minicam
