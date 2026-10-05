// Color correction matrix (CCM).

#ifndef MINICAM_STAGES_COLOR_CORRECTION_H_
#define MINICAM_STAGES_COLOR_CORRECTION_H_

#include <string_view>

#include "minicam/stage.h"

namespace minicam {

// Maps white-balanced camera RGB to linear sRGB with the 3x3 matrix in
// frame.meta.color_matrix (row-major): out = M * in, per pixel.
//
// Sensor color filters overlap more than the sRGB primaries, so camera colors
// are desaturated; the negative off-diagonal terms undo that overlap. Rows of
// a calibrated CCM sum to 1, so neutral colors stay neutral after white
// balance. Must run on linear data, before the tone curve.
//
// Negative results (colors outside the sRGB gamut) are clamped to 0. Values
// above 1 are kept as highlight headroom for tone mapping.
class ColorCorrectionStage final : public Stage {
 public:
  std::string_view Name() const override { return "color_correction"; }
  Status Process(Frame& frame) override;
};

}  // namespace minicam

#endif  // MINICAM_STAGES_COLOR_CORRECTION_H_
