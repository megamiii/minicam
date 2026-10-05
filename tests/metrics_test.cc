#include "minicam/metrics.h"

#include <cmath>

#include <gtest/gtest.h>

#include "test_scenes.h"

namespace minicam {
namespace {

TEST(PsnrTest, IdenticalImagesAreInfinite) {
  const Image<float> image = testing::MakeTestScene(16, 16);
  auto psnr = Psnr(image, image);
  ASSERT_TRUE(psnr.ok());
  EXPECT_TRUE(std::isinf(*psnr));
}

TEST(PsnrTest, KnownError) {
  // A constant error of 0.1 gives MSE 0.01, so PSNR = 10*log10(1/0.01) = 20.
  const Image<float> a = testing::MakeFlatImage(8, 8, {0.5f, 0.5f, 0.5f});
  const Image<float> b = testing::MakeFlatImage(8, 8, {0.6f, 0.4f, 0.6f});
  auto psnr = Psnr(a, b);
  ASSERT_TRUE(psnr.ok());
  EXPECT_NEAR(*psnr, 20.0, 1e-4);
}

TEST(PsnrTest, BorderIsIgnored) {
  Image<float> a = testing::MakeFlatImage(8, 8, {0.5f, 0.5f, 0.5f});
  Image<float> b = a;
  b.at(0, 0, 0) = 1.0f;  // Error only in the border.
  auto psnr = Psnr(a, b, /*border=*/1);
  ASSERT_TRUE(psnr.ok());
  EXPECT_TRUE(std::isinf(*psnr));
}

TEST(PsnrTest, RejectsMismatchedSizes) {
  EXPECT_FALSE(Psnr(testing::MakeTestScene(8, 8), testing::MakeTestScene(8, 6))
                   .ok());
}

}  // namespace
}  // namespace minicam
