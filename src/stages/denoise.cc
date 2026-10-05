#include "minicam/stages/denoise.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace minicam {

BilateralDenoiseStage::BilateralDenoiseStage(BilateralOptions options)
    : options_(options) {
  if (options_.sigma_spatial <= 0.0f) return;  // Rejected in Process().
  radius_ = static_cast<int>(std::ceil(2.0f * options_.sigma_spatial));
  const int size = 2 * radius_ + 1;
  const float inv_two_sigma_sq =
      1.0f / (2.0f * options_.sigma_spatial * options_.sigma_spatial);
  spatial_weights_.resize(static_cast<size_t>(size) * size);
  for (int dy = -radius_; dy <= radius_; ++dy) {
    for (int dx = -radius_; dx <= radius_; ++dx) {
      spatial_weights_[(dy + radius_) * size + (dx + radius_)] =
          std::exp(-static_cast<float>(dx * dx + dy * dy) * inv_two_sigma_sq);
    }
  }
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

  const int width = in.width();
  const int height = in.height();
  const int size = 2 * radius_ + 1;
  const float inv_two_sigma_r_sq =
      1.0f / (2.0f * options_.sigma_range * options_.sigma_range);
  scratch_.Resize(width, height, 3);

  for (int y = 0; y < height; ++y) {
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

  std::swap(frame.rgb, scratch_);
  return OkStatus();
}

}  // namespace minicam
