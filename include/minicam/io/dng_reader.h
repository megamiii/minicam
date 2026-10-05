// Reads camera RAW files (DNG and other formats LibRaw supports).

#ifndef MINICAM_IO_DNG_READER_H_
#define MINICAM_IO_DNG_READER_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "minicam/frame.h"
#include "minicam/status.h"

class LibRaw;

namespace minicam {

// Decodes a Bayer RAW file into frame.raw and frame.meta using LibRaw:
//   raw            uint16 samples of the visible area
//   cfa            from LibRaw's color layout of the 2x2 tile
//   black_level    per tile position (black + per-color + pattern parts)
//   white_level    saturation value (DNG WhiteLevel)
//   wb_gains       the camera's as-shot gains, normalized so G = 1
//   color_matrix   camera RGB -> linear sRGB (LibRaw rgb_cam), unless a
//                  sidecar matrix is set with SetColorMatrixOverride()
//   exposure_time_s, analog_gain (ISO / 100)
//   orientation    from the Orientation tag (LibRaw flip)
// frame.meta.frame_id and timestamp_ns are left unchanged.
//
// Not thread-safe: use one DngReader per thread. The LibRaw instance is kept
// and recycled between files instead of being reallocated.
class DngReader {
 public:
  DngReader();
  ~DngReader();
  DngReader(const DngReader&) = delete;
  DngReader& operator=(const DngReader&) = delete;

  Status Read(const std::string& path, Frame& frame);

  // Uses this matrix instead of the file's own for every following Read().
  void SetColorMatrixOverride(std::optional<std::array<float, 9>> matrix) {
    matrix_override_ = matrix;
  }

 private:
  std::unique_ptr<LibRaw> libraw_;
  std::optional<std::array<float, 9>> matrix_override_;
};

// Parses an HDR+ "rgb2rgb.txt" sidecar: 9 numbers, row-major, the 3x3 matrix
// from white-balanced sensor RGB to linear sRGB.
StatusOr<std::array<float, 9>> ReadColorMatrixFile(const std::string& path);

}  // namespace minicam

#endif  // MINICAM_IO_DNG_READER_H_
