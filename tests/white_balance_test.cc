#include "minicam/stages/white_balance.h"

#include <array>

#include <gtest/gtest.h>

#include "test_scenes.h"

namespace minicam {
namespace {

Frame MakeRgbFrame(const Image<float>& rgb) {
  Frame frame;
  frame.rgb = rgb;
  return frame;
}

void ExpectNeutral(const Image<float>& rgb, float tolerance) {
  for (int y = 0; y < rgb.height(); ++y) {
    for (int x = 0; x < rgb.width(); ++x) {
      ASSERT_NEAR(rgb.at(x, y, 0), rgb.at(x, y, 1), tolerance);
      ASSERT_NEAR(rgb.at(x, y, 2), rgb.at(x, y, 1), tolerance);
    }
  }
}

TEST(WhiteBalanceTest, UniformGrayStaysGray) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(8, 8, {0.4f, 0.4f, 0.4f}));
  ASSERT_TRUE(WhiteBalanceStage().Process(frame).ok());
  for (float v : frame.rgb.pixels()) EXPECT_FLOAT_EQ(v, 0.4f);
  EXPECT_EQ(frame.meta.wb_gains, (std::array<float, 3>{1.0f, 1.0f, 1.0f}));
}

TEST(WhiteBalanceTest, RemovesColorCastFromGrayScene) {
  // A gray card under warm light: red too strong, blue too weak.
  Frame frame =
      MakeRgbFrame(testing::MakeFlatImage(8, 8, {0.5f, 0.4f, 0.2f}));
  ASSERT_TRUE(WhiteBalanceStage().Process(frame).ok());
  ExpectNeutral(frame.rgb, 1e-6f);
  // Gains after rescaling so the smallest is 1: R 0.8 -> 1, G 1 -> 1.25,
  // B 2 -> 2.5.
  EXPECT_NEAR(frame.meta.wb_gains[0], 1.0f, 1e-6f);
  EXPECT_NEAR(frame.meta.wb_gains[1], 1.25f, 1e-6f);
  EXPECT_NEAR(frame.meta.wb_gains[2], 2.5f, 1e-6f);
}

TEST(WhiteBalanceTest, GainsRestoreNeutralityOfAColorfulScene) {
  // Apply a known cast to the test scene, then check that gray-world on the
  // cast image recovers the inverse ratio of that cast.
  Image<float> scene = testing::MakeTestScene(128, 96);
  const std::array<float, 3> truth_gains = EstimateGrayWorldGains(scene);
  const std::array<float, 3> cast = {0.9f, 1.0f, 0.6f};
  for (int y = 0; y < scene.height(); ++y) {
    for (int x = 0; x < scene.width(); ++x) {
      for (int c = 0; c < 3; ++c) scene.at(x, y, c) *= cast[c];
    }
  }
  const std::array<float, 3> gains = EstimateGrayWorldGains(scene);
  EXPECT_NEAR(gains[0], truth_gains[0] / cast[0], 1e-3f);
  EXPECT_NEAR(gains[2], truth_gains[2] / cast[2], 1e-3f);
}

TEST(WhiteBalanceTest, ClippedPixelsAreIgnoredAndStayWhite) {
  Image<float> image = testing::MakeFlatImage(10, 10, {0.5f, 0.4f, 0.2f});
  // A blown highlight covering 20% of the image. If it were counted, it
  // would pull the estimate toward "no cast".
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 10; ++x) {
      for (int c = 0; c < 3; ++c) image.at(x, y, c) = 1.0f;
    }
  }
  Frame frame = MakeRgbFrame(image);
  ASSERT_TRUE(WhiteBalanceStage().Process(frame).ok());
  EXPECT_NEAR(frame.meta.wb_gains[2] / frame.meta.wb_gains[1], 2.0f, 1e-5f);
  for (int c = 0; c < 3; ++c) EXPECT_FLOAT_EQ(frame.rgb.at(0, 0, c), 1.0f);
  EXPECT_NEAR(frame.rgb.at(5, 5, 0), frame.rgb.at(5, 5, 2), 1e-6f);
}

TEST(WhiteBalanceTest, FromMetadataAppliesCameraGains) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(4, 4, {0.2f, 0.4f, 0.1f}));
  frame.meta.wb_gains = {2.0f, 1.0f, 4.0f};
  ASSERT_TRUE(
      WhiteBalanceStage(WhiteBalanceStage::Mode::kFromMetadata).Process(frame)
          .ok());
  ExpectNeutral(frame.rgb, 1e-6f);
  EXPECT_FLOAT_EQ(frame.rgb.at(0, 0, 1), 0.4f);
}

TEST(WhiteBalanceTest, FromMetadataRejectsNonPositiveGains) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(4, 4, {0.2f, 0.4f, 0.1f}));
  frame.meta.wb_gains = {2.0f, 0.0f, 4.0f};
  EXPECT_EQ(WhiteBalanceStage(WhiteBalanceStage::Mode::kFromMetadata)
                .Process(frame)
                .code(),
            StatusCode::kInvalidArgument);
}

TEST(WhiteBalanceTest, RejectsMissingRgb) {
  Frame frame;
  EXPECT_EQ(WhiteBalanceStage().Process(frame).code(),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
