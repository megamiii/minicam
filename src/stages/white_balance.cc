#include "minicam/stages/white_balance.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace minicam {
namespace {

constexpr float kClippedLevel = 0.98f;
constexpr float kDarkLevel = 0.002f;
// Below this share of usable pixels, fall back to using every pixel.
constexpr double kMinUsableFraction = 0.01;
// Limits that keep a degenerate image (e.g. no blue at all) from producing
// absurd gains.
constexpr float kMinGain = 1.0f / 16.0f;
constexpr float kMaxGain = 16.0f;

}  // namespace

std::array<float, 3> EstimateGrayWorldGains(const Image<float>& rgb) {
  std::array<double, 3> usable_sum = {0.0, 0.0, 0.0};
  std::array<double, 3> all_sum = {0.0, 0.0, 0.0};
  size_t usable_count = 0;

  for (int y = 0; y < rgb.height(); ++y) {
    const float* p = rgb.row(y);
    for (int x = 0; x < rgb.width(); ++x, p += 3) {
      const float max_c = std::max({p[0], p[1], p[2]});
      for (int c = 0; c < 3; ++c) all_sum[c] += p[c];
      if (max_c < kClippedLevel && max_c > kDarkLevel) {
        for (int c = 0; c < 3; ++c) usable_sum[c] += p[c];
        ++usable_count;
      }
    }
  }

  const size_t total = static_cast<size_t>(rgb.width()) * rgb.height();
  const bool use_all =
      static_cast<double>(usable_count) < kMinUsableFraction * total;
  const std::array<double, 3>& sum = use_all ? all_sum : usable_sum;

  std::array<float, 3> gains = {1.0f, 1.0f, 1.0f};
  for (int c : {0, 2}) {
    if (sum[c] > 0.0) {
      gains[c] = static_cast<float>(sum[1] / sum[c]);
    }
    gains[c] = std::clamp(gains[c], kMinGain, kMaxGain);
  }
  return gains;
}

Status WhiteBalanceStage::Process(Frame& frame) {
  Image<float>& rgb = frame.rgb;
  if (rgb.empty() || rgb.channels() != 3) {
    return InvalidArgumentError(
        "expected a 3-channel RGB image; run demosaicing first");
  }

  std::array<float, 3> gains = frame.meta.wb_gains;
  if (mode_ == Mode::kGrayWorld) {
    gains = EstimateGrayWorldGains(rgb);
  } else if (std::min({gains[0], gains[1], gains[2]}) <= 0.0f) {
    return InvalidArgumentError("white balance gains must be positive");
  }

  // Rescale so the smallest gain is 1: every channel is then multiplied by
  // at least 1, so a pixel clipped in all channels stays (1, 1, 1).
  const float min_gain = std::min({gains[0], gains[1], gains[2]});
  for (float& g : gains) g /= min_gain;
  frame.meta.wb_gains = gains;

  for (int y = 0; y < rgb.height(); ++y) {
    float* p = rgb.row(y);
    for (int x = 0; x < rgb.width(); ++x, p += 3) {
      for (int c = 0; c < 3; ++c) p[c] = std::min(p[c] * gains[c], 1.0f);
    }
  }
  return OkStatus();
}

}  // namespace minicam
