// Synthetic RAW generation: runs the camera "backwards" so that tests have a
// known ground truth.
//
//   linear RGB image  --mosaic-->  one color per pixel  --scale-->  raw values
//                                                       --noise-->  (optional)
//
// The input image is the ground truth for everything up to demosaicing.

#ifndef MINICAM_IO_SYNTHETIC_H_
#define MINICAM_IO_SYNTHETIC_H_

#include <cstdint>

#include "minicam/cfa.h"
#include "minicam/frame.h"
#include "minicam/image.h"
#include "minicam/status.h"

namespace minicam {

struct SyntheticRawOptions {
  CfaPattern cfa = CfaPattern::kRggb;
  // A 10-bit sensor with a pedestal, so black-level subtraction has work to do.
  uint16_t black_level = 64;
  uint16_t white_level = 1023;

  // Poisson-Gaussian sensor noise, in normalized units (signal in 0..1):
  //   variance(signal) = shot_noise * signal + read_noise^2
  // Shot noise comes from photon counting and grows with brightness; read
  // noise comes from the readout electronics and is constant. Both 0 disables
  // noise.
  float shot_noise = 0.0f;
  float read_noise = 0.0f;
  uint32_t seed = 1;
};

// Fills frame.raw and frame.meta from a 3-channel linear RGB image with values
// in [0, 1]. Width and height must be even and at least 2.
Status MosaicLinearRgb(const Image<float>& linear_rgb,
                       const SyntheticRawOptions& options, Frame& frame);

}  // namespace minicam

#endif  // MINICAM_IO_SYNTHETIC_H_
