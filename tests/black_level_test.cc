#include "minicam/stages/black_level.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace minicam {
namespace {

Frame MakeRawFrame(int width, int height, uint16_t value, uint16_t black,
                   uint16_t white) {
  Frame frame;
  frame.raw.Resize(width, height, 1);
  frame.raw.Fill(value);
  frame.meta.black_level.fill(black);
  frame.meta.white_level = white;
  return frame;
}

TEST(BlackLevelTest, BlackMapsToZeroAndWhiteToOne) {
  BlackLevelStage stage;

  Frame dark = MakeRawFrame(4, 4, 64, 64, 1023);
  ASSERT_TRUE(stage.Process(dark).ok());
  for (float v : dark.bayer.pixels()) EXPECT_FLOAT_EQ(v, 0.0f);

  Frame bright = MakeRawFrame(4, 4, 1023, 64, 1023);
  ASSERT_TRUE(stage.Process(bright).ok());
  for (float v : bright.bayer.pixels()) EXPECT_FLOAT_EQ(v, 1.0f);
}

TEST(BlackLevelTest, ScalesLinearly) {
  BlackLevelStage stage;
  // Halfway between black (100) and white (1100).
  Frame frame = MakeRawFrame(2, 2, 600, 100, 1100);
  ASSERT_TRUE(stage.Process(frame).ok());
  for (float v : frame.bayer.pixels()) EXPECT_FLOAT_EQ(v, 0.5f);
}

TEST(BlackLevelTest, ClampsOutOfRangeValues) {
  BlackLevelStage stage;
  Frame frame = MakeRawFrame(2, 2, 0, 64, 1023);
  frame.raw.at(1, 1) = 4000;  // Above the white level.
  ASSERT_TRUE(stage.Process(frame).ok());
  EXPECT_FLOAT_EQ(frame.bayer.at(0, 0), 0.0f);  // Below black.
  EXPECT_FLOAT_EQ(frame.bayer.at(1, 1), 1.0f);
}

TEST(BlackLevelTest, UsesPerTileBlackLevels) {
  BlackLevelStage stage;
  Frame frame = MakeRawFrame(4, 4, 200, 0, 1000);
  frame.meta.black_level = {100, 150, 50, 0};  // Tile positions 0..3.
  ASSERT_TRUE(stage.Process(frame).ok());
  // Pixel (2, 0) is tile 0, (3, 0) tile 1, (0, 3) tile 2, (3, 3) tile 3.
  EXPECT_FLOAT_EQ(frame.bayer.at(2, 0), 100.0f / 900.0f);
  EXPECT_FLOAT_EQ(frame.bayer.at(3, 0), 50.0f / 850.0f);
  EXPECT_FLOAT_EQ(frame.bayer.at(0, 3), 150.0f / 950.0f);
  EXPECT_FLOAT_EQ(frame.bayer.at(3, 3), 200.0f / 1000.0f);
}

TEST(BlackLevelTest, RejectsWhiteNotAboveBlack) {
  BlackLevelStage stage;
  Frame frame = MakeRawFrame(2, 2, 10, 64, 64);
  EXPECT_EQ(stage.Process(frame).code(), StatusCode::kInvalidArgument);
}

TEST(BlackLevelTest, RejectsEmptyRaw) {
  BlackLevelStage stage;
  Frame frame;
  frame.meta.white_level = 1023;
  EXPECT_EQ(stage.Process(frame).code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace minicam
