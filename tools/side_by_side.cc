// Builds a side-by-side comparison image for viewing (e.g. for the README).
//
//   side_by_side [--crop fx,fy,fw,fh] <output.jpg> <width per image>
//                <image> <image> [...]
//
// Each image is optionally cropped (fractions of its width and height, so the
// same region is cut from images of different sizes), resized to the given
// width keeping its aspect ratio, padded to the same height and placed left to
// right. Enlargement uses nearest-neighbor so pixel-level artifacts stay
// visible. Used only for display; OpenCV does the resizing, as allowed for
// display and verification.

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <string_view>
#include <system_error>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

int main(int argc, char** argv) {
  // Optional leading --crop fx,fy,fw,fh.
  double crop[4] = {0.0, 0.0, 1.0, 1.0};
  if (argc > 2 && std::string_view(argv[1]) == "--crop") {
    if (std::sscanf(argv[2], "%lf,%lf,%lf,%lf", &crop[0], &crop[1], &crop[2],
                    &crop[3]) != 4 ||
        crop[0] < 0.0 || crop[1] < 0.0 || crop[2] <= 0.0 || crop[3] <= 0.0 ||
        crop[0] + crop[2] > 1.0 || crop[1] + crop[3] > 1.0) {
      std::cerr << "error: bad --crop, expected fx,fy,fw,fh in [0, 1]\n";
      return 1;
    }
    argv += 2;
    argc -= 2;
  }
  if (argc < 4) {
    std::cerr << "usage: side_by_side [--crop fx,fy,fw,fh] <output.jpg> "
                 "<width per image> <image> [image ...]\n";
    return 1;
  }
  const std::string_view width_arg = argv[2];
  int width = 0;
  auto [ptr, ec] = std::from_chars(width_arg.data(),
                                   width_arg.data() + width_arg.size(), width);
  if (ec != std::errc() || width <= 0) {
    std::cerr << "error: bad width\n";
    return 1;
  }

  std::vector<cv::Mat> images;
  int height = 0;
  for (int i = 3; i < argc; ++i) {
    const cv::Mat full = cv::imread(argv[i], cv::IMREAD_COLOR);
    if (full.empty()) {
      std::cerr << "error: cannot read " << argv[i] << "\n";
      return 1;
    }
    const cv::Rect region(static_cast<int>(crop[0] * full.cols),
                          static_cast<int>(crop[1] * full.rows),
                          std::max(1, static_cast<int>(crop[2] * full.cols)),
                          std::max(1, static_cast<int>(crop[3] * full.rows)));
    const cv::Mat image = full(region);
    cv::Mat resized;
    const int h =
        static_cast<int>(static_cast<double>(image.rows) * width / image.cols);
    const int interpolation =
        width > image.cols ? cv::INTER_NEAREST : cv::INTER_AREA;
    cv::resize(image, resized, cv::Size(width, h), 0, 0, interpolation);
    height = std::max(height, h);
    images.push_back(resized);
  }
  constexpr int kGap = 8;
  for (size_t i = 0; i < images.size(); ++i) {
    cv::Mat& image = images[i];
    const int right = i + 1 < images.size() ? kGap : 0;
    cv::copyMakeBorder(image, image, 0, height - image.rows, 0, right,
                       cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
  }
  cv::Mat out;
  cv::hconcat(images, out);
  if (!cv::imwrite(argv[1], out, {cv::IMWRITE_JPEG_QUALITY, 90})) {
    std::cerr << "error: cannot write " << argv[1] << "\n";
    return 1;
  }
  return 0;
}
