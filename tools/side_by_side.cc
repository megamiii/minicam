// Builds a side-by-side comparison image for viewing (e.g. for the README).
//
//   side_by_side <output.jpg> <width per image> <image> <image> [...]
//
// Each image is resized to the given width (keeping its aspect ratio), all
// are padded to the same height and placed left to right. Used only for
// display; OpenCV does the resizing, as allowed for display and verification.

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: side_by_side <output.jpg> <width per image> <image> "
                 "[image ...]\n";
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
    const cv::Mat image = cv::imread(argv[i], cv::IMREAD_COLOR);
    if (image.empty()) {
      std::cerr << "error: cannot read " << argv[i] << "\n";
      return 1;
    }
    cv::Mat resized;
    const int h =
        static_cast<int>(static_cast<double>(image.rows) * width / image.cols);
    cv::resize(image, resized, cv::Size(width, h), 0, 0, cv::INTER_AREA);
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
