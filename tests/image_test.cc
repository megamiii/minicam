#include "minicam/image.h"

#include <cstdint>

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

}  // namespace
}  // namespace minicam
