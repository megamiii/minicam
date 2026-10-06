// Histogram of Oriented Gradients (Dalal and Triggs, 2005).

#ifndef MINICAM_HOG_H_
#define MINICAM_HOG_H_

#include <array>
#include <cstdint>
#include <vector>

#include "minicam/image.h"

namespace minicam {

inline constexpr int kHogBins = 6;  // Unsigned orientations, 30 degrees each.

struct HogParams {
  int cell_size = 8;   // Pixels per cell side.
  int block_size = 2;  // Cells per block side; blocks slide by one cell.
};

using HogHistogram = std::array<float, kHogBins>;

// Per-pixel gradient of a grayscale image.
struct GradientField {
  Image<float> magnitude;  // sqrt(dx^2 + dy^2) from 3x3 Sobel filters.
  Image<uint8_t> bin;      // Orientation bin in 0..5.
};

// Orientation bin of a gradient: the unsigned angle atan2(dy, dx) folded into
// [0, 180) degrees, bins centered at 0, 30, ..., 150 (edges 15, 45, ..., 165;
// angles >= 165 wrap to bin 0).
int OrientationBin(float dx, float dy);

// Sobel gradients of a 1-channel image. Border pixels replicate their edge.
void ComputeGradients(const Image<float>& gray, GradientField& out);

// Sum of magnitudes per bin over every rectangle in O(1): one integral image
// per bin (Porikli 2005), so a cell histogram at any position costs 4 lookups
// per bin instead of summing cell_size^2 pixels.
class IntegralHistogram {
 public:
  void Build(const GradientField& gradients);

  // Histogram of the rectangle [x, x + width) x [y, y + height).
  HogHistogram Sum(int x, int y, int width, int height) const;

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  int width_ = 0;
  int height_ = 0;
  // (width + 1) x (height + 1) entries of kHogBins doubles; double because
  // sums over millions of pixels lose precision in float.
  std::vector<double> sums_;
};

// HOG descriptor of the window [x, x + width) x [y, y + height): cell
// histograms on a grid anchored at (x, y), then L2-normalized 2x2-cell blocks
// (epsilon 0.001), concatenated block by block in row-major order.
void WindowDescriptor(const IntegralHistogram& integral, int x, int y,
                      int width, int height, const HogParams& params,
                      std::vector<float>& out);

// Descriptor of a whole image (e.g. a template); same layout as
// WindowDescriptor over the full image.
std::vector<float> ExtractHog(const Image<float>& gray,
                              const HogParams& params);

// Number of descriptor values for a window of this size.
int DescriptorLength(int width, int height, const HogParams& params);

}  // namespace minicam

#endif  // MINICAM_HOG_H_
