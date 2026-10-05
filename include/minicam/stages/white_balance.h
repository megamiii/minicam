// White balance.

#ifndef MINICAM_STAGES_WHITE_BALANCE_H_
#define MINICAM_STAGES_WHITE_BALANCE_H_

#include <array>
#include <string_view>

#include "minicam/image.h"
#include "minicam/stage.h"

namespace minicam {

// Scales R, G and B of frame.rgb (linear) so neutral surfaces become neutral.
//
// kGrayWorld assumes the scene averages to gray:
//   gain_R = mean(G) / mean(R), gain_G = 1, gain_B = mean(G) / mean(B)
// Pixels that are nearly clipped (any channel >= 0.98) or nearly black (all
// channels <= 0.002) are left out of the means, because their color ratios
// are unreliable. The estimated gains are written to frame.meta.wb_gains.
//
// kFromMetadata applies frame.meta.wb_gains as given (e.g. the camera's
// as-shot gains from a DNG).
//
// In both modes the gains are rescaled so the smallest is 1 and the result is
// clipped to 1, so blown highlights stay white instead of turning pink or
// yellow. The rescaling brightens the image slightly; tone mapping absorbs
// that.
class WhiteBalanceStage final : public Stage {
 public:
  enum class Mode { kGrayWorld, kFromMetadata };

  explicit WhiteBalanceStage(Mode mode = Mode::kGrayWorld) : mode_(mode) {}

  std::string_view Name() const override {
    return mode_ == Mode::kGrayWorld ? "awb_gray_world" : "wb_from_metadata";
  }
  Status Process(Frame& frame) override;

 private:
  Mode mode_;
};

// Gray-world gain estimate (G anchored at 1), exposed for testing.
std::array<float, 3> EstimateGrayWorldGains(const Image<float>& rgb);

}  // namespace minicam

#endif  // MINICAM_STAGES_WHITE_BALANCE_H_
