#include "minicam/stages/color_correction.h"

#include <limits>

#include <gtest/gtest.h>

#include "test_scenes.h"

namespace minicam {
namespace {

Frame MakeRgbFrame(const Image<float>& rgb) {
  Frame frame;
  frame.rgb = rgb;
  return frame;
}

TEST(ColorCorrectionTest, IdentityMatrixChangesNothing) {
  const Image<float> scene = testing::MakeTestScene(32, 24);
  Frame frame = MakeRgbFrame(scene);  // Default matrix is the identity.
  ASSERT_TRUE(ColorCorrectionStage().Process(frame).ok());
  for (size_t i = 0; i < scene.size(); ++i) {
    ASSERT_EQ(frame.rgb.pixels()[i], scene.pixels()[i]);
  }
}

TEST(ColorCorrectionTest, AppliesMatrixPerPixel) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(2, 2, {0.5f, 0.4f, 0.2f}));
  frame.meta.color_matrix = {1.6f,  -0.4f, -0.2f,  //
                             -0.3f, 1.6f,  -0.3f,  //
                             0.0f,  -0.6f, 1.6f};
  ASSERT_TRUE(ColorCorrectionStage().Process(frame).ok());
  EXPECT_NEAR(frame.rgb.at(1, 1, 0), 1.6f * 0.5f - 0.4f * 0.4f - 0.2f * 0.2f,
              1e-6f);
  EXPECT_NEAR(frame.rgb.at(1, 1, 1), -0.3f * 0.5f + 1.6f * 0.4f - 0.3f * 0.2f,
              1e-6f);
  EXPECT_NEAR(frame.rgb.at(1, 1, 2), -0.6f * 0.4f + 1.6f * 0.2f, 1e-6f);
}

TEST(ColorCorrectionTest, RowsSummingToOneKeepNeutralsNeutral) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(4, 4, {0.3f, 0.3f, 0.3f}));
  frame.meta.color_matrix = {1.6f,  -0.4f, -0.2f,  //
                             -0.3f, 1.6f,  -0.3f,  //
                             0.0f,  -0.6f, 1.6f};
  ASSERT_TRUE(ColorCorrectionStage().Process(frame).ok());
  for (float v : frame.rgb.pixels()) EXPECT_NEAR(v, 0.3f, 1e-6f);
}

TEST(ColorCorrectionTest, ClampsNegativesButKeepsHeadroom) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(1, 1, {0.0f, 1.0f, 0.0f}));
  frame.meta.color_matrix = {1.0f, -0.5f, 0.0f,  //
                             0.0f, 1.5f,  0.0f,  //
                             0.0f, 0.0f,  1.0f};
  ASSERT_TRUE(ColorCorrectionStage().Process(frame).ok());
  EXPECT_EQ(frame.rgb.at(0, 0, 0), 0.0f);
  EXPECT_FLOAT_EQ(frame.rgb.at(0, 0, 1), 1.5f);
}

TEST(ColorCorrectionTest, RejectsNonFiniteMatrix) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(2, 2, {0.1f, 0.1f, 0.1f}));
  frame.meta.color_matrix[4] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(ColorCorrectionStage().Process(frame).code(),
            StatusCode::kInvalidArgument);
}

TEST(ColorCorrectionTest, RejectsMissingRgb) {
  Frame frame;
  EXPECT_EQ(ColorCorrectionStage().Process(frame).code(),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
