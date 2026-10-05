#include "minicam/stages/demosaic.h"

#include <array>
#include <iostream>
#include <memory>

#include <gtest/gtest.h>

#include "minicam/cfa.h"
#include "minicam/io/synthetic.h"
#include "minicam/metrics.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "test_scenes.h"

namespace minicam {
namespace {

constexpr std::array<CfaPattern, 4> kAllPatterns = {
    CfaPattern::kRggb, CfaPattern::kBggr, CfaPattern::kGrbg, CfaPattern::kGbrg};

// Builds frame.bayer by sampling a linear RGB image through a CFA (no
// quantization or noise), so demosaic tests are independent of black level.
Frame MosaicFloat(const Image<float>& rgb, CfaPattern cfa) {
  Frame frame;
  frame.meta.cfa = cfa;
  frame.bayer.Resize(rgb.width(), rgb.height(), 1);
  for (int y = 0; y < rgb.height(); ++y) {
    for (int x = 0; x < rgb.width(); ++x) {
      frame.bayer.at(x, y) = rgb.at(x, y, CfaColorAt(cfa, x, y));
    }
  }
  return frame;
}

TEST(BilinearDemosaicTest, FlatColorIsReconstructedExactly) {
  const std::array<float, 3> color = {0.7f, 0.4f, 0.2f};
  for (CfaPattern cfa : kAllPatterns) {
    Frame frame = MosaicFloat(testing::MakeFlatImage(10, 8, color), cfa);
    ASSERT_TRUE(BilinearDemosaicStage().Process(frame).ok());
    ASSERT_EQ(frame.rgb.channels(), 3);
    // Includes the border pixels, which exercise the mirroring.
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 10; ++x) {
        for (int c = 0; c < 3; ++c) {
          ASSERT_FLOAT_EQ(frame.rgb.at(x, y, c), color[c])
              << CfaPatternName(cfa) << " (" << x << ", " << y << ") c=" << c;
        }
      }
    }
  }
}

TEST(BilinearDemosaicTest, KeepsMeasuredSamples) {
  const Image<float> scene = testing::MakeTestScene(32, 24);
  for (CfaPattern cfa : kAllPatterns) {
    Frame frame = MosaicFloat(scene, cfa);
    ASSERT_TRUE(BilinearDemosaicStage().Process(frame).ok());
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 32; ++x) {
        ASSERT_EQ(frame.rgb.at(x, y, CfaColorAt(cfa, x, y)),
                  frame.bayer.at(x, y));
      }
    }
  }
}

TEST(BilinearDemosaicTest, HorizontalRampIsExactAwayFromBorders) {
  // Linear interpolation is exact for a linear signal.
  Image<float> ramp(16, 8, 3);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      for (int c = 0; c < 3; ++c) ramp.at(x, y, c) = 0.05f * x + 0.1f * c;
    }
  }
  Frame frame = MosaicFloat(ramp, CfaPattern::kRggb);
  ASSERT_TRUE(BilinearDemosaicStage().Process(frame).ok());
  for (int y = 1; y < 7; ++y) {
    for (int x = 1; x < 15; ++x) {
      for (int c = 0; c < 3; ++c) {
        ASSERT_NEAR(frame.rgb.at(x, y, c), ramp.at(x, y, c), 1e-6f);
      }
    }
  }
}

TEST(BilinearDemosaicTest, RejectsMissingMosaic) {
  Frame frame;
  EXPECT_EQ(BilinearDemosaicStage().Process(frame).code(),
            StatusCode::kInvalidArgument);
}

Image<float> Crop(const Image<float>& image, int x0, int y0, int width,
                  int height) {
  Image<float> out(width, height, image.channels());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < image.channels(); ++c) {
        out.at(x, y, c) = image.at(x0 + x, y0 + y, c);
      }
    }
  }
  return out;
}

// End to end on synthetic RAW: mosaic -> black level -> demosaic, compared
// with the ground-truth linear image.
TEST(BilinearDemosaicTest, PipelinePsnrOnTestScene) {
  constexpr int kWidth = 256;
  constexpr int kHeight = 192;
  const Image<float> truth = testing::MakeTestScene(kWidth, kHeight);
  Frame frame;
  ASSERT_TRUE(MosaicLinearRgb(truth, SyntheticRawOptions{}, frame).ok());

  Pipeline pipeline;
  pipeline.Add(std::make_unique<BlackLevelStage>())
      .Add(std::make_unique<BilinearDemosaicStage>());
  ASSERT_TRUE(pipeline.Process(frame).ok());

  auto full = Psnr(frame.rgb, truth, /*border=*/2);
  // The left strip of the scene holds only the smooth gradient.
  auto smooth = Psnr(Crop(frame.rgb, 0, 0, 40, kHeight),
                     Crop(truth, 0, 0, 40, kHeight), /*border=*/2);
  ASSERT_TRUE(full.ok());
  ASSERT_TRUE(smooth.ok());
  std::cout << "bilinear PSNR, full scene: " << *full
            << " dB, smooth region: " << *smooth << " dB\n";

  // Regression floors, set below the measured values. The smooth region must
  // be near the 10-bit quantization limit (about 70.4 dB for 959 levels);
  // the full scene is dominated by the thin stripes, which bilinear
  // interpolation aliases badly.
  EXPECT_GT(*smooth, 65.0);
  EXPECT_GT(*full, 19.0);
}

}  // namespace
}  // namespace minicam
