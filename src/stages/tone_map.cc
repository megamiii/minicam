#include "minicam/stages/tone_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "minicam/color.h"

namespace minicam {
namespace {

// Avoids log(0) for black pixels in the log-average.
constexpr double kLogDelta = 1e-4;

inline float Luminance(const float* p) {
  return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

}  // namespace

Status ToneMapStage::Process(Frame& frame) {
  Image<float>& rgb = frame.rgb;
  if (rgb.empty() || rgb.channels() != 3) {
    return InvalidArgumentError(
        "expected a 3-channel RGB image; run demosaicing first");
  }
  if (options_.key <= 0.0f || options_.max_auto_gain <= 0.0f ||
      options_.white_percentile <= 0.0f || options_.white_percentile > 1.0f) {
    return InvalidArgumentError("invalid tone mapping options");
  }

  const int width = rgb.width();
  const int height = rgb.height();
  const size_t num_pixels = static_cast<size_t>(width) * height;

  // Exposure: auto (log-average luminance -> key) times manual EV.
  float exposure = std::exp2(options_.exposure_ev);
  if (options_.auto_exposure) {
    double log_sum = 0.0;
    for (int y = 0; y < height; ++y) {
      const float* p = rgb.row(y);
      for (int x = 0; x < width; ++x, p += 3) {
        log_sum += std::log(kLogDelta + std::max(Luminance(p), 0.0f));
      }
    }
    const double log_average =
        std::exp(log_sum / static_cast<double>(num_pixels));
    const auto gain = static_cast<float>(options_.key / log_average);
    exposure *= std::min(gain, options_.max_auto_gain);
  }

  if (options_.op == ToneMapOptions::Operator::kClip) {
    for (float& v : rgb.pixels()) {
      v = LinearToSrgb(std::clamp(v * exposure, 0.0f, 1.0f));
    }
    return OkStatus();
  }

  // White point: a high percentile of exposed luminance, but at least 1 so a
  // low-contrast scene is not stretched to white.
  luminance_.resize(num_pixels);
  for (size_t i = 0; i < num_pixels; ++i) {
    luminance_[i] = exposure * Luminance(&rgb.pixels()[i * 3]);
  }
  const auto rank = static_cast<size_t>(options_.white_percentile *
                                        static_cast<float>(num_pixels - 1));
  std::nth_element(luminance_.begin(), luminance_.begin() + rank,
                   luminance_.end());
  const float white = std::max(luminance_[rank], 1.0f);
  const float inv_white_sq = 1.0f / (white * white);

  for (int y = 0; y < height; ++y) {
    float* p = rgb.row(y);
    for (int x = 0; x < width; ++x, p += 3) {
      const float l = Luminance(p);
      float scale = 0.0f;
      if (l > 0.0f) {
        const float ls = exposure * l;
        const float ld = ls * (1.0f + ls * inv_white_sq) / (1.0f + ls);
        scale = ld / l;
      }
      for (int c = 0; c < 3; ++c) {
        p[c] = LinearToSrgb(std::clamp(p[c] * scale, 0.0f, 1.0f));
      }
    }
  }
  return OkStatus();
}

}  // namespace minicam
