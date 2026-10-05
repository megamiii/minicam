#include "minicam/io/synthetic.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace minicam {

Status MosaicLinearRgb(const Image<float>& linear_rgb,
                       const SyntheticRawOptions& options, Frame& frame) {
  const int width = linear_rgb.width();
  const int height = linear_rgb.height();
  if (linear_rgb.channels() != 3) {
    return InvalidArgumentError("expected a 3-channel RGB image");
  }
  if (width < 2 || height < 2 || width % 2 != 0 || height % 2 != 0) {
    return InvalidArgumentError("width and height must be even and >= 2");
  }
  if (options.white_level <= options.black_level) {
    return InvalidArgumentError("white level must be above black level");
  }
  if (options.shot_noise < 0.0f || options.read_noise < 0.0f) {
    return InvalidArgumentError("noise parameters must be non-negative");
  }

  frame.meta = FrameMetadata{};
  frame.meta.cfa = options.cfa;
  frame.meta.black_level.fill(options.black_level);
  frame.meta.white_level = options.white_level;
  frame.raw.Resize(width, height, 1);

  const bool add_noise = options.shot_noise > 0.0f || options.read_noise > 0.0f;
  const float read_variance = options.read_noise * options.read_noise;
  const float range =
      static_cast<float>(options.white_level - options.black_level);
  const float black = options.black_level;
  const float white = options.white_level;
  std::mt19937 rng(options.seed);
  std::normal_distribution<float> normal(0.0f, 1.0f);

  for (int y = 0; y < height; ++y) {
    uint16_t* out = frame.raw.row(y);
    for (int x = 0; x < width; ++x) {
      const int c = CfaColorAt(options.cfa, x, y);
      float signal = std::clamp(linear_rgb.at(x, y, c), 0.0f, 1.0f);
      if (add_noise) {
        // Gaussian approximation of the Poisson-Gaussian model.
        const float sigma =
            std::sqrt(options.shot_noise * signal + read_variance);
        signal += sigma * normal(rng);
      }
      // Real sensors can report values below the black level when noise is
      // present, so only clamp to the representable range [0, white].
      const float value = std::round(signal * range + black);
      out[x] = static_cast<uint16_t>(std::clamp(value, 0.0f, white));
    }
  }
  return OkStatus();
}

}  // namespace minicam
