// Tone mapping and gamma encoding.

#ifndef MINICAM_STAGES_TONE_MAP_H_
#define MINICAM_STAGES_TONE_MAP_H_

#include <string_view>
#include <vector>

#include "minicam/stage.h"

namespace minicam {

struct ToneMapOptions {
  enum class Operator {
    // Exposure, clip to [0, 1], sRGB encode. A baseline without a curve.
    kClip,
    // Reinhard et al. 2002 global operator on luminance, then sRGB encode.
    kReinhard,
  };
  Operator op = Operator::kReinhard;

  // Scale the image so its log-average luminance equals `key` (what a
  // camera's auto-exposure meter does). 0.18 is photographic middle gray.
  bool auto_exposure = true;
  float key = 0.18f;
  // Upper limit for the auto-exposure gain, so a nearly black frame is not
  // amplified into pure noise.
  float max_auto_gain = 64.0f;
  // Additional exposure in stops (factor 2^ev), applied after auto exposure.
  float exposure_ev = 0.0f;
  // kReinhard: the white point is this percentile of the exposed luminance
  // (at least 1.0), so a few hot pixels do not set it.
  float white_percentile = 0.995f;
};

// Converts frame.rgb from linear sRGB (any range >= 0) to display-ready,
// sRGB-encoded values in [0, 1]:
//
//   L     = 0.2126 R + 0.7152 G + 0.0722 B
//   L_avg = exp(mean(log(1e-4 + L)))           log-average (geometric mean)
//   L_s   = (key / L_avg) * 2^ev * L           exposure
//   L_d   = L_s (1 + L_s / L_w^2) / (1 + L_s)  Reinhard curve, L_w -> 1.0
//   RGB  *= L_d / L                            same factor for all channels,
//                                              so hue and saturation are kept
//   RGB   = sRGB_encode(clip(RGB, 0, 1))
//
// After this stage frame.rgb is no longer linear.
class ToneMapStage final : public Stage {
 public:
  explicit ToneMapStage(ToneMapOptions options = {}) : options_(options) {}

  std::string_view Name() const override {
    return options_.op == ToneMapOptions::Operator::kReinhard
               ? "tone_map_reinhard"
               : "tone_map_clip";
  }
  Status Process(Frame& frame) override;

 private:
  ToneMapOptions options_;
  // Scratch space for the white-point percentile, reused across frames.
  std::vector<float> luminance_;
};

}  // namespace minicam

#endif  // MINICAM_STAGES_TONE_MAP_H_
