// Color transfer functions.

#ifndef MINICAM_COLOR_H_
#define MINICAM_COLOR_H_

#include <algorithm>
#include <array>

namespace minicam {

// The sRGB transfer curve (IEC 61966-2-1). Images on disk are stored
// "gamma encoded" so that 8 bits are spent evenly in perceived brightness;
// sensors measure linear light. Both functions map [0, 1] to [0, 1].
float SrgbToLinear(float encoded);
float LinearToSrgb(float linear);

// LinearToSrgb from a table of 4097 samples on [0, 1] with linear
// interpolation, replacing a pow() per value by a multiply, a load pair and
// a lerp. The interpolation error is at most h^2/8 * |f''| (h = 1/4096),
// about 2e-5 near the steepest part of the curve, i.e. ~1% of an 8-bit step.
// The table is 16 KB and stays in L1 cache.
class SrgbEncodeTable {
 public:
  static constexpr int kSize = 4096;

  // Built once on first use (thread-safe), then shared.
  static const SrgbEncodeTable& Get();

  // `linear` must already be clamped to [0, 1].
  float Encode(float linear) const {
    const float position = linear * static_cast<float>(kSize);
    const int i = std::min(static_cast<int>(position), kSize - 1);
    const float t = position - static_cast<float>(i);
    return table_[i] + t * (table_[i + 1] - table_[i]);
  }

 private:
  SrgbEncodeTable();
  std::array<float, kSize + 1> table_{};
};

}  // namespace minicam

#endif  // MINICAM_COLOR_H_
