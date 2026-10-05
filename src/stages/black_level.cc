#include "minicam/stages/black_level.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace minicam {

Status BlackLevelStage::Process(Frame& frame) {
  const Image<uint16_t>& raw = frame.raw;
  if (raw.empty() || raw.channels() != 1) {
    return InvalidArgumentError("expected a non-empty 1-channel raw mosaic");
  }

  const FrameMetadata& meta = frame.meta;
  std::array<float, 4> black{};
  std::array<float, 4> scale{};
  for (int i = 0; i < 4; ++i) {
    if (meta.white_level <= meta.black_level[i]) {
      return InvalidArgumentError(
          "white level " + std::to_string(meta.white_level) +
          " must be above black level " + std::to_string(meta.black_level[i]));
    }
    black[i] = static_cast<float>(meta.black_level[i]);
    // Multiplying by a precomputed reciprocal is cheaper than dividing.
    scale[i] = 1.0f / static_cast<float>(meta.white_level - meta.black_level[i]);
  }

  const int width = raw.width();
  const int height = raw.height();
  frame.bayer.Resize(width, height, 1);

  for (int y = 0; y < height; ++y) {
    const uint16_t* in = raw.row(y);
    float* out = frame.bayer.row(y);
    const int tile_row = (y & 1) << 1;
    for (int x = 0; x < width; ++x) {
      const int tile = tile_row | (x & 1);
      const float v = (static_cast<float>(in[x]) - black[tile]) * scale[tile];
      out[x] = std::clamp(v, 0.0f, 1.0f);
    }
  }
  return OkStatus();
}

}  // namespace minicam
