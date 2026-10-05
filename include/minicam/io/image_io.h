// Reading and writing ordinary image files (PNG, JPEG, ...).
//
// This is the only part of minicam that uses OpenCV, and it uses it only to
// decode and encode files. All processing is done by minicam's own code.

#ifndef MINICAM_IO_IMAGE_IO_H_
#define MINICAM_IO_IMAGE_IO_H_

#include <string>

#include "minicam/image.h"
#include "minicam/status.h"

namespace minicam {

// Loads an 8- or 16-bit sRGB-encoded image and returns linear RGB in [0, 1].
StatusOr<Image<float>> LoadImageAsLinearRgb(const std::string& path);

// Loads an 8- or 16-bit image and returns its encoded (not linearized) RGB
// values scaled to [0, 1].
StatusOr<Image<float>> LoadImageAsEncodedRgb(const std::string& path);

// Writes a 3-channel float image as an 8-bit RGB PNG. Values are clamped to
// [0, 1] and written as they are: no transfer curve is applied, so pass an
// already gamma-encoded image for correct display.
Status SaveRgbPng(const Image<float>& rgb, const std::string& path);

}  // namespace minicam

#endif  // MINICAM_IO_IMAGE_IO_H_
