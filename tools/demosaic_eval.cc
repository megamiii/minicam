// Demosaicing quality evaluation on a folder of images (e.g. the Kodak set).
//
//   demosaic_eval <image dir> [--border N]
//
// For every PNG in the folder: sample it through an RGGB CFA with an ideal
// sensor (no noise, no quantization), demosaic it with bilinear and
// Malvar-He-Cutler, and compute PSNR over all three channels (often called
// CPSNR) against the original, excluding `border` pixels on each side.
//
// Two domains are reported:
//   srgb    the 8-bit sRGB values are mosaicked directly. This is the usual
//           protocol in the demosaicing literature.
//   linear  the sRGB curve is undone first, as in minicam's own pipeline,
//           where demosaicing runs on linear sensor data.
// Prints a Markdown table.

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "minicam/cfa.h"
#include "minicam/frame.h"
#include "minicam/io/image_io.h"
#include "minicam/io/synthetic.h"
#include "minicam/metrics.h"
#include "minicam/stages/demosaic.h"

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUsage =
    "usage: demosaic_eval <image dir> [--border N]\n";

int Fail(std::string_view message) {
  std::cerr << "error: " << message << "\n" << kUsage;
  return 1;
}

// PSNR of `stage` reconstructing `truth` from its RGGB mosaic.
minicam::StatusOr<double> Evaluate(const minicam::Image<float>& truth,
                                   minicam::Stage& stage, int border) {
  minicam::Frame frame;
  minicam::Status status =
      minicam::SampleCfa(truth, minicam::CfaPattern::kRggb, frame);
  if (status.ok()) status = stage.Process(frame);
  if (!status.ok()) return status;
  return minicam::Psnr(frame.rgb, truth, border);
}

}  // namespace

int main(int argc, char** argv) {
  std::string dir;
  int border = 10;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--border" && i + 1 < argc) {
      const std::string_view value = argv[++i];
      auto [ptr, ec] =
          std::from_chars(value.data(), value.data() + value.size(), border);
      if (ec != std::errc() || ptr != value.data() + value.size()) {
        return Fail("bad --border");
      }
    } else if (dir.empty()) {
      dir = arg;
    } else {
      return Fail("unexpected argument " + std::string(arg));
    }
  }
  if (dir.empty()) return Fail("missing image directory");

  std::error_code ec;
  std::vector<fs::path> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".png") files.push_back(entry.path());
  }
  if (ec) return Fail("cannot read directory " + dir + ": " + ec.message());
  if (files.empty()) return Fail("no .png files in " + dir);
  std::sort(files.begin(), files.end());

  minicam::BilinearDemosaicStage bilinear;
  minicam::MalvarDemosaicStage malvar;
  // [domain][method] running sums for the mean row.
  std::array<std::array<double, 2>, 2> sums{};

  std::cout << "PSNR in dB, RGGB, border " << border << " px excluded.\n\n"
            << "| Image | sRGB: bilinear | sRGB: Malvar | linear: bilinear | "
               "linear: Malvar |\n"
            << "|---|---|---|---|---|\n"
            << std::fixed << std::setprecision(2);

  for (const fs::path& file : files) {
    auto encoded = minicam::LoadImageAsEncodedRgb(file.string());
    auto linear = minicam::LoadImageAsLinearRgb(file.string());
    if (!encoded.ok()) return Fail(encoded.status().ToString());
    if (!linear.ok()) return Fail(linear.status().ToString());

    std::cout << "| " << file.filename().string();
    const std::array<const minicam::Image<float>*, 2> domains = {&*encoded,
                                                                 &*linear};
    const std::array<minicam::Stage*, 2> methods = {&bilinear, &malvar};
    for (int d = 0; d < 2; ++d) {
      for (int m = 0; m < 2; ++m) {
        auto psnr = Evaluate(*domains[d], *methods[m], border);
        if (!psnr.ok()) return Fail(psnr.status().ToString());
        sums[d][m] += *psnr;
        std::cout << " | " << *psnr;
      }
    }
    std::cout << " |\n";
  }

  const auto n = static_cast<double>(files.size());
  std::cout << "| **Mean (" << files.size() << " images)**";
  for (const std::array<double, 2>& domain : sums) {
    for (double sum : domain) std::cout << " | **" << sum / n << "**";
  }
  std::cout << " |\n";
  return 0;
}
