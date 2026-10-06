#include "minicam/stages/tone_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "minicam/color.h"
#include "minicam/parallel.h"

namespace minicam {
namespace {

// Avoids log(0) for black pixels in the log-average.
constexpr double kLogDelta = 1e-4;

inline float Luminance(const float* p) {
  return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

// Reinhard curve factor for a pixel of luminance `l`: the same scale is
// applied to R, G and B so hue and saturation are kept.
inline float ReinhardScale(float l, float exposure, float inv_white_sq) {
  if (l <= 0.0f) return 0.0f;
  const float ls = exposure * l;
  const float ld = ls * (1.0f + ls * inv_white_sq) / (1.0f + ls);
  return ld / l;
}

float AutoExposureGain(double log_sum, size_t num_pixels,
                       const ToneMapOptions& options) {
  const double log_average =
      std::exp(log_sum / static_cast<double>(num_pixels));
  const auto gain = static_cast<float>(options.key / log_average);
  return std::min(gain, options.max_auto_gain);
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
  return options_.implementation == ToneMapOptions::Implementation::kReference
             ? ProcessReference(rgb)
             : ProcessFast(rgb);
}

Status ToneMapStage::ProcessReference(Image<float>& rgb) {
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
    exposure *= AutoExposureGain(log_sum, num_pixels, options_);
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
  std::nth_element(luminance_.begin(),
                   luminance_.begin() + static_cast<std::ptrdiff_t>(rank),
                   luminance_.end());
  const float white = std::max(luminance_[rank], 1.0f);
  const float inv_white_sq = 1.0f / (white * white);

  for (int y = 0; y < height; ++y) {
    float* p = rgb.row(y);
    for (int x = 0; x < width; ++x, p += 3) {
      const float scale = ReinhardScale(Luminance(p), exposure, inv_white_sq);
      for (int c = 0; c < 3; ++c) {
        p[c] = LinearToSrgb(std::clamp(p[c] * scale, 0.0f, 1.0f));
      }
    }
  }
  return OkStatus();
}

Status ToneMapStage::ProcessFast(Image<float>& rgb) {
  const int width = rgb.width();
  const int height = rgb.height();
  const size_t num_pixels = static_cast<size_t>(width) * height;
  const int threads = ResolveThreadCount(options_.num_threads);
  const SrgbEncodeTable& srgb = SrgbEncodeTable::Get();

  float exposure = std::exp2(options_.exposure_ev);
  if (options_.auto_exposure) {
    // float log per pixel (a double constant would silently promote every
    // log to double), summed in double per row, rows added in order.
    row_log_sums_.resize(height);
    ParallelFor(height, threads, [&](int y_begin, int y_end) {
      constexpr auto kDelta = static_cast<float>(kLogDelta);
      for (int y = y_begin; y < y_end; ++y) {
        const float* p = rgb.row(y);
        double sum = 0.0;
        for (int x = 0; x < width; ++x, p += 3) {
          sum += std::log(kDelta + std::max(Luminance(p), 0.0f));
        }
        row_log_sums_[y] = sum;
      }
    });
    double log_sum = 0.0;
    for (double s : row_log_sums_) log_sum += s;
    exposure *= AutoExposureGain(log_sum, num_pixels, options_);
  }

  if (options_.op == ToneMapOptions::Operator::kClip) {
    ParallelFor(height, threads, [&](int y_begin, int y_end) {
      for (int y = y_begin; y < y_end; ++y) {
        float* p = rgb.row(y);
        for (size_t i = 0; i < rgb.stride(); ++i) {
          p[i] = srgb.Encode(std::clamp(p[i] * exposure, 0.0f, 1.0f));
        }
      }
    });
    return OkStatus();
  }

  luminance_.resize(num_pixels);
  ParallelFor(height, threads, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
      const float* p = rgb.row(y);
      float* l = luminance_.data() + static_cast<size_t>(y) * width;
      for (int x = 0; x < width; ++x, p += 3) l[x] = exposure * Luminance(p);
    }
  });
  const auto rank = static_cast<size_t>(options_.white_percentile *
                                        static_cast<float>(num_pixels - 1));
  std::nth_element(luminance_.begin(),
                   luminance_.begin() + static_cast<std::ptrdiff_t>(rank),
                   luminance_.end());
  const float white = std::max(luminance_[rank], 1.0f);
  const float inv_white_sq = 1.0f / (white * white);

  ParallelFor(height, threads, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
      float* p = rgb.row(y);
      for (int x = 0; x < width; ++x, p += 3) {
        const float scale = ReinhardScale(Luminance(p), exposure, inv_white_sq);
        for (int c = 0; c < 3; ++c) {
          p[c] = srgb.Encode(std::clamp(p[c] * scale, 0.0f, 1.0f));
        }
      }
    }
  });
  return OkStatus();
}

}  // namespace minicam
