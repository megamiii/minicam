// Cuts a face template for HogFaceDetectionStage out of an image.
//
//   make_face_template <image> <out.png> <x> <y> <width> <height> <factor>
//
// (x, y, width, height) is the face box in the image's pixels. The crop is
// converted to gray and averaged over factor x factor blocks with the same
// code the detector uses (DownscaledGray), so the template lives in exactly
// the domain the detector scans. Cut it from the pipeline's *output* image
// (sRGB-encoded, after tone mapping), not from a camera JPEG.

#include <array>
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "minicam/image.h"
#include "minicam/io/image_io.h"
#include "minicam/stages/face_detect.h"

namespace {

int Fail(std::string_view message) {
  std::cerr << "error: " << message
            << "\nusage: make_face_template <image> <out.png> <x> <y> "
               "<width> <height> <factor>\n";
  return 1;
}

bool ParseInt(std::string_view text, int& value) {
  const char* end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(text.data(), end, value);
  return ec == std::errc() && ptr == end;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 8) return Fail("wrong number of arguments");
  std::array<int, 5> v{};  // x, y, width, height, factor
  for (size_t i = 0; i < v.size(); ++i) {
    if (!ParseInt(argv[3 + i], v[i])) return Fail("bad number");
  }
  const auto [x0, y0, width, height, factor] = v;

  auto image = minicam::LoadImageAsEncodedRgb(argv[1]);
  if (!image.ok()) return Fail(image.status().ToString());
  if (x0 < 0 || y0 < 0 || width < factor || height < factor || factor < 1 ||
      x0 + width > image->width() || y0 + height > image->height()) {
    return Fail("box outside the image");
  }

  minicam::Image<float> crop(width, height, 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c)
        crop.at(x, y, c) = image->at(x0 + x, y0 + y, c);
    }
  }
  minicam::Image<float> gray;
  minicam::DownscaledGray(crop, factor, gray);

  minicam::Image<float> rgb(gray.width(), gray.height(), 3);
  for (int y = 0; y < gray.height(); ++y) {
    for (int x = 0; x < gray.width(); ++x) {
      for (int c = 0; c < 3; ++c) rgb.at(x, y, c) = gray.at(x, y);
    }
  }
  const minicam::Status status = minicam::SaveRgbPng(rgb, argv[2]);
  if (!status.ok()) return Fail(status.ToString());
  std::cout << "wrote " << argv[2] << ": " << gray.width() << "x"
            << gray.height() << " template (factor " << factor << ")\n";
  return 0;
}
