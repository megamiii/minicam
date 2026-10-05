#include "minicam/stages/demosaic.h"

#include "minicam/cfa.h"

namespace minicam {
namespace {

// Reflect-101 border handling: -1 -> 1, n -> n - 2. Mirroring by an even
// distance preserves the parity of the coordinate, so the mirrored pixel has
// the same CFA color as the missing one would have had. Requires n >= 2.
inline int Reflect(int i, int n) {
  if (i < 0) return -i;
  if (i >= n) return 2 * n - 2 - i;
  return i;
}

Status ValidateMosaic(const Image<float>& bayer) {
  if (bayer.empty() || bayer.channels() != 1) {
    return InvalidArgumentError(
        "expected a 1-channel Bayer mosaic; run black_level first");
  }
  if (bayer.width() < 2 || bayer.height() < 2) {
    return InvalidArgumentError("mosaic must be at least 2x2");
  }
  return OkStatus();
}

}  // namespace

Status BilinearDemosaicStage::Process(Frame& frame) {
  const Image<float>& in = frame.bayer;
  MINICAM_RETURN_IF_ERROR(ValidateMosaic(in));

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

}  // namespace minicam
