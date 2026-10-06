#include "minicam/io/image_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "minicam/color.h"

namespace minicam {

namespace {

StatusOr<Image<float>> LoadRgb(const std::string& path, bool linearize) {
  const cv::Mat bgr = cv::imread(path, cv::IMREAD_COLOR | cv::IMREAD_ANYDEPTH);
  if (bgr.empty()) return NotFoundError("cannot read image '" + path + "'");

  float max_value = 0.0f;
  if (bgr.depth() == CV_8U) {
    max_value = 255.0f;
  } else if (bgr.depth() == CV_16U) {
    max_value = 65535.0f;
  } else {
    return InvalidArgumentError("unsupported bit depth in '" + path + "'");
  }

  Image<float> rgb(bgr.cols, bgr.rows, 3);
  for (int y = 0; y < bgr.rows; ++y) {
    float* out = rgb.row(y);
    for (int x = 0; x < bgr.cols; ++x) {
      for (int c = 0; c < 3; ++c) {
        // OpenCV stores pixels as B, G, R.
        const int src_c = 2 - c;
        const float v =
            bgr.depth() == CV_8U
                ? static_cast<float>(bgr.ptr<uint8_t>(y)[x * 3 + src_c])
                : static_cast<float>(bgr.ptr<uint16_t>(y)[x * 3 + src_c]);
        out[x * 3 + c] =
            linearize ? SrgbToLinear(v / max_value) : v / max_value;
      }
    }
  }
  return rgb;
}

}  // namespace

StatusOr<Image<float>> LoadImageAsLinearRgb(const std::string& path) {
  return LoadRgb(path, /*linearize=*/true);
}

StatusOr<Image<float>> LoadImageAsEncodedRgb(const std::string& path) {
  return LoadRgb(path, /*linearize=*/false);
}

Status SaveRgbPng(const Image<float>& rgb, const std::string& path) {
  if (rgb.empty() || rgb.channels() != 3) {
    return InvalidArgumentError("expected a non-empty 3-channel image");
  }
  cv::Mat bgr(rgb.height(), rgb.width(), CV_8UC3);
  for (int y = 0; y < rgb.height(); ++y) {
    const float* in = rgb.row(y);
    auto* out = bgr.ptr<uint8_t>(y);
    for (int x = 0; x < rgb.width(); ++x) {
      for (int c = 0; c < 3; ++c) {
        const float v = std::clamp(in[x * 3 + c], 0.0f, 1.0f);
        out[x * 3 + (2 - c)] = static_cast<uint8_t>(std::lround(v * 255.0f));
      }
    }
  }
  if (!cv::imwrite(path, bgr)) {
    return InternalError("failed to write '" + path + "'");
  }
  return OkStatus();
}

}  // namespace minicam
