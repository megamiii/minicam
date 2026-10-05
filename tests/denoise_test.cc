#include "minicam/stages/denoise.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>

#include <gtest/gtest.h>

#include "minicam/metrics.h"
#include "test_scenes.h"

namespace minicam {
namespace {

Frame MakeRgbFrame(const Image<float>& rgb) {
  Frame frame;
  frame.rgb = rgb;
  return frame;
}

Image<float> AddNoise(const Image<float>& image, float sigma, uint32_t seed) {
  Image<float> noisy = image;
  std::mt19937 rng(seed);
  std::normal_distribution<float> normal(0.0f, sigma);
  for (float& v : noisy.pixels()) v += normal(rng);
  return noisy;
}

double StdDev(const Image<float>& image) {
  double sum = 0.0;
  double sum_sq = 0.0;
  for (float v : image.pixels()) {
    sum += v;
    sum_sq += static_cast<double>(v) * v;
  }
  const double n = static_cast<double>(image.size());
  const double mean = sum / n;
  return std::sqrt(sum_sq / n - mean * mean);
}

// Left half dark, right half bright.
Image<float> MakeStepEdge(int width, int height) {
  Image<float> image(width, height, 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c) {
        image.at(x, y, c) = x < width / 2 ? 0.2f : 0.8f;
      }
    }
  }
  return image;
}

TEST(BilateralDenoiseTest, FlatImageIsUnchanged) {
  const Image<float> flat = testing::MakeFlatImage(16, 12, {0.3f, 0.5f, 0.7f});
  Frame frame = MakeRgbFrame(flat);
  ASSERT_TRUE(BilateralDenoiseStage().Process(frame).ok());
  for (size_t i = 0; i < flat.size(); ++i) {
    ASSERT_NEAR(frame.rgb.pixels()[i], flat.pixels()[i], 1e-6f);
  }
}

TEST(BilateralDenoiseTest, ReducesNoiseInFlatRegions) {
  const Image<float> noisy = AddNoise(
      testing::MakeFlatImage(64, 64, {0.5f, 0.5f, 0.5f}), 0.02f, /*seed=*/1);
  Frame frame = MakeRgbFrame(noisy);
  ASSERT_TRUE(BilateralDenoiseStage().Process(frame).ok());
  EXPECT_LT(StdDev(frame.rgb), 0.5 * StdDev(noisy));
}

TEST(BilateralDenoiseTest, PreservesEdgesUnlikeGaussianBlur) {
  const Image<float> noisy = AddNoise(MakeStepEdge(32, 16), 0.01f, /*seed=*/2);

  Frame bilateral = MakeRgbFrame(noisy);
  ASSERT_TRUE(BilateralDenoiseStage().Process(bilateral).ok());

  // With a huge range sigma every neighbor counts: a plain Gaussian blur.
  BilateralOptions gaussian_options;
  gaussian_options.sigma_range = 1000.0f;
  Frame gaussian = MakeRgbFrame(noisy);
  ASSERT_TRUE(BilateralDenoiseStage(gaussian_options).Process(gaussian).ok());

  // Pixels directly left and right of the edge (x = 15 and 16).
  for (int y = 0; y < 16; ++y) {
    EXPECT_NEAR(bilateral.rgb.at(15, y, 1), 0.2f, 0.03f);
    EXPECT_NEAR(bilateral.rgb.at(16, y, 1), 0.8f, 0.03f);
    EXPECT_GT(gaussian.rgb.at(15, y, 1), 0.3f);
    EXPECT_LT(gaussian.rgb.at(16, y, 1), 0.7f);
  }
}

TEST(BilateralDenoiseTest, ImprovesPsnrOnNoisyScene) {
  const Image<float> clean = testing::MakeTestScene(128, 96);
  const Image<float> noisy = AddNoise(clean, 0.03f, /*seed=*/3);
  Frame frame = MakeRgbFrame(noisy);
  ASSERT_TRUE(BilateralDenoiseStage().Process(frame).ok());

  auto before = Psnr(noisy, clean);
  auto after = Psnr(frame.rgb, clean);
  ASSERT_TRUE(before.ok());
  ASSERT_TRUE(after.ok());
  std::cout << "bilateral on test scene, noise sigma 0.03: " << *before
            << " dB -> " << *after << " dB\n";
  EXPECT_GT(*after, *before + 3.0);
}

TEST(BilateralDenoiseTest, ReusesItsBuffersAcrossFrames) {
  BilateralDenoiseStage stage;
  Frame frame = MakeRgbFrame(testing::MakeTestScene(32, 32));
  ASSERT_TRUE(stage.Process(frame).ok());
  const float* first = frame.rgb.pixels().data();
  ASSERT_TRUE(stage.Process(frame).ok());
  ASSERT_TRUE(stage.Process(frame).ok());
  // Two buffers alternate (ping-pong), so after an even number of further
  // frames the original one is back.
  EXPECT_EQ(frame.rgb.pixels().data(), first);
}

TEST(BilateralDenoiseTest, RejectsInvalidOptions) {
  BilateralOptions options;
  options.sigma_range = 0.0f;
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(4, 4, {0.1f, 0.1f, 0.1f}));
  EXPECT_EQ(BilateralDenoiseStage(options).Process(frame).code(),
            StatusCode::kInvalidArgument);
}

TEST(BilateralDenoiseTest, RejectsMissingRgb) {
  Frame frame;
  EXPECT_EQ(BilateralDenoiseStage().Process(frame).code(),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
