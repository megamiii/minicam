#include "minicam/hog.h"

#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace minicam {
namespace {

TEST(HogTest, OrientationBinsAreUnsignedAndCentered) {
  EXPECT_EQ(OrientationBin(1.0f, 0.0f), 0);   // 0 degrees.
  EXPECT_EQ(OrientationBin(-1.0f, 0.0f), 0);  // 180 folds onto 0.
  EXPECT_EQ(OrientationBin(1.0f, 1.0f), 2);   // 45: edge between bins 1/2.
  EXPECT_EQ(OrientationBin(0.0f, 1.0f), 3);   // 90.
  EXPECT_EQ(OrientationBin(0.0f, -1.0f), 3);  // -90 folds onto 90.
  EXPECT_EQ(OrientationBin(1.0f, -1.0f), 5);  // -45 -> 135, bin [135, 165).
  const float deg = 3.14159265f / 180.0f;
  EXPECT_EQ(OrientationBin(std::cos(14.0f * deg), std::sin(14.0f * deg)), 0);
  EXPECT_EQ(OrientationBin(std::cos(16.0f * deg), std::sin(16.0f * deg)), 1);
  EXPECT_EQ(OrientationBin(std::cos(170.0f * deg), std::sin(170.0f * deg)), 0);
}

Image<float> StepImage(int width, int height, bool vertical_edge) {
  Image<float> image(width, height, 1);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool bright = vertical_edge ? x >= width / 2 : y >= height / 2;
      image.at(x, y) = bright ? 1.0f : 0.0f;
    }
  }
  return image;
}

HogHistogram WholeImageHistogram(const Image<float>& image) {
  GradientField gradients;
  ComputeGradients(image, gradients);
  IntegralHistogram integral;
  integral.Build(gradients);
  return integral.Sum(0, 0, image.width(), image.height());
}

TEST(HogTest, VerticalEdgeFillsOnlyBinZero) {
  const HogHistogram h = WholeImageHistogram(StepImage(32, 32, true));
  EXPECT_GT(h[0], 0.0f);
  for (int b = 1; b < kHogBins; ++b) EXPECT_EQ(h[b], 0.0f) << "bin " << b;
}

TEST(HogTest, HorizontalEdgeFillsOnlyBinThree) {
  const HogHistogram h = WholeImageHistogram(StepImage(32, 32, false));
  EXPECT_GT(h[3], 0.0f);
  for (int b = 0; b < kHogBins; ++b) {
    if (b != 3) {
      EXPECT_EQ(h[b], 0.0f) << "bin " << b;
    }
  }
}

Image<float> NoiseImage(int width, int height, uint32_t seed) {
  Image<float> image(width, height, 1);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
  for (float& v : image.pixels()) v = uniform(rng);
  return image;
}

// The integral histogram must equal a direct sum over any rectangle,
// including ones not aligned to the cell grid.
TEST(HogTest, IntegralHistogramMatchesBruteForce) {
  const Image<float> image = NoiseImage(50, 37, 1);
  GradientField gradients;
  ComputeGradients(image, gradients);
  IntegralHistogram integral;
  integral.Build(gradients);

  struct Rect {
    int x, y, w, h;
  };
  for (const Rect& r : {Rect{0, 0, 8, 8}, Rect{3, 5, 8, 8}, Rect{13, 7, 21, 30},
                        Rect{0, 0, 50, 37}}) {
    double expected[kHogBins] = {};
    for (int y = r.y; y < r.y + r.h; ++y) {
      for (int x = r.x; x < r.x + r.w; ++x) {
        expected[gradients.bin.at(x, y)] += gradients.magnitude.at(x, y);
      }
    }
    const HogHistogram got = integral.Sum(r.x, r.y, r.w, r.h);
    for (int b = 0; b < kHogBins; ++b) {
      EXPECT_NEAR(got[b], expected[b], 1e-3)
          << "rect at " << r.x << "," << r.y << " bin " << b;
    }
  }
}

TEST(HogTest, DescriptorLayoutAndBlockNormalization) {
  HogParams params;  // 8-pixel cells, 2x2-cell blocks.
  // 48x48: 6x6 cells, 5x5 blocks of 4 cells x 6 bins.
  EXPECT_EQ(DescriptorLength(48, 48, params), 5 * 5 * 24);
  EXPECT_EQ(DescriptorLength(15, 48, params), 0);  // Less than one block.

  const std::vector<float> hog = ExtractHog(NoiseImage(48, 48, 2), params);
  ASSERT_EQ(hog.size(), 600u);
  for (size_t block = 0; block < 25; ++block) {
    double sum_sq = 0.0;
    for (size_t i = 0; i < 24; ++i) {
      sum_sq += static_cast<double>(hog[block * 24 + i]) * hog[block * 24 + i];
    }
    EXPECT_NEAR(std::sqrt(sum_sq), 1.0, 1e-4) << "block " << block;
  }
}

}  // namespace
}  // namespace minicam
