// Performance benchmarks for minicam.
//
//   ./build/minicam_bench --benchmark_repetitions=3
//                         --benchmark_report_aggregates_only=true
//
// BM_Stage/<stage>: one stage on a 4048x3036 frame (the HDR+ sensor size).
//   Each iteration restores the stage's input with the timer paused, so only
//   Stage::Process is timed.
// BM_Pipeline/<mode>: a stream of frames through the whole ISP, sequential or
//   threaded. Reports throughput and per-frame latency (p50, p99), measured
//   from when the source starts a frame until the sink receives it.
//
// Inputs are synthetic (fixed seed), so the benchmark needs no dataset.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "minicam/frame.h"
#include "minicam/image.h"
#include "minicam/io/synthetic.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/color_correction.h"
#include "minicam/stages/demosaic.h"
#include "minicam/stages/denoise.h"
#include "minicam/stages/tone_map.h"
#include "minicam/stages/white_balance.h"
#include "minicam/threaded_pipeline.h"

namespace minicam {
namespace {

constexpr int kWidth = 4048;
constexpr int kHeight = 3036;

// A linear RGB scene with smooth regions, edges and fine detail, so that
// data-dependent stages (bilateral, tone map) do representative work.
Image<float> MakeScene(int width, int height) {
  Image<float> image(width, height, 3);
  const float two_pi = 2.0f * std::numbers::pi_v<float>;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float u = static_cast<float>(x) / width;
      const float v = static_cast<float>(y) / height;
      const bool block = ((x / 64) + (y / 64)) % 2 == 0;
      const float detail = 0.1f * std::sin(two_pi * 40.0f * u * v);
      image.at(x, y, 0) = 0.05f + 0.5f * u + (block ? 0.2f : 0.0f) + detail;
      image.at(x, y, 1) = 0.05f + 0.5f * v + detail;
      image.at(x, y, 2) = 0.3f + 0.2f * std::sin(two_pi * (u + v)) +
                          (block ? 0.0f : 0.15f);
    }
  }
  return image;
}

// A noisy synthetic RAW frame, as the camera would deliver it.
const Frame& RawFrame() {
  static const Frame* frame = [] {
    auto* f = new Frame();
    SyntheticRawOptions options;
    options.shot_noise = 0.002f;
    options.read_noise = 0.005f;
    options.seed = 7;
    if (!MosaicLinearRgb(MakeScene(kWidth, kHeight), options, *f).ok()) {
      std::abort();
    }
    return f;
  }();
  return *frame;
}

std::vector<std::unique_ptr<Stage>> MakeIspStages() {
  std::vector<std::unique_ptr<Stage>> stages;
  stages.push_back(std::make_unique<BlackLevelStage>());
  stages.push_back(std::make_unique<MalvarDemosaicStage>());
  stages.push_back(std::make_unique<WhiteBalanceStage>());
  stages.push_back(std::make_unique<ColorCorrectionStage>());
  stages.push_back(std::make_unique<ToneMapStage>());
  stages.push_back(std::make_unique<BilateralDenoiseStage>());
  return stages;
}

// Runs stage `index` of the ISP repeatedly on the frame produced by the stages
// before it.
void BM_Stage(benchmark::State& state, size_t index,
              std::function<std::unique_ptr<Stage>()> make_stage) {
  std::vector<std::unique_ptr<Stage>> stages = MakeIspStages();
  Frame input = RawFrame();
  for (size_t i = 0; i < index; ++i) {
    if (!stages[i]->Process(input).ok()) {
      state.SkipWithError("preparing the input failed");
      return;
    }
  }
  std::unique_ptr<Stage> stage = make_stage();
  Frame frame = input;
  for (auto _ : state) {
    state.PauseTiming();
    frame.meta = input.meta;
    frame.bayer = input.bayer;
    frame.rgb = input.rgb;
    state.ResumeTiming();
    if (!stage->Process(frame).ok()) {
      state.SkipWithError("stage failed");
      return;
    }
    benchmark::DoNotOptimize(frame.rgb.pixels().data());
    benchmark::ClobberMemory();
  }
  state.counters["MP/s"] = benchmark::Counter(
      static_cast<double>(kWidth) * kHeight / 1e6,
      benchmark::Counter::kIsIterationInvariantRate);
}

template <typename StageT>
std::function<std::unique_ptr<Stage>()> Make() {
  return [] { return std::make_unique<StageT>(); };
}

BENCHMARK_CAPTURE(BM_Stage, black_level, 0, Make<BlackLevelStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, demosaic_bilinear, 1, Make<BilinearDemosaicStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, demosaic_malvar, 1, Make<MalvarDemosaicStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, white_balance, 2, Make<WhiteBalanceStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, color_correction, 3, Make<ColorCorrectionStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, tone_map, 4, Make<ToneMapStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, denoise_bilateral, 5, Make<BilateralDenoiseStage>())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

// Tone mapping variants for the optimization record.
std::function<std::unique_ptr<Stage>()> MakeToneMap(
    ToneMapOptions::Implementation implementation, int num_threads) {
  return [=] {
    ToneMapOptions options;
    options.implementation = implementation;
    options.num_threads = num_threads;
    return std::make_unique<ToneMapStage>(options);
  };
}
BENCHMARK_CAPTURE(BM_Stage, tone_map_reference_1thread, 4,
                  MakeToneMap(ToneMapOptions::Implementation::kReference, 1))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, tone_map_fast_1thread, 4,
                  MakeToneMap(ToneMapOptions::Implementation::kFast, 1))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, tone_map_fast_all_threads, 4,
                  MakeToneMap(ToneMapOptions::Implementation::kFast, 0))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

// Bilateral variants for the optimization record.
std::function<std::unique_ptr<Stage>()> MakeBilateral(
    BilateralOptions::Implementation implementation, int num_threads) {
  return [=] {
    BilateralOptions options;
    options.implementation = implementation;
    options.num_threads = num_threads;
    return std::make_unique<BilateralDenoiseStage>(options);
  };
}
BENCHMARK_CAPTURE(BM_Stage, bilateral_reference_1thread, 5,
                  MakeBilateral(BilateralOptions::Implementation::kReference, 1))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, bilateral_lut_1thread, 5,
                  MakeBilateral(BilateralOptions::Implementation::kLookupTable,
                                1))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK_CAPTURE(BM_Stage, bilateral_lut_all_threads, 5,
                  MakeBilateral(BilateralOptions::Implementation::kLookupTable,
                                0))
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

double Percentile(std::vector<double> values, double p) {
  std::sort(values.begin(), values.end());
  const auto rank = static_cast<size_t>(
      std::ceil(p * static_cast<double>(values.size())) - 1.0);
  return values[std::min(rank, values.size() - 1)];
}

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Streams `num_frames` frames through the full ISP. The source copies a
// prepared RAW frame (standing in for the sensor), the sink records latency.
void BM_Pipeline(benchmark::State& state, bool threaded) {
  const int num_frames = static_cast<int>(state.range(0));
  const Frame& raw = RawFrame();
  std::vector<double> latencies_ms;
  double total_frames = 0.0;

  for (auto _ : state) {
    int produced = 0;
    auto source = [&](Frame& frame) -> StatusOr<bool> {
      if (produced++ == num_frames) return false;
      const int64_t start = frame.meta.timestamp_ns;
      frame.raw = raw.raw;
      frame.meta = raw.meta;
      frame.meta.timestamp_ns = start;
      return true;
    };
    auto sink = [&](Frame& frame) -> Status {
      latencies_ms.push_back(
          static_cast<double>(NowNs() - frame.meta.timestamp_ns) / 1e6);
      benchmark::DoNotOptimize(frame.rgb.pixels().data());
      return OkStatus();
    };

    Status status;
    if (threaded) {
      ThreadedPipeline pipeline;
      for (auto& stage : MakeIspStages()) pipeline.Add(std::move(stage));
      status = pipeline.Run(source, sink);
    } else {
      Pipeline pipeline;
      for (auto& stage : MakeIspStages()) pipeline.Add(std::move(stage));
      Frame frame;
      while (status.ok()) {
        frame.meta = FrameMetadata{};
        frame.meta.timestamp_ns = NowNs();
        StatusOr<bool> more = source(frame);
        if (!more.ok() || !*more) break;
        status = pipeline.Process(frame);
        if (status.ok()) status = sink(frame);
      }
    }
    if (!status.ok()) {
      state.SkipWithError(status.ToString().c_str());
      return;
    }
    total_frames += num_frames;
  }
  state.counters["frames/s"] =
      benchmark::Counter(total_frames, benchmark::Counter::kIsRate);
  state.counters["p50_ms"] = Percentile(latencies_ms, 0.50);
  state.counters["p99_ms"] = Percentile(latencies_ms, 0.99);
}

BENCHMARK_CAPTURE(BM_Pipeline, sequential, false)
    ->Arg(8)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime()
    ->Iterations(1);
BENCHMARK_CAPTURE(BM_Pipeline, threaded, true)
    ->Arg(8)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime()
    ->Iterations(1);

}  // namespace
}  // namespace minicam

BENCHMARK_MAIN();
