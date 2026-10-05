#include "minicam/stages/demosaic.h"

#include <array>
#include <iostream>
#include <memory>
#include <utility>

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

// Ideal noise-free mosaic, so demosaic tests are independent of black level.
Frame MosaicFloat(const Image<float>& rgb, CfaPattern cfa) {
  Frame frame;
  EXPECT_TRUE(SampleCfa(rgb, cfa, frame).ok());
  return frame;
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

// Properties every demosaicing algorithm must have.
template <typename T>
class DemosaicTest : public ::testing::Test {};
using DemosaicStages =
    ::testing::Types<BilinearDemosaicStage, MalvarDemosaicStage>;
TYPED_TEST_SUITE(DemosaicTest, DemosaicStages);

TYPED_TEST(DemosaicTest, FlatColorIsReconstructedExactly) {
  const std::array<float, 3> color = {0.7f, 0.4f, 0.2f};
  for (CfaPattern cfa : kAllPatterns) {
    Frame frame = MosaicFloat(testing::MakeFlatImage(10, 8, color), cfa);
    ASSERT_TRUE(TypeParam().Process(frame).ok());
    ASSERT_EQ(frame.rgb.channels(), 3);
    // Includes the border pixels, which exercise the mirroring.
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 10; ++x) {
        for (int c = 0; c < 3; ++c) {
          ASSERT_NEAR(frame.rgb.at(x, y, c), color[c], 1e-6f)
              << CfaPatternName(cfa) << " (" << x << ", " << y << ") c=" << c;
        }
      }
    }
  }
}

TYPED_TEST(DemosaicTest, KeepsMeasuredSamples) {
  const Image<float> scene = testing::MakeTestScene(32, 24);
  for (CfaPattern cfa : kAllPatterns) {
    Frame frame = MosaicFloat(scene, cfa);
    ASSERT_TRUE(TypeParam().Process(frame).ok());
    for (int y = 0; y < 24; ++y) {
      for (int x = 0; x < 32; ++x) {
        ASSERT_EQ(frame.rgb.at(x, y, CfaColorAt(cfa, x, y)),
                  frame.bayer.at(x, y));
      }
    }
  }
}

TYPED_TEST(DemosaicTest, LinearRampIsExactAwayFromBorders) {
  // Linear interpolation is exact for a linear signal, and Malvar's Laplacian
  // correction is zero on it. Channels are offset so they differ.
  Image<float> ramp(16, 12, 3);
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 16; ++x) {
      for (int c = 0; c < 3; ++c) {
        ramp.at(x, y, c) = 0.03f * x + 0.02f * y + 0.1f * c;
      }
    }
  }
  for (CfaPattern cfa : kAllPatterns) {
    Frame frame = MosaicFloat(ramp, cfa);
    ASSERT_TRUE(TypeParam().Process(frame).ok());
    for (int y = 2; y < 10; ++y) {
      for (int x = 2; x < 14; ++x) {
        for (int c = 0; c < 3; ++c) {
          ASSERT_NEAR(frame.rgb.at(x, y, c), ramp.at(x, y, c), 1e-5f)
              << CfaPatternName(cfa) << " (" << x << ", " << y << ") c=" << c;
        }
      }
    }
  }
}

TYPED_TEST(DemosaicTest, RejectsMissingMosaic) {
  Frame frame;
  EXPECT_EQ(TypeParam().Process(frame).code(), StatusCode::kInvalidArgument);
}

struct ScenePsnr {
  double full = 0.0;
  double smooth = 0.0;
};

// End to end on synthetic RAW: mosaic -> black level -> demosaic, compared
// with the ground-truth linear image.
ScenePsnr MeasureOnTestScene(std::unique_ptr<Stage> demosaic) {
  constexpr int kWidth = 256;
  constexpr int kHeight = 192;
  const Image<float> truth = testing::MakeTestScene(kWidth, kHeight);
  Frame frame;
  EXPECT_TRUE(MosaicLinearRgb(truth, SyntheticRawOptions{}, frame).ok());

  Pipeline pipeline;
  pipeline.Add(std::make_unique<BlackLevelStage>()).Add(std::move(demosaic));
  EXPECT_TRUE(pipeline.Process(frame).ok());

  ScenePsnr result;
  auto full = Psnr(frame.rgb, truth, /*border=*/2);
  // The left strip of the scene holds only the smooth gradient.
  auto smooth = Psnr(Crop(frame.rgb, 0, 0, 40, kHeight),
                     Crop(truth, 0, 0, 40, kHeight), /*border=*/2);
  EXPECT_TRUE(full.ok());
  EXPECT_TRUE(smooth.ok());
  if (full.ok()) result.full = *full;
  if (smooth.ok()) result.smooth = *smooth;
  return result;
}

TEST(DemosaicQualityTest, BilinearPsnrOnTestScene) {
  const ScenePsnr psnr =
      MeasureOnTestScene(std::make_unique<BilinearDemosaicStage>());
  std::cout << "bilinear PSNR, full scene: " << psnr.full
            << " dB, smooth region: " << psnr.smooth << " dB\n";
  // Regression floors, set below the measured values. The smooth region must
  // be near the 10-bit quantization limit (about 70.4 dB for 959 levels);
  // the full scene is dominated by the thin stripes, which bilinear
  // interpolation aliases badly.
  EXPECT_GT(psnr.smooth, 65.0);
  EXPECT_GT(psnr.full, 19.0);
}

TEST(DemosaicQualityTest, MalvarBeatsBilinear) {
  const ScenePsnr bilinear =
      MeasureOnTestScene(std::make_unique<BilinearDemosaicStage>());
  const ScenePsnr malvar =
      MeasureOnTestScene(std::make_unique<MalvarDemosaicStage>());
  std::cout << "malvar PSNR, full scene: " << malvar.full
            << " dB, smooth region: " << malvar.smooth << " dB\n";
  EXPECT_GT(malvar.full, bilinear.full);
  EXPECT_GT(malvar.smooth, 65.0);
}

}  // namespace
}  // namespace minicam
