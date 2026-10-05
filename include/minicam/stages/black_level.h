// Black-level subtraction and normalization.

#ifndef MINICAM_STAGES_BLACK_LEVEL_H_
#define MINICAM_STAGES_BLACK_LEVEL_H_

#include <string_view>

#include "minicam/stage.h"

namespace minicam {

// Reads frame.raw (uint16 mosaic) and writes frame.bayer (float mosaic):
//
//   bayer = clamp((raw - black[tile]) / (white - black[tile]), 0, 1)
//
// where black[tile] is the black level for the pixel's position in the 2x2
// CFA tile. Afterwards 0 means "no light" and 1 means "sensor saturated",
// independent of the sensor's bit depth.
//
// Clamping at 0 throws away the negative half of the noise in very dark
// regions, which slightly brightens them on average. That is acceptable here;
// a production ISP would keep signed values until after denoising.
class BlackLevelStage final : public Stage {
 public:
  std::string_view Name() const override { return "black_level"; }
  Status Process(Frame& frame) override;
};

}  // namespace minicam

#endif  // MINICAM_STAGES_BLACK_LEVEL_H_
