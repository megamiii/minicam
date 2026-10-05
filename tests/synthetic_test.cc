#include "minicam/io/synthetic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "minicam/cfa.h"
#include "minicam/stages/black_level.h"
#include "test_scenes.h"

namespace minicam {
namespace {

TEST(SyntheticRawTest, WritesSensorMetadata) {
  SyntheticRawOptions options;
  options.cfa = CfaPattern::kGbrg;
  options.black_level = 100;
  options.white_level = 4000;
  Frame frame;
  ASSERT_TRUE(
      MosaicLinearRgb(testing::MakeTestScene(8, 6), options, frame).ok());

  EXPECT_EQ(frame.raw.width(), 8);
  EXPECT_EQ(frame.raw.height(), 6);
  EXPECT_EQ(frame.meta.cfa, CfaPattern::kGbrg);
  EXPECT_EQ(frame.meta.white_level, 4000);
  for (uint16_t b : frame.meta.black_level) EXPECT_EQ(b, 100);
}

// Mosaic followed by black-level correction must give back, at every pixel,
// the ground-truth value of that pixel's CFA color (up to quantization).
TEST(SyntheticRawTest, BlackLevelRecoversTruthAtCfaSites) {
  const Image<float> truth = testing::MakeTestScene(64, 48);
  for (CfaPattern cfa : {CfaPattern::kRggb, CfaPattern::kBggr,
                         CfaPattern::kGrbg, CfaPattern::kGbrg}) {
    SyntheticRawOptions options;
    options.cfa = cfa;
    Frame frame;
    ASSERT_TRUE(MosaicLinearRgb(truth, options, frame).ok());
    ASSERT_TRUE(BlackLevelStage().Process(frame).ok());

    const float half_step =
        0.5f / static_cast<float>(options.white_level - options.black_level);
    for (int y = 0; y < truth.height(); ++y) {
      for (int x = 0; x < truth.width(); ++x) {
        const float expected = truth.at(x, y, CfaColorAt(cfa, x, y));
        ASSERT_NEAR(frame.bayer.at(x, y), expected, half_step + 1e-6f)
            << CfaPatternName(cfa) << " at (" << x << ", " << y << ")";
      }
    }
  }
}

TEST(SyntheticRawTest, NoiseHasExpectedStandardDeviation) {
  constexpr float kSignal = 0.25f;
  SyntheticRawOptions options;
  options.white_level = 16383;  // 14-bit, so quantization noise is negligible.
  options.shot_noise = 0.004f;
  options.read_noise = 0.01f;
  Frame frame;
  ASSERT_TRUE(MosaicLinearRgb(
                  testing::MakeFlatImage(256, 256, {kSignal, kSignal, kSignal}),
                  options, frame)
                  .ok());
  ASSERT_TRUE(BlackLevelStage().Process(frame).ok());

  double sum = 0.0;
  double sum_sq = 0.0;
  for (float v : frame.bayer.pixels()) {
    sum += v;
    sum_sq += static_cast<double>(v) * v;
  }
  const double n = static_cast<double>(frame.bayer.size());
  const double mean = sum / n;
  const double stddev = std::sqrt(sum_sq / n - mean * mean);
  const double expected_stddev = std::sqrt(
      options.shot_noise * kSignal + options.read_noise * options.read_noise);

  EXPECT_NEAR(mean, kSignal, 0.002);
  EXPECT_NEAR(stddev, expected_stddev, 0.05 * expected_stddev);
}

TEST(SyntheticRawTest, SameSeedGivesSameNoise) {
  SyntheticRawOptions options;
  options.read_noise = 0.02f;
  options.seed = 42;
  const Image<float> scene = testing::MakeTestScene(32, 32);
  Frame a;
  Frame b;
  ASSERT_TRUE(MosaicLinearRgb(scene, options, a).ok());
  ASSERT_TRUE(MosaicLinearRgb(scene, options, b).ok());
  EXPECT_TRUE(std::equal(a.raw.pixels().begin(), a.raw.pixels().end(),
                         b.raw.pixels().begin()));
}

TEST(SyntheticRawTest, RejectsOddDimensions) {
  Frame frame;
  EXPECT_EQ(MosaicLinearRgb(testing::MakeTestScene(7, 8), {}, frame).code(),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
