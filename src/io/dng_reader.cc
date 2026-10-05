#include "minicam/io/dng_reader.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <libraw/libraw.h>

namespace minicam {
namespace {

Status LibRawError(const std::string& what, const std::string& path, int code) {
  return DataLossError(what + " '" + path + "': " + libraw_strerror(code));
}

// LibRaw color indices: 0 = R, 1 = G, 2 = B, 3 = second G.
std::optional<CfaPattern> PatternFromLibRaw(LibRaw& raw) {
  std::array<int, 4> tile{};
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int c = raw.COLOR(y, x);
      tile[CfaTileIndex(x, y)] = c == 3 ? kGreen : c;
    }
  }
  for (CfaPattern p : {CfaPattern::kRggb, CfaPattern::kBggr, CfaPattern::kGrbg,
                       CfaPattern::kGbrg}) {
    bool match = true;
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        match = match && CfaColorAt(p, x, y) == tile[CfaTileIndex(x, y)];
      }
    }
    if (match) return p;
  }
  return std::nullopt;
}

}  // namespace

DngReader::DngReader() : libraw_(std::make_unique<LibRaw>(0)) {}
DngReader::~DngReader() = default;

Status DngReader::Read(const std::string& path, Frame& frame) {
  LibRaw& raw = *libraw_;
  raw.recycle();
  if (!std::filesystem::exists(path)) {
    return NotFoundError("cannot open '" + path + "'");
  }
  if (int rc = raw.open_file(path.c_str()); rc != LIBRAW_SUCCESS) {
    return LibRawError("cannot parse", path, rc);
  }
  if (int rc = raw.unpack(); rc != LIBRAW_SUCCESS) {
    return LibRawError("cannot decode", path, rc);
  }

  const libraw_rawdata_t& data = raw.imgdata.rawdata;
  const libraw_image_sizes_t& sizes = raw.imgdata.sizes;
  const libraw_colordata_t& color = raw.imgdata.color;
  // filters == 0: not a mosaic (e.g. linear DNG); 9: Fujifilm X-Trans.
  if (data.raw_image == nullptr || raw.imgdata.idata.filters == 0 ||
      raw.imgdata.idata.filters == 9) {
    return InvalidArgumentError("'" + path + "' is not a Bayer RAW image");
  }
  const std::optional<CfaPattern> cfa = PatternFromLibRaw(raw);
  if (!cfa) {
    return InvalidArgumentError("unsupported CFA layout in '" + path + "'");
  }

  // Copy the visible area out of the full sensor buffer. Both margins are
  // even in practice; the CFA layout above is relative to the visible area.
  const int width = sizes.width;
  const int height = sizes.height;
  const size_t pitch = sizes.raw_pitch / sizeof(uint16_t);
  frame.raw.Resize(width, height, 1);
  for (int y = 0; y < height; ++y) {
    const uint16_t* src =
        data.raw_image + (y + sizes.top_margin) * pitch + sizes.left_margin;
    std::memcpy(frame.raw.row(y), src, width * sizeof(uint16_t));
  }

  FrameMetadata& m = frame.meta;
  m.cfa = *cfa;
  // LibRaw splits the black level into a common part, a per-color part and
  // an optional repeating pattern (cblack[4] x cblack[5] values at cblack[6]).
  const unsigned pattern_rows = color.cblack[4];
  const unsigned pattern_cols = color.cblack[5];
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      unsigned black = color.black + color.cblack[raw.COLOR(y, x)];
      if (pattern_rows > 0 && pattern_cols > 0) {
        black += color.cblack[6 + (y % pattern_rows) * pattern_cols +
                              (x % pattern_cols)];
      }
      m.black_level[CfaTileIndex(x, y)] = static_cast<uint16_t>(black);
    }
  }
  m.white_level = static_cast<uint16_t>(color.maximum);

  const float green = color.cam_mul[1];
  if (green > 0.0f && color.cam_mul[0] > 0.0f && color.cam_mul[2] > 0.0f) {
    m.wb_gains = {color.cam_mul[0] / green, 1.0f, color.cam_mul[2] / green};
  } else {
    m.wb_gains = {1.0f, 1.0f, 1.0f};
  }

  if (matrix_override_) {
    m.color_matrix = *matrix_override_;
  } else {
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c)
        m.color_matrix[r * 3 + c] = color.rgb_cam[r][c];
    }
  }

  m.exposure_time_s = raw.imgdata.other.shutter;
  m.analog_gain = raw.imgdata.other.iso_speed > 0.0f
                      ? raw.imgdata.other.iso_speed / 100.0f
                      : 1.0f;
  raw.recycle();  // Free the decoded data now rather than at the next Read.
  return OkStatus();
}

StatusOr<std::array<float, 9>> ReadColorMatrixFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) return NotFoundError("cannot open '" + path + "'");
  std::array<float, 9> matrix{};
  for (float& v : matrix) {
    if (!(in >> v) || !std::isfinite(v)) {
      return DataLossError("expected 9 numbers in '" + path + "'");
    }
  }
  return matrix;
}

}  // namespace minicam
