// Demosaicing: reconstructs full RGB from a Bayer mosaic.

#ifndef MINICAM_STAGES_DEMOSAIC_H_
#define MINICAM_STAGES_DEMOSAIC_H_

#include <string_view>

#include "minicam/stage.h"

namespace minicam {

// Bilinear demosaicing. Reads frame.bayer, writes frame.rgb (3 channels).
//
// Each pixel keeps the color it measured and takes each missing color as the
// average of the nearest neighbors that measured it:
//   - green at a red/blue pixel:   the 4 neighbors up, down, left, right
//   - red/blue at a green pixel:   the 2 neighbors left+right or up+down,
//                                  whichever pair has that color
//   - red at blue / blue at red:   the 4 diagonal neighbors
// This equals convolving each sparse color plane with
//   R, B: [1 2 1; 2 4 2; 1 2 1] / 4      G: [0 1 0; 1 4 1; 0 1 0] / 4
// Pixels outside the image are mirrored (reflect-101), which keeps the CFA
// phase intact at the borders.
//
// It is fast but blurs edges and causes color fringes ("zipper" artifacts),
// because each color is interpolated without looking at the other colors.
class BilinearDemosaicStage final : public Stage {
 public:
  std::string_view Name() const override { return "demosaic_bilinear"; }
  Status Process(Frame& frame) override;
};

// Malvar-He-Cutler demosaicing (ICASSP 2004). Reads frame.bayer, writes
// frame.rgb.
//
// Starts from the bilinear estimate and adds a scaled Laplacian of the color
// measured at the pixel, e.g. for green at a red pixel:
//
//   G = G_bilinear + 1/2 * (R(x,y) - mean of the 4 reds two pixels away)
//
// The idea is that color differences (R - G) vary more slowly than the colors
// themselves, so an edge measured in one channel also predicts the edge in the
// others. Each case collapses into one fixed 5x5 kernel (weights / 8, gains
// alpha = 1/2, beta = 5/8, gamma = 3/4 from the paper), so the cost stays a
// small linear filter. Output is clamped to [0, 1] because the correction can
// overshoot at sharp edges.
struct MalvarOptions {
  enum class Implementation {
    // Every neighbor read goes through border mirroring. Kept as the
    // correctness reference and the "before" of the optimization.
    kReference,
    // Mirroring only on the 2-pixel image border; interior pixels read
    // through five row pointers. Bit-identical to kReference.
    kFast,
  };
  Implementation implementation = Implementation::kFast;
  // Threads for row bands (kFast); 0 means one per hardware thread.
  int num_threads = 0;
};

class MalvarDemosaicStage final : public Stage {
 public:
  explicit MalvarDemosaicStage(MalvarOptions options = {})
      : options_(options) {}

  std::string_view Name() const override { return "demosaic_malvar"; }
  Status Process(Frame& frame) override;

 private:
  MalvarOptions options_;
};

}  // namespace minicam

#endif  // MINICAM_STAGES_DEMOSAIC_H_
