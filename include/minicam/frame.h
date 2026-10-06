// A frame and the metadata that travels with it through the pipeline.

#ifndef MINICAM_FRAME_H_
#define MINICAM_FRAME_H_

#include <array>
#include <cstdint>
#include <vector>

#include "minicam/cfa.h"
#include "minicam/image.h"

namespace minicam {

struct FrameMetadata {
  uint64_t frame_id = 0;
  // Capture time in nanoseconds.
  int64_t timestamp_ns = 0;
  double exposure_time_s = 0.0;
  float analog_gain = 1.0f;
  // Clockwise quarter turns (0-3) needed to display the image upright. The
  // sensor always reads out in landscape; this comes from the RAW file's
  // Orientation tag and is applied only when the output is written.
  int orientation_quarter_turns = 0;

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

// An axis-aligned box found by a detection stage, in pixels of frame.rgb.
struct Detection {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  float score = 0.0f;
};

// A frame owns one buffer per representation it passes through:
//   raw   (uint16, 1 channel)  sensor values, as read from the file
//   bayer (float,  1 channel)  black-level-corrected mosaic, normalized to 0..1
//   rgb   (float,  3 channels) linear RGB after demosaicing, edited in place by
//                              the later stages
// Keeping all three in the frame lets a buffer pool recycle whole frames.
// `detections` is filled by optional detection stages; it lives in the frame
// (not the metadata) so its memory is reused with the frame.
struct Frame {
  FrameMetadata meta;
  Image<uint16_t> raw;
  Image<float> bayer;
  Image<float> rgb;
  std::vector<Detection> detections;
};

}  // namespace minicam

#endif  // MINICAM_FRAME_H_
