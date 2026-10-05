#include "minicam/color.h"

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
    const float v = i / 100.0f;
    EXPECT_NEAR(LinearToSrgb(SrgbToLinear(v)), v, 1e-5f);
  }
}

}  // namespace
}  // namespace minicam
