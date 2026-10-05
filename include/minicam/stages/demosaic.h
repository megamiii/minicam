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

}  // namespace minicam

#endif  // MINICAM_STAGES_DEMOSAIC_H_
