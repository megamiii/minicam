#include "minicam/image.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace minicam {
namespace {

TEST(ImageTest, InterleavedLayout) {
  Image<float> image(4, 3, 3);
  EXPECT_EQ(image.size(), 4u * 3u * 3u);
  EXPECT_EQ(image.stride(), 12u);

  image.at(2, 1, 1) = 7.0f;
  // Row 1 starts at element 12; pixel 2 starts 6 elements later; channel 1.
  EXPECT_EQ(image.pixels()[12 + 6 + 1], 7.0f);
  EXPECT_EQ(image.row(1)[6 + 1], 7.0f);
}

TEST(ImageTest, FillSetsEveryElement) {
  Image<uint16_t> image(5, 5, 1);
  image.Fill(42);
  for (uint16_t v : image.pixels()) EXPECT_EQ(v, 42);
}

TEST(ImageTest, ResizeToSmallerKeepsCapacity) {
  Image<float> image(64, 64, 3);
  const size_t capacity = image.capacity();
  image.Resize(32, 32, 3);
  EXPECT_EQ(image.capacity(), capacity);
  image.Resize(64, 64, 3);
  EXPECT_EQ(image.capacity(), capacity);
}

// 3x2 image with values 0..5:   0 1 2
//                               3 4 5
Image<int> MakeSmall() {
  Image<int> image(3, 2, 1);
  for (int i = 0; i < 6; ++i) image.pixels()[i] = i;
  return image;
}

std::vector<int> Values(const Image<int>& image) {
  return {image.pixels().begin(), image.pixels().end()};
}

TEST(RotateClockwiseTest, QuarterTurn) {
  Image<int> out;
  RotateClockwise(MakeSmall(), 1, out);
  // 3 0
  // 4 1
  // 5 2
  EXPECT_EQ(out.width(), 2);
  EXPECT_EQ(out.height(), 3);
  EXPECT_EQ(Values(out), (std::vector<int>{3, 0, 4, 1, 5, 2}));
}

TEST(RotateClockwiseTest, HalfTurnAndThreeQuarters) {
  Image<int> out;
  RotateClockwise(MakeSmall(), 2, out);
  EXPECT_EQ(Values(out), (std::vector<int>{5, 4, 3, 2, 1, 0}));
  RotateClockwise(MakeSmall(), 3, out);
  // 2 5
  // 1 4
  // 0 3
  EXPECT_EQ(Values(out), (std::vector<int>{2, 5, 1, 4, 0, 3}));
}

TEST(RotateClockwiseTest, FourTurnsIsIdentityAndKeepsChannels) {
  Image<int> rgb(4, 3, 3);
  for (size_t i = 0; i < rgb.size(); ++i) rgb.pixels()[i] = static_cast<int>(i);
  Image<int> a;
  Image<int> b;
  RotateClockwise(rgb, 1, a);
  RotateClockwise(a, 3, b);
  EXPECT_EQ(Values(b), Values(rgb));
  RotateClockwise(rgb, 4, a);
  EXPECT_EQ(Values(a), Values(rgb));
}

}  // namespace
}  // namespace minicam
