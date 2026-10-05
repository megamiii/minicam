#include "minicam/io/dng_reader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/color_correction.h"
#include "minicam/stages/demosaic.h"
#include "minicam/stages/tone_map.h"
#include "minicam/stages/white_balance.h"

namespace minicam {
namespace {

namespace fs = std::filesystem;

std::string TempPath(const std::string& name) {
  return (fs::path(::testing::TempDir()) / name).string();
}

// First payload DNG of the first downloaded HDR+ burst, if any.
std::optional<fs::path> FindHdrPlusDng() {
  const fs::path root = fs::path(MINICAM_SOURCE_DIR) / "data" / "hdrplus";
  std::error_code ec;
  std::vector<fs::path> bursts;
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (entry.is_directory()) bursts.push_back(entry.path());
  }
  std::sort(bursts.begin(), bursts.end());
  for (const fs::path& burst : bursts) {
    const fs::path dng = burst / "payload_N000.dng";
    if (fs::exists(dng)) return dng;
  }
  return std::nullopt;
}

TEST(DngReaderTest, MissingFileIsNotFound) {
  DngReader reader;
  Frame frame;
  EXPECT_EQ(reader.Read(TempPath("missing.dng"), frame).code(),
            StatusCode::kNotFound);
}

TEST(DngReaderTest, NonRawFileIsRejected) {
  const std::string path = TempPath("not_a_raw.dng");
  std::ofstream(path) << "definitely not a DNG file";
  DngReader reader;
  Frame frame;
  EXPECT_FALSE(reader.Read(path, frame).ok());
}

TEST(ColorMatrixFileTest, ParsesNineNumbers) {
  const std::string path = TempPath("rgb2rgb.txt");
  std::ofstream(path) << "1.5 -0.25 -0.25 -0.125 1.25 -0.125 0 -0.5 1.5\n";
  auto matrix = ReadColorMatrixFile(path);
  ASSERT_TRUE(matrix.ok());
  EXPECT_EQ((*matrix)[0], 1.5f);
  EXPECT_EQ((*matrix)[7], -0.5f);
}

TEST(ColorMatrixFileTest, RejectsShortFile) {
  const std::string path = TempPath("short.txt");
  std::ofstream(path) << "1 2 3";
  EXPECT_EQ(ReadColorMatrixFile(path).status().code(), StatusCode::kDataLoss);
}

// The following tests need the HDR+ data (scripts/download_hdrplus.sh).

TEST(DngReaderHdrPlusTest, ReadsPlausibleSensorMetadata) {
  const std::optional<fs::path> dng = FindHdrPlusDng();
  if (!dng) GTEST_SKIP() << "no HDR+ data in data/hdrplus";

  DngReader reader;
  Frame frame;
  frame.meta.frame_id = 42;
  const Status status = reader.Read(dng->string(), frame);
  ASSERT_TRUE(status.ok()) << status.ToString();

  const FrameMetadata& m = frame.meta;
  EXPECT_EQ(m.frame_id, 42u);  // Left untouched.
  EXPECT_GT(frame.raw.width(), 1000);
  EXPECT_GT(frame.raw.height(), 1000);
  for (uint16_t black : m.black_level) {
    EXPECT_GT(m.white_level, black);
  }
  EXPECT_FLOAT_EQ(m.wb_gains[1], 1.0f);
  EXPECT_GT(m.wb_gains[0], 0.0f);
  EXPECT_GT(m.wb_gains[2], 0.0f);
  for (int r = 0; r < 3; ++r) {
    const float row_sum = m.color_matrix[r * 3] + m.color_matrix[r * 3 + 1] +
                          m.color_matrix[r * 3 + 2];
    EXPECT_NEAR(row_sum, 1.0f, 0.05f) << "row " << r;
  }
  EXPECT_GT(m.exposure_time_s, 0.0);

  // Pixel values must lie within the sensor's range.
  const uint16_t max_value =
      *std::max_element(frame.raw.pixels().begin(), frame.raw.pixels().end());
  EXPECT_LE(max_value, m.white_level);

  std::cout << "  " << dng->filename().string() << ": " << frame.raw.width()
            << "x" << frame.raw.height() << " " << CfaPatternName(m.cfa)
            << ", black " << m.black_level[0] << "/" << m.black_level[1] << "/"
            << m.black_level[2] << "/" << m.black_level[3] << ", white "
            << m.white_level << ", wb " << m.wb_gains[0] << "/" << m.wb_gains[2]
            << ", exposure " << m.exposure_time_s << " s, gain "
            << m.analog_gain << "\n";
}

TEST(DngReaderHdrPlusTest, SidecarMatrixOverridesTheFileMatrix) {
  const std::optional<fs::path> dng = FindHdrPlusDng();
  if (!dng) GTEST_SKIP() << "no HDR+ data in data/hdrplus";
  auto matrix =
      ReadColorMatrixFile((dng->parent_path() / "rgb2rgb.txt").string());
  ASSERT_TRUE(matrix.ok()) << matrix.status().ToString();

  DngReader reader;
  reader.SetColorMatrixOverride(*matrix);
  Frame frame;
  ASSERT_TRUE(reader.Read(dng->string(), frame).ok());
  EXPECT_EQ(frame.meta.color_matrix, *matrix);
}

TEST(DngReaderHdrPlusTest, RealFrameRunsThroughTheIsp) {
  const std::optional<fs::path> dng = FindHdrPlusDng();
  if (!dng) GTEST_SKIP() << "no HDR+ data in data/hdrplus";
  DngReader reader;
  Frame frame;
  ASSERT_TRUE(reader.Read(dng->string(), frame).ok());

  Pipeline pipeline;
  pipeline.Add(std::make_unique<BlackLevelStage>())
      .Add(std::make_unique<MalvarDemosaicStage>())
      .Add(std::make_unique<WhiteBalanceStage>(
          WhiteBalanceStage::Mode::kFromMetadata))
      .Add(std::make_unique<ColorCorrectionStage>())
      .Add(std::make_unique<ToneMapStage>());
  const Status status = pipeline.Process(frame);
  ASSERT_TRUE(status.ok()) << status.ToString();
  for (float v : frame.rgb.pixels()) {
    ASSERT_GE(v, 0.0f);
    ASSERT_LE(v, 1.0f);
  }
}

}  // namespace
}  // namespace minicam
