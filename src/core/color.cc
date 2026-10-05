#include "minicam/color.h"

#include <cmath>

namespace minicam {

float SrgbToLinear(float encoded) {
  // Linear segment near black avoids an infinite slope at zero.
  if (encoded <= 0.04045f) return encoded / 12.92f;
  return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float linear) {
  if (linear <= 0.0031308f) return linear * 12.92f;
  return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

}  // namespace minicam
