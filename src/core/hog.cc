#include "minicam/hog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace minicam {
namespace {

constexpr float kBlockEpsilon = 0.001f;

}  // namespace

int OrientationBin(float dx, float dy) {
  // atan2 is in (-180, 180]; adding 180 to negative angles folds opposite
  // gradient directions together, so the angle is in [0, 180].
  float degrees = std::atan2(dy, dx) * (180.0f / std::numbers::pi_v<float>);
  if (degrees < 0.0f) degrees += 180.0f;
  // Bin edges 15, 45, ..., 165: bin 0 covers [165, 180] and [0, 15).
  if (degrees < 15.0f) return 0;
  const int bin = static_cast<int>((degrees - 15.0f) / 30.0f) + 1;
  return bin >= kHogBins ? 0 : bin;
}

void ComputeGradients(const Image<float>& gray, GradientField& out) {
  const int width = gray.width();
  const int height = gray.height();
  out.magnitude.Resize(width, height, 1);
  out.bin.Resize(width, height, 1);
  auto at = [&](int x, int y) {
    return gray.at(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1));
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Sobel: [-1 0 1; -2 0 2; -1 0 1] and its transpose (y grows down).
      const float dx =
          (at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1)) -
          (at(x - 1, y - 1) + 2.0f * at(x - 1, y) + at(x - 1, y + 1));
      const float dy =
          (at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1)) -
          (at(x - 1, y - 1) + 2.0f * at(x, y - 1) + at(x + 1, y - 1));
      out.magnitude.at(x, y) = std::sqrt(dx * dx + dy * dy);
      out.bin.at(x, y) = static_cast<uint8_t>(OrientationBin(dx, dy));
    }
  }
}

void IntegralHistogram::Build(const GradientField& gradients) {
  width_ = gradients.magnitude.width();
  height_ = gradients.magnitude.height();
  const size_t stride = static_cast<size_t>(width_ + 1) * kHogBins;
  sums_.assign(stride * (height_ + 1), 0.0);
  // S(x + 1, y + 1) = S(x + 1, y) + (sum of row y up to and including x).
  for (int y = 0; y < height_; ++y) {
    std::array<double, kHogBins> row_sum{};
    const double* above = sums_.data() + y * stride;
    double* current = sums_.data() + (y + 1) * stride;
    for (int x = 0; x < width_; ++x) {
      row_sum[gradients.bin.at(x, y)] += gradients.magnitude.at(x, y);
      for (int b = 0; b < kHogBins; ++b) {
        current[(x + 1) * kHogBins + b] =
            above[(x + 1) * kHogBins + b] + row_sum[b];
      }
    }
  }
}

HogHistogram IntegralHistogram::Sum(int x, int y, int width, int height) const {
  const size_t stride = static_cast<size_t>(width_ + 1) * kHogBins;
  const double* top = sums_.data() + y * stride;
  const double* bottom = sums_.data() + (y + height) * stride;
  HogHistogram out{};
  for (int b = 0; b < kHogBins; ++b) {
    out[b] = static_cast<float>(
        bottom[(x + width) * kHogBins + b] - bottom[x * kHogBins + b] -
        top[(x + width) * kHogBins + b] + top[x * kHogBins + b]);
  }
  return out;
}

int DescriptorLength(int width, int height, const HogParams& params) {
  const int blocks_x = width / params.cell_size - params.block_size + 1;
  const int blocks_y = height / params.cell_size - params.block_size + 1;
  if (blocks_x <= 0 || blocks_y <= 0) return 0;
  return blocks_x * blocks_y * params.block_size * params.block_size * kHogBins;
}

void WindowDescriptor(const IntegralHistogram& integral, int x, int y,
                      int width, int height, const HogParams& params,
                      std::vector<float>& out) {
  const int cell = params.cell_size;
  const int block = params.block_size;
  const int cells_x = width / cell;
  const int cells_y = height / cell;
  out.clear();
  if (cells_x < block || cells_y < block) return;

  // Cell histograms, reused across calls on this thread.
  thread_local std::vector<HogHistogram> cells;
  cells.resize(static_cast<size_t>(cells_x) * cells_y);
  for (int cy = 0; cy < cells_y; ++cy) {
    for (int cx = 0; cx < cells_x; ++cx) {
      cells[cy * cells_x + cx] =
          integral.Sum(x + cx * cell, y + cy * cell, cell, cell);
    }
  }

  // Each block: its cells' histograms concatenated (row-major), divided by
  // the block's L2 norm so local contrast does not matter.
  out.reserve(DescriptorLength(width, height, params));
  for (int by = 0; by + block <= cells_y; ++by) {
    for (int bx = 0; bx + block <= cells_x; ++bx) {
      const size_t start = out.size();
      double sum_sq = 0.0;
      for (int i = 0; i < block; ++i) {
        for (int j = 0; j < block; ++j) {
          for (float v : cells[(by + i) * cells_x + (bx + j)]) {
            out.push_back(v);
            sum_sq += static_cast<double>(v) * v;
          }
        }
      }
      const auto inv_norm = static_cast<float>(
          1.0 / std::sqrt(sum_sq + kBlockEpsilon * kBlockEpsilon));
      for (size_t i = start; i < out.size(); ++i) out[i] *= inv_norm;
    }
  }
}

std::vector<float> ExtractHog(const Image<float>& gray,
                              const HogParams& params) {
  GradientField gradients;
  ComputeGradients(gray, gradients);
  IntegralHistogram integral;
  integral.Build(gradients);
  std::vector<float> descriptor;
  WindowDescriptor(integral, 0, 0, gray.width(), gray.height(), params,
                   descriptor);
  return descriptor;
}

}  // namespace minicam
