#include "minicam/cfa.h"

#include <gtest/gtest.h>

namespace minicam {
namespace {

TEST(CfaTest, RggbLayout) {
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 0, 0), kRed);
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 1, 0), kGreen);
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 0, 1), kGreen);
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 1, 1), kBlue);
  // The tile repeats every two pixels.
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 10, 20), kRed);
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, 11, 21), kBlue);
}

TEST(CfaTest, NegativeCoordinatesKeepParity) {
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, -1, 0), kGreen);
  EXPECT_EQ(CfaColorAt(CfaPattern::kRggb, -1, -1), kBlue);
}

TEST(CfaTest, EveryPatternHasTwoGreensOneRedOneBlue) {
  for (CfaPattern p : {CfaPattern::kRggb, CfaPattern::kBggr, CfaPattern::kGrbg,
                       CfaPattern::kGbrg}) {
    int counts[3] = {0, 0, 0};
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) ++counts[CfaColorAt(p, x, y)];
    }
    EXPECT_EQ(counts[kRed], 1) << CfaPatternName(p);
    EXPECT_EQ(counts[kGreen], 2) << CfaPatternName(p);
    EXPECT_EQ(counts[kBlue], 1) << CfaPatternName(p);
  }
}

TEST(CfaTest, NameRoundTrip) {
  for (CfaPattern p : {CfaPattern::kRggb, CfaPattern::kBggr, CfaPattern::kGrbg,
                       CfaPattern::kGbrg}) {
    EXPECT_EQ(ParseCfaPattern(CfaPatternName(p)), p);
  }
  EXPECT_FALSE(ParseCfaPattern("XYZW").has_value());
}

}  // namespace
}  // namespace minicam
