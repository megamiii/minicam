// minicam command-line tool.
//
//   minicam --input frame.dng --output out.png [options]
//   minicam --input burst_dir/ --output out_dir/ [--mode threaded] [options]
//
// Reads camera RAW files (.dng, via LibRaw) or minicam .raw files (see
// make_synthetic_raw) and runs the ISP:
//   black level -> demosaic -> white balance -> color correction
//   -> tone map -> denoise
// and writes 8-bit sRGB PNGs. Each stage can be selected or turned off.
//
// With a directory as input (stream mode), every .dng/.raw file in it is
// processed as one frame sequence, by the thread-per-stage pipeline
// (--mode threaded, the default) or one frame at a time (--mode sequential).
// An HDR+ "rgb2rgb.txt" next to the input replaces the file's color matrix.

#include <algorithm>
#include <cctype>
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

#include "minicam/color.h"
#include "minicam/frame.h"
#include "minicam/io/dng_reader.h"
#include "minicam/io/image_io.h"
#include "minicam/io/raw_file.h"
#include "minicam/pipeline.h"
#include "minicam/stages/black_level.h"
#include "minicam/stages/color_correction.h"
#include "minicam/stages/demosaic.h"
#include "minicam/stages/denoise.h"
#include "minicam/stages/face_detect.h"
#include "minicam/stages/tone_map.h"
#include "minicam/stages/white_balance.h"
#include "minicam/threaded_pipeline.h"

namespace {

constexpr std::string_view kUsage =
    R"(usage: minicam --input <frame.dng|frame.raw> --output <out.png> [options]
       minicam --input <dir with .dng/.raw files> --output <out dir> [options]

options:
  --mode threaded|sequential         stream mode pipeline (default: threaded)
  --demosaic bilinear|malvar         demosaicing algorithm (default: malvar)
  --wb gray-world|metadata|off       white balance (default: gray-world)
  --ccm on|off                       color correction matrix (default: on)
  --tone reinhard|clip               tone mapping operator (default: reinhard)
  --exposure EV                      extra exposure in stops (default: 0)
  --denoise on|off                   bilateral denoising (default: on)
  --sigma-spatial S, --sigma-range R bilateral parameters (default: 2, 0.08)
  --raw-preview <png>                single file: also save the black-level
                                     corrected Bayer mosaic as grayscale
  --detect-faces <template image>    add HOG face detection (template
                                     matching) and draw the boxes
  --detect-downscale N               detect on the image shrunk N times
  --ncc-threshold T                  detection threshold (default: 0.49)
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
  std::string raw_preview;  // Single-file mode: also save the RAW mosaic.
  std::string face_template;
  minicam::FaceDetectionOptions faces;
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

  if (!o.face_template.empty()) {
    // The template is compared with the final, sRGB-encoded image, so it is
    // loaded without linearization.
    auto rgb = minicam::LoadImageAsEncodedRgb(o.face_template);
    if (!rgb.ok()) return rgb.status();
    minicam::Image<float> gray;
    minicam::RgbToGray(*rgb, gray);
    auto stage = minicam::HogFaceDetectionStage::Create(gray, o.faces);
    if (!stage.ok()) return stage.status();
    pipeline.Add(std::move(*stage));
  }
  return minicam::OkStatus();
}

std::string LowerExtension(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

bool IsInputFile(const std::filesystem::path& path) {
  const std::string ext = LowerExtension(path);
  return ext == ".dng" || ext == ".raw";
}

// Reads .dng files with LibRaw and .raw files with minicam's own reader. If
// the folder holds an HDR+ rgb2rgb.txt, it replaces the DNG color matrix.
class InputReader {
 public:
  static minicam::StatusOr<std::unique_ptr<InputReader>> Create(
      const std::filesystem::path& folder) {
    auto reader = std::make_unique<InputReader>();
    const std::filesystem::path sidecar = folder / "rgb2rgb.txt";
    if (std::filesystem::exists(sidecar)) {
      auto matrix = minicam::ReadColorMatrixFile(sidecar.string());
      if (!matrix.ok()) return matrix.status();
      reader->dng_.SetColorMatrixOverride(*matrix);
      reader->has_sidecar_ = true;
    }
    return reader;
  }

  minicam::Status Read(const std::filesystem::path& path,
                       minicam::Frame& frame) {
    if (LowerExtension(path) == ".dng") return dng_.Read(path.string(), frame);
    return minicam::ReadRawFile(path.string(), frame);
  }

  bool has_sidecar() const { return has_sidecar_; }

 private:
  minicam::DngReader dng_;
  bool has_sidecar_ = false;
};

// Writes frame.rgb as a PNG, rotated upright according to the frame's
// orientation. `scratch` holds the rotated copy and is reused across frames.
minicam::Status SaveUpright(const minicam::Frame& frame,
                            const std::string& path,
                            minicam::Image<float>& scratch) {
  if (frame.meta.orientation_quarter_turns == 0) {
    return minicam::SaveRgbPng(frame.rgb, path);
  }
  minicam::RotateClockwise(frame.rgb, frame.meta.orientation_quarter_turns,
                           scratch);
  return minicam::SaveRgbPng(scratch, path);
}

// Saves the normalized Bayer mosaic (frame.bayer) as an upright grayscale
// PNG: what the sensor recorded, one color sample per pixel. Brightened so
// the mean is middle gray and sRGB-encoded, so it can be viewed.
minicam::Status SaveRawPreview(const minicam::Frame& frame,
                               const std::string& path) {
  const minicam::Image<float>& bayer = frame.bayer;
  double sum = 0.0;
  for (float v : bayer.pixels()) sum += v;
  const double mean = sum / static_cast<double>(bayer.size());
  const auto gain = static_cast<float>(mean > 0.0 ? 0.18 / mean : 1.0);
  minicam::Frame preview;
  preview.meta = frame.meta;
  preview.rgb.Resize(bayer.width(), bayer.height(), 3);
  for (int y = 0; y < bayer.height(); ++y) {
    for (int x = 0; x < bayer.width(); ++x) {
      const float v =
          minicam::LinearToSrgb(std::min(bayer.at(x, y) * gain, 1.0f));
      for (int c = 0; c < 3; ++c) preview.rgb.at(x, y, c) = v;
    }
  }
  minicam::Image<float> upright;
  return SaveUpright(preview, path, upright);
}

int RunSingle(const Options& o) {
  minicam::Pipeline pipeline;
  minicam::Status status = BuildPipeline(o, pipeline);
  if (!status.ok()) return Fail(status.message());

  auto reader =
      InputReader::Create(std::filesystem::path(o.input).parent_path());
  if (!reader.ok()) return Fail(reader.status().ToString());
  minicam::Frame frame;
  status = (*reader)->Read(o.input, frame);
  if (!status.ok()) return Fail(status.ToString());
  if ((*reader)->has_sidecar()) {
    std::cout << "using rgb2rgb.txt color matrix\n";
  }

  std::vector<double> stage_ms;
  status = pipeline.Process(frame, &stage_ms);
  if (!status.ok()) return Fail(status.ToString());

  for (const minicam::Detection& d : frame.detections) {
    std::cout << "face at (" << d.x << ", " << d.y << ") size " << d.width
              << "x" << d.height << ", NCC " << d.score << "\n";
  }
  minicam::DrawDetections(frame.detections, frame.rgb);

  minicam::Image<float> upright;
  status = SaveUpright(frame, o.output, upright);
  if (status.ok() && !o.raw_preview.empty()) {
    status = SaveRawPreview(frame, o.raw_preview);
  }
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

// Processes every .dng/.raw file in o.input as one frame sequence.
int RunStream(const Options& o) {
  namespace fs = std::filesystem;
  std::vector<fs::path> inputs;
  std::error_code ec;
  for (const fs::directory_entry& entry : fs::directory_iterator(o.input, ec)) {
    if (IsInputFile(entry.path())) inputs.push_back(entry.path());
  }
  if (ec) return Fail("cannot read " + o.input + ": " + ec.message());
  if (inputs.empty()) return Fail("no .dng or .raw files in " + o.input);
  std::sort(inputs.begin(), inputs.end());
  fs::create_directories(o.output, ec);
  if (ec) return Fail("cannot create " + o.output + ": " + ec.message());

  auto reader = InputReader::Create(o.input);
  if (!reader.ok()) return Fail(reader.status().ToString());

  // The same source and sink drive both modes, so they are compared fairly.
  // The source runs on the source thread, so file decoding overlaps with
  // processing of earlier frames.
  size_t next = 0;
  auto source = [&](minicam::Frame& frame) -> minicam::StatusOr<bool> {
    if (next == inputs.size()) return false;
    const minicam::FrameMetadata assigned = frame.meta;
    MINICAM_RETURN_IF_ERROR((*reader)->Read(inputs[next], frame));
    // Keep the pipeline's sequence number and timestamp; the file supplies
    // the sensor metadata.
    frame.meta.frame_id = assigned.frame_id;
    frame.meta.timestamp_ns = assigned.timestamp_ns;
    ++next;
    return true;
  };
  minicam::Image<float> upright;  // Only touched by the sink thread.
  auto sink = [&](minicam::Frame& frame) -> minicam::Status {
    const fs::path out = fs::path(o.output) /
                         (inputs[frame.meta.frame_id].stem().string() + ".png");
    minicam::DrawDetections(frame.detections, frame.rgb);
    return SaveUpright(frame, out.string(), upright);
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
    } else if (arg == "--raw-preview") {
      o.raw_preview = value;
    } else if (arg == "--detect-faces") {
      o.face_template = value;
    } else if (arg == "--detect-downscale") {
      auto [ptr, ec] = std::from_chars(
          value.data(), value.data() + value.size(), o.faces.downscale);
      if (ec != std::errc() || ptr != value.data() + value.size()) {
        return Fail("bad --detect-downscale");
      }
    } else if (arg == "--ncc-threshold") {
      if (!ParseFloat(value, o.faces.ncc_threshold)) {
        return Fail("bad --ncc-threshold");
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

  if (o.mode != "threaded" && o.mode != "sequential") {
    return Fail("unknown --mode " + o.mode);
  }
  return std::filesystem::is_directory(o.input) ? RunStream(o) : RunSingle(o);
}
