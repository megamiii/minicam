// A frame and the metadata that travels with it through the pipeline.

#ifndef MINICAM_FRAME_H_
#define MINICAM_FRAME_H_

#include <array>
#include <cstdint>

#include "minicam/cfa.h"
#include "minicam/image.h"

namespace minicam {

struct FrameMetadata {
  uint64_t frame_id = 0;
  // Capture time in nanoseconds.
  int64_t timestamp_ns = 0;
  double exposure_time_s = 0.0;
  float analog_gain = 1.0f;

  // Sensor description, read from the RAW file.
  CfaPattern cfa = CfaPattern::kRggb;
  // Black level per position in the 2x2 CFA tile (see CfaTileIndex). Sensors
  // may use a different pedestal for each of the four photosites.
  std::array<uint16_t, 4> black_level = {0, 0, 0, 0};
  // Raw value at which the sensor saturates.
  uint16_t white_level = 0;

  // White balance gains for R, G, B. Initially the camera's as-shot gains;
  // the auto white balance stage overwrites them with the gains it applied.
  std::array<float, 3> wb_gains = {1.0f, 1.0f, 1.0f};
  // 3x3 row-major matrix from white-balanced camera RGB to linear sRGB.
  std::array<float, 9> color_matrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// A frame owns one buffer per representation it passes through:
//   raw   (uint16, 1 channel)  sensor values, as read from the file
//   bayer (float,  1 channel)  black-level-corrected mosaic, normalized to 0..1
//   rgb   (float,  3 channels) linear RGB after demosaicing, edited in place by
//                              the later stages
// Keeping all three in the frame lets a buffer pool recycle whole frames.
struct Frame {
  FrameMetadata meta;
  Image<uint16_t> raw;
  Image<float> bayer;
  Image<float> rgb;
};

}  // namespace minicam

#endif  // MINICAM_FRAME_H_
