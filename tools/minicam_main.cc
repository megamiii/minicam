// minicam command-line tool.
//
//   minicam --input frame.raw --output out.png [options]
//   minicam --input frames_dir/ --output out_dir/ [--mode threaded] [options]
//
// Reads minicam .raw files (see make_synthetic_raw) and runs the ISP:
//   black level -> demosaic -> white balance -> color correction
//   -> tone map -> denoise
// and writes 8-bit sRGB PNGs. Each stage can be selected or turned off.
//
// With a directory as input (stream mode), every .raw file in it is processed
// as one frame sequence, by the thread-per-stage pipeline (--mode threaded,
// the default) or one frame at a time (--mode sequential).

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
#include "minicam/threaded_pipeline.h"

namespace {

constexpr std::string_view kUsage =
    R"(usage: minicam --input <frame.raw> --output <out.png> [options]
       minicam --input <dir with .raw files> --output <out dir> [options]

options:
  --mode threaded|sequential         stream mode pipeline (default: threaded)
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
  std::string mode = "threaded";
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

// Works for both minicam::Pipeline and minicam::ThreadedPipeline.
template <typename PipelineT>
minicam::Status BuildPipeline(const Options& o, PipelineT& pipeline) {
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
    pipeline.Add(
        std::make_unique<minicam::WhiteBalanceStage>(WbMode::kGrayWorld));
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

int RunSingle(const Options& o) {
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

// Processes every .raw file in o.input as one frame sequence.
int RunStream(const Options& o) {
  namespace fs = std::filesystem;
  std::vector<fs::path> inputs;
  std::error_code ec;
  for (const fs::directory_entry& entry : fs::directory_iterator(o.input, ec)) {
    if (entry.path().extension() == ".raw") inputs.push_back(entry.path());
  }
  if (ec) return Fail("cannot read " + o.input + ": " + ec.message());
  if (inputs.empty()) return Fail("no .raw files in " + o.input);
  std::sort(inputs.begin(), inputs.end());
  fs::create_directories(o.output, ec);
  if (ec) return Fail("cannot create " + o.output + ": " + ec.message());

  // The same source and sink drive both modes, so they are compared fairly.
  size_t next = 0;
  auto source = [&](minicam::Frame& frame) -> minicam::StatusOr<bool> {
    if (next == inputs.size()) return false;
    const minicam::FrameMetadata assigned = frame.meta;
    MINICAM_RETURN_IF_ERROR(minicam::ReadRawFile(inputs[next].string(), frame));
    // Keep the pipeline's sequence number and timestamp; the file supplies
    // the sensor metadata.
    frame.meta.frame_id = assigned.frame_id;
    frame.meta.timestamp_ns = assigned.timestamp_ns;
    ++next;
    return true;
  };
  auto sink = [&](minicam::Frame& frame) -> minicam::Status {
    const fs::path out = fs::path(o.output) /
                         (inputs[frame.meta.frame_id].stem().string() + ".png");
    return minicam::SaveRgbPng(frame.rgb, out.string());
  };

  const auto start = std::chrono::steady_clock::now();
  minicam::Status status;
  if (o.mode == "threaded") {
    minicam::ThreadedPipeline pipeline;
    status = BuildPipeline(o, pipeline);
    if (status.ok()) status = pipeline.Run(source, sink);
  } else {
    minicam::Pipeline pipeline;
    status = BuildPipeline(o, pipeline);
    minicam::Frame frame;
    for (uint64_t id = 0; status.ok(); ++id) {
      frame.meta = minicam::FrameMetadata{};
      frame.meta.frame_id = id;
      minicam::StatusOr<bool> produced = source(frame);
      if (!produced.ok()) {
        status = produced.status();
        break;
      }
      if (!*produced) break;
      status = pipeline.Process(frame);
      if (status.ok()) status = sink(frame);
    }
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  if (!status.ok()) return Fail(status.ToString());

  std::cout << std::fixed << std::setprecision(2) << "processed "
            << inputs.size() << " frames (" << o.mode << ") in " << seconds
            << " s, " << static_cast<double>(inputs.size()) / seconds
            << " frames/s -> " << o.output << "\n";
  return 0;
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
    } else if (arg == "--mode") {
      o.mode = value;
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

  if (o.mode != "threaded" && o.mode != "sequential") {
    return Fail("unknown --mode " + o.mode);
  }
  return std::filesystem::is_directory(o.input) ? RunStream(o) : RunSingle(o);
}
