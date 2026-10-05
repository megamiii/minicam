#include "minicam/metrics.h"

#include <cmath>
#include <limits>

namespace minicam {

StatusOr<double> Psnr(const Image<float>& a, const Image<float>& b, int border,
                      double peak) {
  if (a.width() != b.width() || a.height() != b.height() ||
      a.channels() != b.channels()) {
    return InvalidArgumentError("images have different sizes");
  }
  if (border < 0 || 2 * border >= a.width() || 2 * border >= a.height()) {
    return InvalidArgumentError("border leaves no pixels to compare");
  }

  double sum_sq = 0.0;
  for (int y = border; y < a.height() - border; ++y) {
    const float* pa = a.row(y);
    const float* pb = b.row(y);
    const int begin = border * a.channels();
    const int end = (a.width() - border) * a.channels();
    for (int i = begin; i < end; ++i) {
      const double d = static_cast<double>(pa[i]) - pb[i];
      sum_sq += d * d;
    }
  }
  const double count = static_cast<double>(a.width() - 2 * border) *
                       (a.height() - 2 * border) * a.channels();
  const double mse = sum_sq / count;
  if (mse == 0.0) return std::numeric_limits<double>::infinity();
  return 10.0 * std::log10(peak * peak / mse);
}

}  // namespace minicam
