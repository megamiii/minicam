// Converts an ordinary sRGB image into a synthetic RAW Bayer file.
//
//   make_synthetic_raw input.png output.raw [--cfa RGGB] [--shot-noise A]
//                      [--read-noise S] [--seed N]

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "minicam/cfa.h"
#include "minicam/frame.h"
#include "minicam/io/image_io.h"
#include "minicam/io/raw_file.h"
#include "minicam/io/synthetic.h"

namespace {

constexpr std::string_view kUsage =
    "usage: make_synthetic_raw <input image> <output.raw> [--cfa RGGB|BGGR|"
    "GRBG|GBRG]\n"
    "                          [--shot-noise A] [--read-noise S] [--seed N]\n";

template <typename T>
bool ParseNumber(std::string_view text, T& value) {
  const char* end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(text.data(), end, value);
  return ec == std::errc() && ptr == end;
}

int Fail(std::string_view message) {
  std::cerr << "error: " << message << "\n" << kUsage;
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input;
  std::string output;
  minicam::SyntheticRawOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--cfa" && has_value) {
      auto cfa = minicam::ParseCfaPattern(argv[++i]);
      if (!cfa) return Fail("unknown CFA pattern");
      options.cfa = *cfa;
    } else if (arg == "--shot-noise" && has_value) {
      if (!ParseNumber(argv[++i], options.shot_noise)) {
        return Fail("bad --shot-noise");
      }
    } else if (arg == "--read-noise" && has_value) {
      if (!ParseNumber(argv[++i], options.read_noise)) {
        return Fail("bad --read-noise");
      }
    } else if (arg == "--seed" && has_value) {
      if (!ParseNumber(argv[++i], options.seed)) return Fail("bad --seed");
    } else if (arg.starts_with("--")) {
      return Fail("unknown or incomplete option " + std::string(arg));
    } else if (input.empty()) {
      input = arg;
    } else if (output.empty()) {
      output = arg;
    } else {
      return Fail("too many arguments");
    }
  }
  if (output.empty()) return Fail("missing input or output path");

  auto linear = minicam::LoadImageAsLinearRgb(input);
  if (!linear.ok()) return Fail(linear.status().ToString());

  // Bayer images need even dimensions; drop a trailing row/column if needed.
  minicam::Image<float>& image = *linear;
  const int width = image.width() & ~1;
  const int height = image.height() & ~1;
  if (width != image.width() || height != image.height()) {
    minicam::Image<float> cropped(width, height, 3);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        for (int c = 0; c < 3; ++c) cropped.at(x, y, c) = image.at(x, y, c);
      }
    }
    image = std::move(cropped);
  }

  minicam::Frame frame;
  minicam::Status status = minicam::MosaicLinearRgb(image, options, frame);
  if (status.ok()) status = minicam::WriteRawFile(frame, output);
  if (!status.ok()) return Fail(status.ToString());

  std::cout << "wrote " << output << ": " << width << "x" << height << ", "
            << minicam::CfaPatternName(options.cfa)
            << ", black=" << options.black_level
            << ", white=" << options.white_level << "\n";
  return 0;
}
