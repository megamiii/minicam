// minicam command-line tool.
//
//   minicam --input frame.raw --output out.png [options]
//
// Reads a minicam .raw file (see make_synthetic_raw) and runs the ISP:
//   black level -> demosaic -> white balance -> color correction
//   -> tone map -> denoise
// and writes an 8-bit sRGB PNG. Each stage can be selected or turned off.

#include <charconv>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "minicam/frame.h"
#include "minicam/io/image_io.h"
#include "minicam/io/raw_file.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/color_correction.h"
#include "minicam/stages/demosaic.h"
#include "minicam/stages/denoise.h"
#include "minicam/stages/tone_map.h"
#include "minicam/stages/white_balance.h"

namespace {

constexpr std::string_view kUsage = R"(usage: minicam --input <frame.raw> --output <out.png> [options]

options:
  --demosaic bilinear|malvar         demosaicing algorithm (default: malvar)
  --wb gray-world|metadata|off       white balance (default: gray-world)
  --ccm on|off                       color correction matrix (default: on)
  --tone reinhard|clip               tone mapping operator (default: reinhard)
  --exposure EV                      extra exposure in stops (default: 0)
  --denoise on|off                   bilateral denoising (default: on)
  --sigma-spatial S, --sigma-range R bilateral parameters (default: 2, 0.08)
)";

struct Options {
  std::string input;
  std::string output;
  std::string demosaic = "malvar";
  std::string wb = "gray-world";
  std::string ccm = "on";
  std::string tone = "reinhard";
  float exposure_ev = 0.0f;
  std::string denoise = "on";
  minicam::BilateralOptions bilateral;
};

int Fail(std::string_view message) {
  std::cerr << "error: " << message << "\n" << kUsage;
  return 1;
}

bool ParseFloat(std::string_view text, float& value) {
  const char* end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(text.data(), end, value);
  return ec == std::errc() && ptr == end;
}

minicam::Status BuildPipeline(const Options& o, minicam::Pipeline& pipeline) {
  using minicam::InvalidArgumentError;
  pipeline.Add(std::make_unique<minicam::BlackLevelStage>());

  if (o.demosaic == "malvar") {
    pipeline.Add(std::make_unique<minicam::MalvarDemosaicStage>());
  } else if (o.demosaic == "bilinear") {
    pipeline.Add(std::make_unique<minicam::BilinearDemosaicStage>());
  } else {
    return InvalidArgumentError("unknown --demosaic " + o.demosaic);
  }

  using WbMode = minicam::WhiteBalanceStage::Mode;
  if (o.wb == "gray-world") {
    pipeline.Add(std::make_unique<minicam::WhiteBalanceStage>(WbMode::kGrayWorld));
  } else if (o.wb == "metadata") {
    pipeline.Add(
        std::make_unique<minicam::WhiteBalanceStage>(WbMode::kFromMetadata));
  } else if (o.wb != "off") {
    return InvalidArgumentError("unknown --wb " + o.wb);
  }

  if (o.ccm == "on") {
    pipeline.Add(std::make_unique<minicam::ColorCorrectionStage>());
  } else if (o.ccm != "off") {
    return InvalidArgumentError("unknown --ccm " + o.ccm);
  }

  minicam::ToneMapOptions tone;
  tone.exposure_ev = o.exposure_ev;
  if (o.tone == "reinhard") {
    tone.op = minicam::ToneMapOptions::Operator::kReinhard;
  } else if (o.tone == "clip") {
    tone.op = minicam::ToneMapOptions::Operator::kClip;
  } else {
    return InvalidArgumentError("unknown --tone " + o.tone);
  }
  pipeline.Add(std::make_unique<minicam::ToneMapStage>(tone));

  if (o.denoise == "on") {
    pipeline.Add(std::make_unique<minicam::BilateralDenoiseStage>(o.bilateral));
  } else if (o.denoise != "off") {
    return InvalidArgumentError("unknown --denoise " + o.denoise);
  }
  return minicam::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (i + 1 >= argc) return Fail("missing value for " + std::string(arg));
    const std::string_view value = argv[++i];
    if (arg == "--input") {
      o.input = value;
    } else if (arg == "--output") {
      o.output = value;
    } else if (arg == "--demosaic") {
      o.demosaic = value;
    } else if (arg == "--wb") {
      o.wb = value;
    } else if (arg == "--ccm") {
      o.ccm = value;
    } else if (arg == "--tone") {
      o.tone = value;
    } else if (arg == "--exposure") {
      if (!ParseFloat(value, o.exposure_ev)) return Fail("bad --exposure");
    } else if (arg == "--denoise") {
      o.denoise = value;
    } else if (arg == "--sigma-spatial") {
      if (!ParseFloat(value, o.bilateral.sigma_spatial)) {
        return Fail("bad --sigma-spatial");
      }
    } else if (arg == "--sigma-range") {
      if (!ParseFloat(value, o.bilateral.sigma_range)) {
        return Fail("bad --sigma-range");
      }
    } else {
      return Fail("unknown argument " + std::string(arg));
    }
  }
  if (o.input.empty() || o.output.empty()) {
    return Fail("--input and --output are required");
  }

  minicam::Pipeline pipeline;
  minicam::Status status = BuildPipeline(o, pipeline);
  if (!status.ok()) return Fail(status.message());

  minicam::Frame frame;
  status = minicam::ReadRawFile(o.input, frame);
  if (!status.ok()) return Fail(status.ToString());

  std::vector<double> stage_ms;
  status = pipeline.Process(frame, &stage_ms);
  if (!status.ok()) return Fail(status.ToString());

  status = minicam::SaveRgbPng(frame.rgb, o.output);
  if (!status.ok()) return Fail(status.ToString());

  std::cout << "processed " << frame.rgb.width() << "x" << frame.rgb.height()
            << " -> " << o.output << "\n";
  double total_ms = 0.0;
  const std::vector<std::string_view> names = pipeline.StageNames();
  std::cout << std::fixed << std::setprecision(2);
  for (size_t i = 0; i < stage_ms.size(); ++i) {
    std::cout << "  " << std::left << std::setw(20) << names[i] << std::right
              << std::setw(10) << stage_ms[i] << " ms\n";
    total_ms += stage_ms[i];
  }
  std::cout << "  " << std::left << std::setw(20) << "total" << std::right
            << std::setw(10) << total_ms << " ms\n";
  const auto& g = frame.meta.wb_gains;
  std::cout << "  white balance gains (R, G, B): " << std::setprecision(3)
            << g[0] << ", " << g[1] << ", " << g[2] << "\n";
  return 0;
}
