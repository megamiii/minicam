#include "minicam/color.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <gtest/gtest.h>

namespace minicam {
namespace {

TEST(ColorTest, EndpointsAreFixed) {
  EXPECT_FLOAT_EQ(SrgbToLinear(0.0f), 0.0f);
  EXPECT_NEAR(SrgbToLinear(1.0f), 1.0f, 1e-6f);
  EXPECT_FLOAT_EQ(LinearToSrgb(0.0f), 0.0f);
  EXPECT_NEAR(LinearToSrgb(1.0f), 1.0f, 1e-6f);
}

TEST(ColorTest, MidGrayIsDarkerInLinearLight) {
  // sRGB 0.5 is about 21.4% of the light of white.
  EXPECT_NEAR(SrgbToLinear(0.5f), 0.2140f, 1e-3f);
}

TEST(ColorTest, RoundTrip) {
  for (int i = 0; i <= 100; ++i) {
    const float v = static_cast<float>(i) / 100.0f;
    EXPECT_NEAR(LinearToSrgb(SrgbToLinear(v)), v, 1e-5f);
  }
}

TEST(SrgbEncodeTableTest, MatchesExactCurveWithinOnePercentOfAnOutputStep) {
  const SrgbEncodeTable& table = SrgbEncodeTable::Get();
  float max_error = 0.0f;
  // Dense sweep, including points between table entries.
  for (int i = 0; i <= 1000000; ++i) {
    const float v = static_cast<float>(i) / 1000000.0f;
    max_error =
        std::max(max_error, std::abs(table.Encode(v) - LinearToSrgb(v)));
  }
  std::cout << "sRGB table max error: " << max_error << "\n";
  EXPECT_LT(max_error, 0.01f / 255.0f);
  EXPECT_FLOAT_EQ(table.Encode(0.0f), 0.0f);
  EXPECT_NEAR(table.Encode(1.0f), 1.0f, 1e-6f);
}

}  // namespace
}  // namespace minicam
