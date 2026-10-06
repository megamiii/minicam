#include "test_scenes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace minicam::testing {

Image<float> MakeTestScene(int width, int height) {
  Image<float> image(width, height, 3);
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const float cx = w / 3.0f;
  const float cy = h / 2.0f;
  const float radius = std::min(w, h) / 5.0f;
  const float two_pi = 2.0f * std::numbers::pi_v<float>;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float u = static_cast<float>(x) / w;
      const float v = static_cast<float>(y) / h;
      std::array<float, 3> rgb = {
          0.15f + 0.6f * u,
          0.15f + 0.6f * v,
          0.45f + 0.3f * std::sin(two_pi * (u + v)),
      };

      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      if (dx * dx + dy * dy < radius * radius) rgb = {0.8f, 0.3f, 0.1f};

      // Stripes in the right third: vertical in the upper half, horizontal in
      // the lower half, period 6 pixels.
      if (x >= 2 * width / 3 && y >= height / 8 && y < 7 * height / 8) {
        const int phase = y < height / 2 ? x : y;
        const float level = (phase / 3) % 2 == 0 ? 0.85f : 0.1f;
        rgb = {level, level, level};
      }

      for (int c = 0; c < 3; ++c) {
        image.at(x, y, c) = std::clamp(rgb[c], 0.02f, 0.95f);
      }
    }
  }
  return image;
}

Image<float> MakeFlatImage(int width, int height, std::array<float, 3> rgb) {
  Image<float> image(width, height, 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c) image.at(x, y, c) = rgb[c];
    }
  }
  return image;
}

}  // namespace minicam::testing
