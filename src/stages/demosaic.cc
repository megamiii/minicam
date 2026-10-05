#include "minicam/stages/demosaic.h"

#include <algorithm>
#include <string>

#include "minicam/cfa.h"

namespace minicam {
namespace {

// Reflect-101 border handling: -1 -> 1, n -> n - 2. Mirroring by an even
// distance preserves the parity of the coordinate, so the mirrored pixel has
// the same CFA color as the missing one would have had. Valid for offsets up
// to `reach` pixels outside the image when n > reach.
inline int Reflect(int i, int n) {
  if (i < 0) return -i;
  if (i >= n) return 2 * n - 2 - i;
  return i;
}

// `reach` is how many pixels the filter reads beyond the center pixel.
Status ValidateMosaic(const Image<float>& bayer, int reach) {
  if (bayer.empty() || bayer.channels() != 1) {
    return InvalidArgumentError(
        "expected a 1-channel Bayer mosaic; run black_level first");
  }
  if (bayer.width() <= reach || bayer.height() <= reach) {
    return InvalidArgumentError("mosaic must be larger than " +
                                std::to_string(reach) + " pixels per side");
  }
  return OkStatus();
}

}  // namespace

Status BilinearDemosaicStage::Process(Frame& frame) {
  const Image<float>& in = frame.bayer;
  MINICAM_RETURN_IF_ERROR(ValidateMosaic(in, /*reach=*/1));

  const int width = in.width();
  const int height = in.height();
  const CfaPattern cfa = frame.meta.cfa;
  Image<float>& out = frame.rgb;
  out.Resize(width, height, 3);

  // Straightforward baseline: every neighbor access goes through Reflect().
  // This is the "before" version for the later optimization work.
  auto px = [&](int x, int y) {
    return in.at(Reflect(x, width), Reflect(y, height));
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int color = CfaColorAt(cfa, x, y);
      float rgb[3];
      rgb[color] = in.at(x, y);

      if (color == kGreen) {
        // A green pixel has red neighbors on one axis and blue on the other.
        const float horizontal = 0.5f * (px(x - 1, y) + px(x + 1, y));
        const float vertical = 0.5f * (px(x, y - 1) + px(x, y + 1));
        const int horizontal_color = CfaColorAt(cfa, x + 1, y);
        rgb[horizontal_color] = horizontal;
        rgb[2 - horizontal_color] = vertical;  // kRed <-> kBlue.
      } else {
        rgb[kGreen] = 0.25f * (px(x - 1, y) + px(x + 1, y) + px(x, y - 1) +
                               px(x, y + 1));
        rgb[2 - color] = 0.25f * (px(x - 1, y - 1) + px(x + 1, y - 1) +
                                  px(x - 1, y + 1) + px(x + 1, y + 1));
      }

      float* dst = &out.at(x, y);
      dst[0] = rgb[0];
      dst[1] = rgb[1];
      dst[2] = rgb[2];
    }
  }
  return OkStatus();
}

Status MalvarDemosaicStage::Process(Frame& frame) {
  const Image<float>& in = frame.bayer;
  MINICAM_RETURN_IF_ERROR(ValidateMosaic(in, /*reach=*/2));

  const int width = in.width();
  const int height = in.height();
  const CfaPattern cfa = frame.meta.cfa;
  Image<float>& out = frame.rgb;
  out.Resize(width, height, 3);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Sample at offset (dx, dy) from the current pixel.
      auto p = [&](int dx, int dy) {
        return in.at(Reflect(x + dx, width), Reflect(y + dy, height));
      };
      const int color = CfaColorAt(cfa, x, y);
      const float center = p(0, 0);
      // Neighbor sums shared by the kernels. At distance 1 on the axes and on
      // the diagonals sit the other colors; at distance 2 on the axes sits the
      // same color as the center (used for the Laplacian correction).
      const float h1 = p(-1, 0) + p(1, 0);
      const float v1 = p(0, -1) + p(0, 1);
      const float h2 = p(-2, 0) + p(2, 0);
      const float v2 = p(0, -2) + p(0, 2);
      const float diag = p(-1, -1) + p(1, -1) + p(-1, 1) + p(1, 1);

      float rgb[3];
      rgb[color] = center;
      if (color == kGreen) {
        // Color sampled left/right of this pixel, and the one above/below.
        const float horizontal =
            (5.0f * center + 4.0f * h1 - diag - h2 + 0.5f * v2) / 8.0f;
        const float vertical =
            (5.0f * center + 4.0f * v1 - diag - v2 + 0.5f * h2) / 8.0f;
        const int horizontal_color = CfaColorAt(cfa, x + 1, y);
        rgb[horizontal_color] = horizontal;
        rgb[2 - horizontal_color] = vertical;  // kRed <-> kBlue.
      } else {
        rgb[kGreen] = (4.0f * center + 2.0f * (h1 + v1) - (h2 + v2)) / 8.0f;
        rgb[2 - color] =
            (6.0f * center + 2.0f * diag - 1.5f * (h2 + v2)) / 8.0f;
      }

      // The correction terms can overshoot at sharp edges.
      float* dst = &out.at(x, y);
      for (int c = 0; c < 3; ++c) dst[c] = std::clamp(rgb[c], 0.0f, 1.0f);
    }
  }
  return OkStatus();
}

}  // namespace minicam
