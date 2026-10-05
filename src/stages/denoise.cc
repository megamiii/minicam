#include "minicam/stages/denoise.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "minicam/parallel.h"

namespace minicam {
namespace {

// Entries in the range-weight table.
constexpr int kRangeLutSize = 4096;
// Colors farther apart than this get weight 0 (exp(-x) < 1e-4 for x > 9.2).
constexpr float kRangeCutoffExponent = 9.21f;

}  // namespace

BilateralDenoiseStage::BilateralDenoiseStage(BilateralOptions options)
    : options_(options) {
  // Invalid sigmas are rejected in Process().
  if (options_.sigma_spatial <= 0.0f || options_.sigma_range <= 0.0f) return;

  radius_ = static_cast<int>(std::ceil(2.0f * options_.sigma_spatial));
  const int size = 2 * radius_ + 1;
  const float inv_two_sigma_s_sq =
      1.0f / (2.0f * options_.sigma_spatial * options_.sigma_spatial);
  spatial_weights_.resize(static_cast<size_t>(size) * size);
  for (int dy = -radius_; dy <= radius_; ++dy) {
    for (int dx = -radius_; dx <= radius_; ++dx) {
      spatial_weights_[(dy + radius_) * size + (dx + radius_)] =
          std::exp(-static_cast<float>(dx * dx + dy * dy) * inv_two_sigma_s_sq);
    }
  }

  // The table covers squared distances up to the cutoff; the weight's slope
  // is at most 1 / (2 sigma_r^2), so nearest-entry lookup is accurate to
  // about 1e-3.
  const float two_sigma_r_sq =
      2.0f * options_.sigma_range * options_.sigma_range;
  const float max_d2 = kRangeCutoffExponent * two_sigma_r_sq;
  range_scale_ = static_cast<float>(kRangeLutSize - 1) / max_d2;
  range_lut_.resize(kRangeLutSize + 1);
  for (int i = 0; i < kRangeLutSize; ++i) {
    range_lut_[i] =
        std::exp(-(static_cast<float>(i) / range_scale_) / two_sigma_r_sq);
  }
  range_lut_[kRangeLutSize] = 0.0f;
}

Status BilateralDenoiseStage::Process(Frame& frame) {
  if (options_.sigma_spatial <= 0.0f || options_.sigma_range <= 0.0f) {
    return InvalidArgumentError("bilateral sigmas must be positive");
  }
  const Image<float>& in = frame.rgb;
  if (in.empty() || in.channels() != 3) {
    return InvalidArgumentError(
        "expected a 3-channel RGB image; run demosaicing first");
  }
  scratch_.Resize(in.width(), in.height(), 3);

  // Every output pixel depends only on the input image, and each band writes
  // only its own rows, so bands need no synchronization.
  const bool use_lut =
      options_.implementation == BilateralOptions::Implementation::kLookupTable;
  ParallelFor(in.height(), ResolveThreadCount(options_.num_threads),
              [&](int y_begin, int y_end) {
                if (use_lut) {
                  FilterRowsLookupTable(in, y_begin, y_end);
                } else {
                  FilterRowsReference(in, y_begin, y_end);
                }
              });

  std::swap(frame.rgb, scratch_);
  return OkStatus();
}

void BilateralDenoiseStage::FilterRowsReference(const Image<float>& in,
                                                int y_begin, int y_end) {
  const int width = in.width();
  const int height = in.height();
  const int size = 2 * radius_ + 1;
  const float inv_two_sigma_r_sq =
      1.0f / (2.0f * options_.sigma_range * options_.sigma_range);

  for (int y = y_begin; y < y_end; ++y) {
    const int y0 = std::max(y - radius_, 0);
    const int y1 = std::min(y + radius_, height - 1);
    for (int x = 0; x < width; ++x) {
      const int x0 = std::max(x - radius_, 0);
      const int x1 = std::min(x + radius_, width - 1);
      const float* center = &in.at(x, y);

      float sum[3] = {0.0f, 0.0f, 0.0f};
      float weight_sum = 0.0f;
      for (int qy = y0; qy <= y1; ++qy) {
        // Points at the window's center column, indexed by qx - x.
        const float* row_weights =
            spatial_weights_.data() + (qy - y + radius_) * size + radius_;
        for (int qx = x0; qx <= x1; ++qx) {
          const float* q = &in.at(qx, qy);
          const float d0 = q[0] - center[0];
          const float d1 = q[1] - center[1];
          const float d2 = q[2] - center[2];
          const float range_weight =
              std::exp(-(d0 * d0 + d1 * d1 + d2 * d2) * inv_two_sigma_r_sq);
          const float w = row_weights[qx - x] * range_weight;
          sum[0] += w * q[0];
          sum[1] += w * q[1];
          sum[2] += w * q[2];
          weight_sum += w;
        }
      }
      // The center pixel always contributes weight 1, so weight_sum >= 1.
      float* out = &scratch_.at(x, y);
      const float inv = 1.0f / weight_sum;
      out[0] = sum[0] * inv;
      out[1] = sum[1] * inv;
      out[2] = sum[2] * inv;
    }
  }
}

void BilateralDenoiseStage::FilterRowsLookupTable(const Image<float>& in,
                                                  int y_begin, int y_end) {
  const int width = in.width();
  const int height = in.height();
  const int size = 2 * radius_ + 1;
  const float* lut = range_lut_.data();
  const float scale = range_scale_;

  for (int y = y_begin; y < y_end; ++y) {
    const int y0 = std::max(y - radius_, 0);
    const int y1 = std::min(y + radius_, height - 1);
    for (int x = 0; x < width; ++x) {
      const int x0 = std::max(x - radius_, 0);
      const int x1 = std::min(x + radius_, width - 1);
      const float* center = &in.at(x, y);
      const float c0 = center[0];
      const float c1 = center[1];
      const float c2 = center[2];

      float sum0 = 0.0f;
      float sum1 = 0.0f;
      float sum2 = 0.0f;
      float weight_sum = 0.0f;
      for (int qy = y0; qy <= y1; ++qy) {
        const float* row_weights =
            spatial_weights_.data() + (qy - y + radius_) * size + radius_;
        const float* q = &in.at(x0, qy);
        for (int qx = x0; qx <= x1; ++qx, q += 3) {
          const float d0 = q[0] - c0;
          const float d1 = q[1] - c1;
          const float d2 = q[2] - c2;
          // Nearest table entry; every distance past the cutoff lands on the
          // final zero entry, so no branch is needed.
          const int index = std::min(
              static_cast<int>((d0 * d0 + d1 * d1 + d2 * d2) * scale + 0.5f),
              kRangeLutSize);
          const float w = row_weights[qx - x] * lut[index];
          sum0 += w * q[0];
          sum1 += w * q[1];
          sum2 += w * q[2];
          weight_sum += w;
        }
      }
      float* out = &scratch_.at(x, y);
      const float inv = 1.0f / weight_sum;
      out[0] = sum0 * inv;
      out[1] = sum1 * inv;
      out[2] = sum2 * inv;
    }
  }
}

}  // namespace minicam
