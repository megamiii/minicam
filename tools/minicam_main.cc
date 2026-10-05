// minicam command-line tool.
//
//   minicam --input frame.raw --output out.png
//
// Milestone 1: reads minicam .raw files (see make_synthetic_raw) and runs
// black level + bilinear demosaicing. There is no tone mapping stage yet, so
// the PNG holds linear values and looks darker than a normal photo.

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "minicam/frame.h"
#include "minicam/io/image_io.h"
#include "minicam/io/raw_file.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/demosaic.h"

namespace {

constexpr std::string_view kUsage =
    "usage: minicam --input <frame.raw> --output <out.png>\n";

int Fail(std::string_view message) {
  std::cerr << "error: " << message << "\n" << kUsage;
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input;
  std::string output;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--input" && has_value) {
      input = argv[++i];
    } else if (arg == "--output" && has_value) {
      output = argv[++i];
    } else {
      return Fail("unknown or incomplete argument " + std::string(arg));
    }
  }
  if (input.empty() || output.empty()) {
    return Fail("--input and --output are required");
  }

  minicam::Frame frame;
  minicam::Status status = minicam::ReadRawFile(input, frame);
  if (!status.ok()) return Fail(status.ToString());

  minicam::Pipeline pipeline;
  pipeline.Add(std::make_unique<minicam::BlackLevelStage>())
      .Add(std::make_unique<minicam::BilinearDemosaicStage>());

  const auto start = std::chrono::steady_clock::now();
  status = pipeline.Process(frame);
  const auto end = std::chrono::steady_clock::now();
  if (!status.ok()) return Fail(status.ToString());

  status = minicam::SaveRgbPng(frame.rgb, output);
  if (!status.ok()) return Fail(status.ToString());

  const double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "processed " << frame.rgb.width() << "x" << frame.rgb.height()
            << " frame in " << ms << " ms -> " << output << "\n";
  return 0;
}
