#include "minicam/stages/face_detect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace minicam {
namespace {

constexpr int kFaceSize = 48;

// A cartoon face: dark oval head on a light background, light eyes and a
// mouth. Only for tests; the detector compares gradient structure.
Image<float> MakeFace() {
  Image<float> face(kFaceSize, kFaceSize, 1);
  const float c = static_cast<float>(kFaceSize) / 2.0f;
  for (int y = 0; y < kFaceSize; ++y) {
    for (int x = 0; x < kFaceSize; ++x) {
      const auto fx = static_cast<float>(x);
      const auto fy = static_cast<float>(y);
      const float dx = (fx - c) / 18.0f;
      const float dy = (fy - c) / 22.0f;
      float v = dx * dx + dy * dy < 1.0f ? 0.35f : 0.8f;
      auto disk = [&](float cx, float cy, float r) {
        return (fx - cx) * (fx - cx) + (fy - cy) * (fy - cy) < r * r;
      };
      if (disk(16.0f, 19.0f, 4.0f) || disk(32.0f, 19.0f, 4.0f)) v = 0.9f;
      if (y >= 33 && y <= 35 && x >= 16 && x <= 32) v = 0.9f;
      face.at(x, y) = v;
    }
  }
  return face;
}

// Noisy smooth background with the face pasted at the given positions.
Image<float> MakeTarget(int width, int height,
                        const std::vector<std::pair<int, int>>& positions) {
  Image<float> gray(width, height, 1);
  std::mt19937 rng(11);
  std::normal_distribution<float> noise(0.0f, 0.02f);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      gray.at(x, y) = 0.5f +
                      0.2f * std::sin(static_cast<float>(x) / 37.0f) *
                          std::cos(static_cast<float>(y) / 23.0f) +
                      noise(rng);
    }
  }
  const Image<float> face = MakeFace();
  for (auto [px, py] : positions) {
    for (int y = 0; y < kFaceSize; ++y) {
      for (int x = 0; x < kFaceSize; ++x)
        gray.at(px + x, py + y) = face.at(x, y);
    }
  }
  return gray;
}

Frame GrayToFrame(const Image<float>& gray, int upscale = 1) {
  Frame frame;
  frame.rgb.Resize(gray.width() * upscale, gray.height() * upscale, 3);
  for (int y = 0; y < frame.rgb.height(); ++y) {
    for (int x = 0; x < frame.rgb.width(); ++x) {
      for (int c = 0; c < 3; ++c) {
        frame.rgb.at(x, y, c) = gray.at(x / upscale, y / upscale);
      }
    }
  }
  return frame;
}

std::vector<Detection> Detect(Frame& frame,
                              const FaceDetectionOptions& options) {
  auto stage = HogFaceDetectionStage::Create(MakeFace(), options);
  EXPECT_TRUE(stage.ok()) << stage.status().ToString();
  EXPECT_TRUE((*stage)->Process(frame).ok());
  return frame.detections;
}

TEST(FaceDetectTest, FindsEveryEmbeddedFaceExactlyOnce) {
  // Positions on the stride-3 grid, so a window lands exactly on each face.
  Frame frame = GrayToFrame(MakeTarget(240, 180, {{30, 39}, {150, 90}}));
  std::vector<Detection> found = Detect(frame, {});
  std::sort(found.begin(), found.end(),
            [](const Detection& a, const Detection& b) { return a.x < b.x; });

  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].x, 30);
  EXPECT_EQ(found[0].y, 39);
  EXPECT_EQ(found[1].x, 150);
  EXPECT_EQ(found[1].y, 90);
  for (const Detection& d : found) {
    EXPECT_EQ(d.width, kFaceSize);
    EXPECT_EQ(d.height, kFaceSize);
    // Not 1.0: the template's HOG is computed on the template alone (edge
    // pixels replicated), while a window in the target also sees the
    // background just outside it, which adds gradients to its border cells.
    // Measured 0.93 and 0.95.
    EXPECT_GT(d.score, 0.9f);
    std::cout << "  face at (" << d.x << ", " << d.y << "), NCC " << d.score
              << "\n";
  }
}

TEST(FaceDetectTest, NoFacesNoDetections) {
  Frame frame = GrayToFrame(MakeTarget(240, 180, {}));
  EXPECT_TRUE(Detect(frame, {}).empty());
}

TEST(FaceDetectTest, ThreadCountDoesNotChangeTheResult) {
  const Image<float> target = MakeTarget(240, 180, {{30, 39}, {150, 90}});
  FaceDetectionOptions one;
  one.num_threads = 1;
  one.ncc_threshold = 0.2f;  // Many candidates, to exercise the merge.
  Frame a = GrayToFrame(target);
  const std::vector<Detection> expected = Detect(a, one);
  for (int threads : {2, 5, 0}) {
    FaceDetectionOptions many = one;
    many.num_threads = threads;
    Frame b = GrayToFrame(target);
    const std::vector<Detection> got = Detect(b, many);
    ASSERT_EQ(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i) {
      EXPECT_EQ(got[i].x, expected[i].x);
      EXPECT_EQ(got[i].y, expected[i].y);
      EXPECT_EQ(got[i].score, expected[i].score);
    }
  }
}

TEST(FaceDetectTest, DownscaledDetectionReportsFullResolutionBoxes) {
  // The same scene at twice the size: faces are 96 px, the template 48 px.
  Frame frame =
      GrayToFrame(MakeTarget(240, 180, {{30, 39}, {150, 90}}), /*upscale=*/2);
  FaceDetectionOptions options;
  options.downscales = {2};
  std::vector<Detection> found = Detect(frame, options);
  std::sort(found.begin(), found.end(),
            [](const Detection& a, const Detection& b) { return a.x < b.x; });
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].x, 60);
  EXPECT_EQ(found[0].y, 78);
  EXPECT_EQ(found[0].width, 2 * kFaceSize);
  EXPECT_EQ(found[1].x, 300);
  EXPECT_EQ(found[1].y, 180);
}

// Faces of two sizes in one image: a pyramid of factors {1, 2} finds the
// small faces at factor 1 and the large one at factor 2, and NMS merges the
// candidates of both scales.
TEST(FaceDetectTest, PyramidFindsFacesOfDifferentSizes) {
  // Large face: the 48 px face enlarged 2x (96 px) at (180, 30).
  Frame frame = GrayToFrame(MakeTarget(320, 240, {{30, 39}, {30, 150}}));
  const Image<float> face = MakeFace();
  for (int y = 0; y < 2 * kFaceSize; ++y) {
    for (int x = 0; x < 2 * kFaceSize; ++x) {
      for (int c = 0; c < 3; ++c) {
        frame.rgb.at(180 + x, 30 + y, c) = face.at(x / 2, y / 2);
      }
    }
  }
  FaceDetectionOptions options;
  options.downscales = {1, 2};
  std::vector<Detection> found = Detect(frame, options);
  std::sort(found.begin(), found.end(),
            [](const Detection& a, const Detection& b) {
              return a.x != b.x ? a.x < b.x : a.y < b.y;
            });
  ASSERT_EQ(found.size(), 3u);
  EXPECT_EQ(found[0].x, 30);
  EXPECT_EQ(found[0].width, kFaceSize);
  EXPECT_EQ(found[1].y, 150);
  EXPECT_EQ(found[2].x, 180);
  EXPECT_EQ(found[2].y, 30);
  EXPECT_EQ(found[2].width, 2 * kFaceSize);

  // With only factor 1, the large face is missed.
  options.downscales = {1};
  Frame again = frame;
  EXPECT_EQ(Detect(again, options).size(), 2u);
}

TEST(FaceDetectTest, RejectsBadTemplates) {
  EXPECT_FALSE(HogFaceDetectionStage::Create(Image<float>(8, 8, 1)).ok());
  Image<float> flat(48, 48, 1);
  flat.Fill(0.5f);
  EXPECT_FALSE(HogFaceDetectionStage::Create(flat).ok());
  EXPECT_FALSE(HogFaceDetectionStage::Create(Image<float>(48, 48, 3)).ok());
}

TEST(NonMaxSuppressionTest, KeepsBestOfOverlappingBoxes) {
  std::vector<Detection> boxes = {
      {0, 0, 10, 10, 0.6f}, {1, 1, 10, 10, 0.9f}, {50, 50, 10, 10, 0.7f}};
  std::vector<Detection> kept = NonMaxSuppression(boxes, 0.5f);
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].score, 0.9f);
  EXPECT_EQ(kept[1].score, 0.7f);
}

// A is best; B overlaps A and is dropped; C overlaps only B. Greedy NMS keeps
// C because B was already suppressed. (Suppressing every box that overlaps
// any higher-scoring box, suppressed or not, would wrongly drop C.)
TEST(NonMaxSuppressionTest, BoxOverlappingOnlyASuppressedBoxIsKept) {
  const Detection a{0, 0, 10, 10, 0.9f};
  const Detection b{4, 0, 10, 10, 0.8f};
  const Detection c{8, 0, 10, 10, 0.7f};
  ASSERT_GT(IntersectionOverUnion(a, b), 0.4f);
  ASSERT_GT(IntersectionOverUnion(b, c), 0.4f);
  ASSERT_LT(IntersectionOverUnion(a, c), 0.4f);
  const std::vector<Detection> kept = NonMaxSuppression({c, b, a}, 0.4f);
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].x, 0);
  EXPECT_EQ(kept[1].x, 8);
}

TEST(NonMaxSuppressionTest, IntersectionOverUnion) {
  const Detection a{0, 0, 10, 10, 0.0f};
  EXPECT_FLOAT_EQ(IntersectionOverUnion(a, a), 1.0f);
  EXPECT_FLOAT_EQ(IntersectionOverUnion(a, {20, 20, 10, 10, 0.0f}), 0.0f);
  // Overlap 5x10 = 50, union 150.
  EXPECT_FLOAT_EQ(IntersectionOverUnion(a, {5, 0, 10, 10, 0.0f}), 1.0f / 3.0f);
}

}  // namespace
}  // namespace minicam
