#include "minicam/stages/color_correction.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace minicam {

Status ColorCorrectionStage::Process(Frame& frame) {
  Image<float>& rgb = frame.rgb;
  if (rgb.empty() || rgb.channels() != 3) {
    return InvalidArgumentError(
        "expected a 3-channel RGB image; run demosaicing first");
  }
  const std::array<float, 9>& m = frame.meta.color_matrix;
  for (float v : m) {
    if (!std::isfinite(v)) {
      return InvalidArgumentError("color matrix contains a non-finite value");
    }
  }

  for (int y = 0; y < rgb.height(); ++y) {
    float* p = rgb.row(y);
    for (int x = 0; x < rgb.width(); ++x, p += 3) {
      const float r = p[0];
      const float g = p[1];
      const float b = p[2];
      p[0] = std::max(m[0] * r + m[1] * g + m[2] * b, 0.0f);
      p[1] = std::max(m[3] * r + m[4] * g + m[5] * b, 0.0f);
      p[2] = std::max(m[6] * r + m[7] * g + m[8] * b, 0.0f);
    }
  }
  return OkStatus();
}

}  // namespace minicam
