#include "minicam/io/raw_file.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace minicam {
namespace {

std::string TempPath(const std::string& name) {
  return (std::filesystem::path(::testing::TempDir()) / name).string();
}

Frame MakeFrame() {
  Frame frame;
  frame.raw.Resize(6, 4, 1);
  for (size_t i = 0; i < frame.raw.size(); ++i) {
    frame.raw.pixels()[i] = static_cast<uint16_t>(i * 37);
  }
  frame.meta.frame_id = 12;
  frame.meta.timestamp_ns = 123456789;
  frame.meta.exposure_time_s = 0.01;
  frame.meta.analog_gain = 2.0f;
  frame.meta.cfa = CfaPattern::kGrbg;
  frame.meta.black_level = {60, 61, 62, 63};
  frame.meta.white_level = 1023;
  frame.meta.wb_gains = {2.0f, 1.0f, 1.5f};
  frame.meta.color_matrix = {1.5f, -0.3f, -0.2f, -0.1f, 1.4f,
                             -0.3f, 0.0f, -0.5f, 1.5f};
  return frame;
}

TEST(RawFileTest, RoundTripPreservesPixelsAndMetadata) {
  const std::string path = TempPath("roundtrip.raw");
  const Frame original = MakeFrame();
  ASSERT_TRUE(WriteRawFile(original, path).ok());

  Frame loaded;
  ASSERT_TRUE(ReadRawFile(path, loaded).ok());
  EXPECT_EQ(loaded.raw.width(), 6);
  EXPECT_EQ(loaded.raw.height(), 4);
  EXPECT_TRUE(std::equal(original.raw.pixels().begin(),
                         original.raw.pixels().end(),
                         loaded.raw.pixels().begin()));

  const FrameMetadata& a = original.meta;
  const FrameMetadata& b = loaded.meta;
  EXPECT_EQ(b.frame_id, a.frame_id);
  EXPECT_EQ(b.timestamp_ns, a.timestamp_ns);
  EXPECT_EQ(b.exposure_time_s, a.exposure_time_s);
  EXPECT_EQ(b.analog_gain, a.analog_gain);
  EXPECT_EQ(b.cfa, a.cfa);
  EXPECT_EQ(b.black_level, a.black_level);
  EXPECT_EQ(b.white_level, a.white_level);
  EXPECT_EQ(b.wb_gains, a.wb_gains);
  EXPECT_EQ(b.color_matrix, a.color_matrix);
}

TEST(RawFileTest, MissingFileIsNotFound) {
  Frame frame;
  EXPECT_EQ(ReadRawFile(TempPath("does_not_exist.raw"), frame).code(),
            StatusCode::kNotFound);
}

TEST(RawFileTest, WrongMagicIsDataLoss) {
  const std::string path = TempPath("not_raw.raw");
  std::ofstream(path) << "this is not a raw file at all";
  Frame frame;
  EXPECT_EQ(ReadRawFile(path, frame).code(), StatusCode::kDataLoss);
}

TEST(RawFileTest, TruncatedPixelsAreDataLoss) {
  const std::string path = TempPath("truncated.raw");
  ASSERT_TRUE(WriteRawFile(MakeFrame(), path).ok());
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 10);
  Frame frame;
  EXPECT_EQ(ReadRawFile(path, frame).code(), StatusCode::kDataLoss);
}

}  // namespace
}  // namespace minicam
