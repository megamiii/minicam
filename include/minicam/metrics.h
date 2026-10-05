// Image quality metrics.

#ifndef MINICAM_METRICS_H_
#define MINICAM_METRICS_H_

#include "minicam/image.h"
#include "minicam/status.h"

namespace minicam {

// Peak signal-to-noise ratio in dB between two images of the same size:
//   PSNR = 10 * log10(peak^2 / MSE)
// Higher is better; +6 dB means the RMS error halved. `border` pixels on each
// side are excluded, so edge handling does not dominate the score. Identical
// images return +infinity.
StatusOr<double> Psnr(const Image<float>& a, const Image<float>& b,
                      int border = 0, double peak = 1.0);

}  // namespace minicam

#endif  // MINICAM_METRICS_H_
