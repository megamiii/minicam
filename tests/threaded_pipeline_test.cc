#include "minicam/threaded_pipeline.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "minicam/io/synthetic.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/color_correction.h"
#include "minicam/stages/demosaic.h"
#include "minicam/stages/denoise.h"
#include "minicam/stages/tone_map.h"
#include "minicam/stages/white_balance.h"
#include "test_scenes.h"

namespace minicam {
namespace {

// Tracks how many stages are inside Process() at the same time.
struct Concurrency {
  std::atomic<int> active{0};
  std::atomic<int> max_active{0};
};

// Adds `value` to meta.exposure_time_s, optionally sleeping to simulate work.
class AddStage : public Stage {
 public:
  explicit AddStage(
      double value,
      std::chrono::milliseconds work = std::chrono::milliseconds(0),
      Concurrency* concurrency = nullptr)
      : value_(value), work_(work), concurrency_(concurrency) {}
  std::string_view Name() const override { return "add"; }
  Status Process(Frame& frame) override {
    if (concurrency_ != nullptr) {
      const int now = concurrency_->active.fetch_add(1) + 1;
      int prev = concurrency_->max_active.load();
      while (now > prev &&
             !concurrency_->max_active.compare_exchange_weak(prev, now)) {
      }
    }
    if (work_ > std::chrono::milliseconds(0))
      std::this_thread::sleep_for(work_);
    frame.meta.exposure_time_s += value_;
    if (concurrency_ != nullptr) concurrency_->active.fetch_sub(1);
    return OkStatus();
  }

 private:
  double value_;
  std::chrono::milliseconds work_;
  Concurrency* concurrency_;
};

class FailAtStage : public Stage {
 public:
  explicit FailAtStage(uint64_t frame_id) : frame_id_(frame_id) {}
  std::string_view Name() const override { return "fail_at"; }
  Status Process(Frame& frame) override {
    if (frame.meta.frame_id == frame_id_) return InvalidArgumentError("boom");
    return OkStatus();
  }

 private:
  uint64_t frame_id_;
};

// Produces `count` frames, then ends the stream.
ThreadedPipeline::FrameSource CountingSource(int count) {
  auto produced = std::make_shared<int>(0);
  return [produced, count](Frame& /*frame*/) -> StatusOr<bool> {
    return (*produced)++ < count;
  };
}

// Never ends; used to test errors and cancellation.
StatusOr<bool> EndlessSource(Frame& /*frame*/) { return true; }

TEST(ThreadedPipelineTest, ProcessesEveryFrameInOrder) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<AddStage>(1.0))
      .Add(std::make_unique<AddStage>(2.0))
      .Add(std::make_unique<AddStage>(3.0));
  uint64_t expected_id = 0;
  ASSERT_TRUE(pipeline
                  .Run(CountingSource(200),
                       [&](Frame& frame) {
                         EXPECT_EQ(frame.meta.frame_id, expected_id++);
                         EXPECT_EQ(frame.meta.exposure_time_s, 6.0);
                         EXPECT_GT(frame.meta.timestamp_ns, 0);
                         return OkStatus();
                       })
                  .ok());
  EXPECT_EQ(expected_id, 200u);
  EXPECT_EQ(pipeline.available_frames(), pipeline.pool_size());
}

TEST(ThreadedPipelineTest, WorksWithoutStages) {
  ThreadedPipeline pipeline;
  int received = 0;
  ASSERT_TRUE(pipeline
                  .Run(CountingSource(10),
                       [&](Frame& /*frame*/) {
                         ++received;
                         return OkStatus();
                       })
                  .ok());
  EXPECT_EQ(received, 10);
}

TEST(ThreadedPipelineTest, StagesRunConcurrently) {
  Concurrency concurrency;
  ThreadedPipeline pipeline;
  for (int i = 0; i < 3; ++i) {
    pipeline.Add(std::make_unique<AddStage>(1.0, std::chrono::milliseconds(10),
                                            &concurrency));
  }
  ASSERT_TRUE(
      pipeline
          .Run(CountingSource(12), [](Frame& /*frame*/) { return OkStatus(); })
          .ok());
  // A sequential pipeline would never have more than one stage active.
  EXPECT_GE(concurrency.max_active.load(), 2);
}

TEST(ThreadedPipelineTest, NeverUsesMoreFramesThanThePoolHolds) {
  ThreadedPipelineOptions options;
  options.pool_size = 3;
  ThreadedPipeline pipeline(options);
  pipeline.Add(std::make_unique<AddStage>(1.0))
      .Add(std::make_unique<AddStage>(1.0))
      .Add(std::make_unique<AddStage>(1.0));
  std::set<const Frame*> frames;
  ASSERT_TRUE(pipeline
                  .Run(CountingSource(100),
                       [&](Frame& frame) {
                         frames.insert(&frame);
                         return OkStatus();
                       })
                  .ok());
  EXPECT_EQ(pipeline.pool_size(), 3u);
  EXPECT_LE(frames.size(), 3u);  // Frames are recycled, not allocated.
}

TEST(ThreadedPipelineTest, StageErrorStopsThePipelineAndNamesTheStage) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<AddStage>(1.0))
      .Add(std::make_unique<FailAtStage>(5))
      .Add(std::make_unique<AddStage>(1.0));
  std::atomic<int> received{0};
  Status status = pipeline.Run(EndlessSource, [&](Frame& frame) {
    EXPECT_LT(frame.meta.frame_id, 5u);
    ++received;
    return OkStatus();
  });
  EXPECT_EQ(status.code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "frame 5, stage 'fail_at': boom");
  EXPECT_LE(received.load(), 5);
  // Every frame went back to the pool, including those left in queues.
  EXPECT_EQ(pipeline.available_frames(), pipeline.pool_size());
}

TEST(ThreadedPipelineTest, SourceErrorIsReported) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<AddStage>(1.0));
  int calls = 0;
  Status status = pipeline.Run(
      [&](Frame& /*frame*/) -> StatusOr<bool> {
        if (++calls == 4) return DataLossError("corrupt file");
        return true;
      },
      [](Frame& /*frame*/) { return OkStatus(); });
  EXPECT_EQ(status.code(), StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "source: corrupt file");
  EXPECT_EQ(pipeline.available_frames(), pipeline.pool_size());
}

TEST(ThreadedPipelineTest, SinkErrorIsReported) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<AddStage>(1.0));
  Status status = pipeline.Run(EndlessSource, [](Frame& frame) {
    if (frame.meta.frame_id == 3) return InternalError("disk full");
    return OkStatus();
  });
  EXPECT_EQ(status.code(), StatusCode::kInternal);
  EXPECT_EQ(status.message(), "sink: disk full");
  EXPECT_EQ(pipeline.available_frames(), pipeline.pool_size());
}

TEST(ThreadedPipelineTest, CancelStopsARunFromAnotherThread) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<AddStage>(1.0, std::chrono::milliseconds(1)));
  pipeline.Cancel();  // No active run: does nothing.

  std::thread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    pipeline.Cancel();
  });
  Status status =
      pipeline.Run(EndlessSource, [](Frame& /*frame*/) { return OkStatus(); });
  canceller.join();
  EXPECT_EQ(status.code(), StatusCode::kCancelled);
  EXPECT_EQ(pipeline.available_frames(), pipeline.pool_size());
}

TEST(ThreadedPipelineTest, CanRunAgainAfterAnError) {
  ThreadedPipeline pipeline;
  pipeline.Add(std::make_unique<FailAtStage>(2));
  EXPECT_FALSE(
      pipeline.Run(EndlessSource, [](Frame& /*frame*/) { return OkStatus(); })
          .ok());
  // Frame ids restart at 0, so frame 2 fails again; 2 frames get through.
  int received = 0;
  Status status = pipeline.Run(CountingSource(2), [&](Frame& /*frame*/) {
    ++received;
    return OkStatus();
  });
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_EQ(received, 2);
}

// The real ISP, threaded vs single-threaded: results must be bit-identical.
TEST(ThreadedPipelineTest, MatchesSingleThreadedPipelineOnTheIsp) {
  constexpr int kFrames = 6;
  const Image<float> scene = testing::MakeTestScene(64, 48);
  auto make_input = [&](uint64_t id, Frame& frame) {
    SyntheticRawOptions options;
    options.read_noise = 0.01f;
    options.shot_noise = 0.002f;
    options.seed = static_cast<uint32_t>(id + 1);
    const FrameMetadata kept = frame.meta;
    EXPECT_TRUE(MosaicLinearRgb(scene, options, frame).ok());
    frame.meta.frame_id = kept.frame_id;
    frame.meta.timestamp_ns = kept.timestamp_ns;
  };
  auto add_isp = [](auto& pipeline) {
    pipeline.Add(std::make_unique<BlackLevelStage>())
        .Add(std::make_unique<MalvarDemosaicStage>())
        .Add(std::make_unique<WhiteBalanceStage>())
        .Add(std::make_unique<ColorCorrectionStage>())
        .Add(std::make_unique<ToneMapStage>())
        .Add(std::make_unique<BilateralDenoiseStage>());
  };

  std::vector<Image<float>> expected;
  Pipeline sequential;
  add_isp(sequential);
  for (int i = 0; i < kFrames; ++i) {
    Frame frame;
    make_input(static_cast<uint64_t>(i), frame);
    ASSERT_TRUE(sequential.Process(frame).ok());
    expected.push_back(frame.rgb);
  }

  ThreadedPipeline threaded;
  add_isp(threaded);
  std::map<uint64_t, Image<float>> results;
  ASSERT_TRUE(
      threaded
          .Run(
              [&, produced = 0](Frame& frame) mutable -> StatusOr<bool> {
                if (produced++ == kFrames) return false;
                make_input(frame.meta.frame_id, frame);
                return true;
              },
              [&](Frame& frame) {
                results[frame.meta.frame_id] = frame.rgb;
                return OkStatus();
              })
          .ok());

  ASSERT_EQ(results.size(), static_cast<size_t>(kFrames));
  for (int i = 0; i < kFrames; ++i) {
    const Image<float>& got = results[static_cast<uint64_t>(i)];
    ASSERT_EQ(got.size(), expected[i].size());
    for (size_t p = 0; p < got.size(); ++p) {
      ASSERT_EQ(got.pixels()[p], expected[i].pixels()[p])
          << "frame " << i << " element " << p;
    }
  }
}

}  // namespace
}  // namespace minicam
