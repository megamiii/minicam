#include "minicam/stages/tone_map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include <gtest/gtest.h>

#include "minicam/color.h"
#include "test_scenes.h"

namespace minicam {
namespace {

Frame MakeRgbFrame(const Image<float>& rgb) {
  Frame frame;
  frame.rgb = rgb;
  return frame;
}

ToneMapOptions ClipWithoutAutoExposure() {
  ToneMapOptions options;
  options.op = ToneMapOptions::Operator::kClip;
  options.auto_exposure = false;
  return options;
}

TEST(ToneMapTest, ClipOperatorIsSrgbEncoding) {
  Image<float> image(4, 1, 3);
  const std::array<float, 4> inputs = {0.0f, 0.2140f, 1.0f, 3.0f};
  for (int x = 0; x < 4; ++x) {
    for (int c = 0; c < 3; ++c) image.at(x, 0, c) = inputs[x];
  }
  Frame frame = MakeRgbFrame(image);
  ASSERT_TRUE(ToneMapStage(ClipWithoutAutoExposure()).Process(frame).ok());
  EXPECT_FLOAT_EQ(frame.rgb.at(0, 0), 0.0f);
  EXPECT_NEAR(frame.rgb.at(1, 0), 0.5f, 1e-3f);  // Linear 21.4% = sRGB 0.5.
  EXPECT_NEAR(frame.rgb.at(2, 0), 1.0f, 1e-6f);
  EXPECT_NEAR(frame.rgb.at(3, 0), 1.0f, 1e-6f);  // Clipped.
}

TEST(ToneMapTest, ExposureEvDoublesPerStop) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(2, 2, {0.1f, 0.1f, 0.1f}));
  ToneMapOptions options = ClipWithoutAutoExposure();
  options.exposure_ev = 1.0f;
  ASSERT_TRUE(ToneMapStage(options).Process(frame).ok());
  EXPECT_NEAR(frame.rgb.at(0, 0), LinearToSrgb(0.2f), 1e-6f);
}

TEST(ToneMapTest, ReinhardOutputIsInRangeAndMonotonic) {
  // A luminance ramp spanning a wide dynamic range (0 to 50).
  Image<float> ramp(256, 1, 3);
  for (int x = 0; x < 256; ++x) {
    const float v = 50.0f * static_cast<float>(x * x) / (255.0f * 255.0f);
    for (int c = 0; c < 3; ++c) ramp.at(x, 0, c) = v;
  }
  Frame frame = MakeRgbFrame(ramp);
  ASSERT_TRUE(ToneMapStage().Process(frame).ok());
  for (int x = 0; x < 256; ++x) {
    const float v = frame.rgb.at(x, 0);
    ASSERT_GE(v, 0.0f);
    ASSERT_LE(v, 1.0f);
    if (x > 0) {
      ASSERT_GE(v, frame.rgb.at(x - 1, 0)) << "at " << x;
    }
  }
}

TEST(ToneMapTest, ReinhardKeepsHueRatios) {
  // A moderate color well below clipping: linear R:G:B ratios must survive
  // the curve (compare after undoing the sRGB encoding).
  Image<float> image = testing::MakeTestScene(64, 48);
  image.at(5, 5, 0) = 0.30f;
  image.at(5, 5, 1) = 0.15f;
  image.at(5, 5, 2) = 0.05f;
  Frame frame = MakeRgbFrame(image);
  ASSERT_TRUE(ToneMapStage().Process(frame).ok());
  const float r = SrgbToLinear(frame.rgb.at(5, 5, 0));
  const float g = SrgbToLinear(frame.rgb.at(5, 5, 1));
  const float b = SrgbToLinear(frame.rgb.at(5, 5, 2));
  EXPECT_NEAR(r / g, 2.0f, 1e-3f);
  EXPECT_NEAR(g / b, 3.0f, 1e-3f);
}

TEST(ToneMapTest, AutoExposureMakesOutputIndependentOfInputBrightness) {
  const Image<float> scene = testing::MakeTestScene(64, 48);
  Image<float> dark = scene;
  for (float& v : dark.pixels()) v *= 0.25f;

  Frame a = MakeRgbFrame(scene);
  Frame b = MakeRgbFrame(dark);
  ASSERT_TRUE(ToneMapStage().Process(a).ok());
  ASSERT_TRUE(ToneMapStage().Process(b).ok());
  for (size_t i = 0; i < scene.size(); ++i) {
    // Only the tiny log delta breaks exact invariance.
    ASSERT_NEAR(a.rgb.pixels()[i], b.rgb.pixels()[i], 2e-3f) << "at " << i;
  }
}

TEST(ToneMapTest, LowContrastSceneIsNotStretchedToWhite) {
  Frame frame = MakeRgbFrame(testing::MakeFlatImage(8, 8, {0.4f, 0.4f, 0.4f}));
  ASSERT_TRUE(ToneMapStage().Process(frame).ok());
  // Auto exposure maps it to the key 0.18. With the white point clamped to
  // 1, the curve leaves it unchanged: 0.18 (1 + 0.18) / (1 + 0.18) = 0.18.
  EXPECT_NEAR(frame.rgb.at(3, 3, 1), LinearToSrgb(0.18f), 2e-3f);
}

Image<float> ToneMap(const Image<float>& input, const ToneMapOptions& options) {
  Frame frame = MakeRgbFrame(input);
  EXPECT_TRUE(ToneMapStage(options).Process(frame).ok());
  return frame.rgb;
}

// High dynamic range input, so both the curve and clipping are exercised.
Image<float> MakeHdrScene() {
  Image<float> scene = testing::MakeTestScene(160, 120);
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 160; ++x) {
      for (int c = 0; c < 3; ++c) scene.at(x, y, c) *= 20.0f;
    }
  }
  return scene;
}

TEST(ToneMapTest, FastMatchesReference) {
  for (auto op :
       {ToneMapOptions::Operator::kReinhard, ToneMapOptions::Operator::kClip}) {
    ToneMapOptions reference;
    reference.op = op;
    reference.implementation = ToneMapOptions::Implementation::kReference;
    ToneMapOptions fast = reference;
    fast.implementation = ToneMapOptions::Implementation::kFast;

    const Image<float> a = ToneMap(MakeHdrScene(), reference);
    const Image<float> b = ToneMap(MakeHdrScene(), fast);
    float max_diff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
      max_diff = std::max(max_diff, std::abs(a.pixels()[i] - b.pixels()[i]));
    }
    std::cout << "tone map fast vs reference: max abs diff " << max_diff
              << "\n";
    EXPECT_LT(max_diff, 0.05f / 255.0f);
  }
}

TEST(ToneMapTest, ThreadCountDoesNotChangeTheResult) {
  ToneMapOptions one;
  one.num_threads = 1;
  const Image<float> expected = ToneMap(MakeHdrScene(), one);
  for (int threads : {2, 3, 7, 0}) {
    ToneMapOptions many = one;
    many.num_threads = threads;
    const Image<float> got = ToneMap(MakeHdrScene(), many);
    for (size_t i = 0; i < got.size(); ++i) {
      ASSERT_EQ(got.pixels()[i], expected.pixels()[i])
          << threads << " threads, element " << i;
    }
  }
}

TEST(ToneMapTest, RejectsMissingRgb) {
  Frame frame;
  EXPECT_EQ(ToneMapStage().Process(frame).code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
