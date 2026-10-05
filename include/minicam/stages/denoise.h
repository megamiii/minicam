// Edge-preserving denoising.

#ifndef MINICAM_STAGES_DENOISE_H_
#define MINICAM_STAGES_DENOISE_H_

#include <string_view>
#include <vector>

#include "minicam/image.h"
#include "minicam/stage.h"

namespace minicam {

struct BilateralOptions {
  // Spatial Gaussian sigma, in pixels. The window radius is ceil(2 sigma).
  float sigma_spatial = 2.0f;
  // Range Gaussian sigma, in pixel-value units (0..1). Color differences
  // well below it are smoothed as noise; differences well above it are kept
  // as edges. Set it somewhat above the noise level.
  float sigma_range = 0.08f;
};

// Bilateral filter (Tomasi and Manduchi, 1998) on frame.rgb:
//
//   out(p) = sum_q ws(p,q) wr(p,q) I(q) / sum_q ws(p,q) wr(p,q)
//   ws = exp(-|p - q|^2 / (2 sigma_s^2))     nearby pixels count more
//   wr = exp(-|I(p) - I(q)|^2 / (2 sigma_r^2)) similar colors count more
//
// |I(p) - I(q)| is the Euclidean distance between the RGB colors, and the
// same weight is used for all three channels, which avoids color fringes.
// Pixels outside the image are skipped and the weights renormalized.
//
// Intended to run after tone mapping: the sRGB curve makes the noise level
// roughly independent of brightness, so one sigma_range fits the whole image.
class BilateralDenoiseStage final : public Stage {
 public:
  explicit BilateralDenoiseStage(BilateralOptions options = {});

  std::string_view Name() const override { return "denoise_bilateral"; }
  Status Process(Frame& frame) override;

 private:
  BilateralOptions options_;
  int radius_ = 0;
  // Spatial weights for the (2r+1) x (2r+1) window, row-major.
  std::vector<float> spatial_weights_;
  // Output buffer, swapped with frame.rgb after filtering so no frame after
  // the first allocates.
  Image<float> scratch_;
};

}  // namespace minicam

#endif  // MINICAM_STAGES_DENOISE_H_
