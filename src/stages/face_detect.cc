#include "minicam/stages/face_detect.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "minicam/parallel.h"

namespace minicam {

void RgbToGray(const Image<float>& rgb, Image<float>& gray) {
  gray.Resize(rgb.width(), rgb.height(), 1);
  for (int y = 0; y < rgb.height(); ++y) {
    const float* p = rgb.row(y);
    float* g = gray.row(y);
    for (int x = 0; x < rgb.width(); ++x, p += 3) {
      g[x] = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    }
  }
}

float IntersectionOverUnion(const Detection& a, const Detection& b) {
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.width, b.x + b.width);
  const int y1 = std::min(a.y + a.height, b.y + b.height);
  const float intersection =
      static_cast<float>(std::max(0, x1 - x0) * std::max(0, y1 - y0));
  const float union_area = static_cast<float>(a.width * a.height) +
                           static_cast<float>(b.width * b.height) -
                           intersection;
  return union_area > 0.0f ? intersection / union_area : 0.0f;
}

std::vector<Detection> NonMaxSuppression(std::vector<Detection> boxes,
                                         float max_iou) {
  // Stable, so equal scores keep their (deterministic) input order.
  std::stable_sort(
      boxes.begin(), boxes.end(),
      [](const Detection& a, const Detection& b) { return a.score > b.score; });
  std::vector<Detection> kept;
  for (const Detection& box : boxes) {
    const bool overlaps_kept =
        std::any_of(kept.begin(), kept.end(), [&](const Detection& k) {
          return IntersectionOverUnion(box, k) > max_iou;
        });
    if (!overlaps_kept) kept.push_back(box);
  }
  return kept;
}

void DrawDetections(const std::vector<Detection>& detections,
                    Image<float>& rgb) {
  constexpr int kThickness = 2;
  constexpr float kColor[3] = {0.0f, 1.0f, 0.0f};
  auto paint = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= rgb.width() || y >= rgb.height()) return;
    for (int c = 0; c < 3; ++c) rgb.at(x, y, c) = kColor[c];
  };
  for (const Detection& d : detections) {
    for (int t = 0; t < kThickness; ++t) {
      for (int x = d.x; x < d.x + d.width; ++x) {
        paint(x, d.y + t);
        paint(x, d.y + d.height - 1 - t);
      }
      for (int y = d.y; y < d.y + d.height; ++y) {
        paint(d.x + t, y);
        paint(d.x + d.width - 1 - t, y);
      }
    }
  }
}

StatusOr<std::unique_ptr<HogFaceDetectionStage>> HogFaceDetectionStage::Create(
    const Image<float>& face_template, FaceDetectionOptions options) {
  if (face_template.empty() || face_template.channels() != 1) {
    return InvalidArgumentError("the face template must be a gray image");
  }
  if (options.hog.cell_size < 1 || options.hog.block_size < 1 ||
      options.stride < 1 || options.downscale < 1) {
    return InvalidArgumentError("invalid face detection options");
  }
  std::vector<float> descriptor = ExtractHog(face_template, options.hog);
  if (descriptor.empty()) {
    return InvalidArgumentError(
        "the face template is smaller than one HOG block");
  }
  // Mean-center and scale to unit length once, so each window's score is a
  // single dot product divided by the window's own norm.
  const float mean =
      std::accumulate(descriptor.begin(), descriptor.end(), 0.0f) /
      static_cast<float>(descriptor.size());
  double sum_sq = 0.0;
  for (float& v : descriptor) {
    v -= mean;
    sum_sq += static_cast<double>(v) * v;
  }
  if (sum_sq <= 0.0) {
    return InvalidArgumentError("the face template has no structure");
  }
  const auto inv_norm = static_cast<float>(1.0 / std::sqrt(sum_sq));
  for (float& v : descriptor) v *= inv_norm;

  return std::unique_ptr<HogFaceDetectionStage>(
      new HogFaceDetectionStage(options, face_template.width(),
                                face_template.height(), std::move(descriptor)));
}

HogFaceDetectionStage::HogFaceDetectionStage(FaceDetectionOptions options,
                                             int template_width,
                                             int template_height,
                                             std::vector<float> descriptor)
    : options_(options),
      template_width_(template_width),
      template_height_(template_height),
      template_descriptor_(std::move(descriptor)) {}

Status HogFaceDetectionStage::Process(Frame& frame) {
  const Image<float>& rgb = frame.rgb;
  if (rgb.empty() || rgb.channels() != 3) {
    return InvalidArgumentError("expected a 3-channel RGB image");
  }
  frame.detections.clear();

  // Gray image at the detection scale: average of f x f blocks.
  const int f = options_.downscale;
  const int width = rgb.width() / f;
  const int height = rgb.height() / f;
  gray_.Resize(width, height, 1);
  const float inv_area = 1.0f / static_cast<float>(f * f);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float sum = 0.0f;
      for (int dy = 0; dy < f; ++dy) {
        const float* p = &rgb.at(x * f, y * f + dy);
        for (int dx = 0; dx < f; ++dx, p += 3) {
          sum += 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
        }
      }
      gray_.at(x, y) = sum * inv_area;
    }
  }
  if (width < template_width_ || height < template_height_) {
    return OkStatus();  // Nothing template-sized fits.
  }

  ComputeGradients(gray_, gradients_);
  integral_.Build(gradients_);

  // Score every window; each row of windows is independent.
  const int stride = options_.stride;
  const int rows = (height - template_height_) / stride + 1;
  const int cols = (width - template_width_) / stride + 1;
  const auto n = static_cast<float>(template_descriptor_.size());
  std::vector<std::vector<Detection>> per_row(rows);
  ParallelFor(rows, ResolveThreadCount(options_.num_threads),
              [&](int row_begin, int row_end) {
                std::vector<float> window;
                for (int r = row_begin; r < row_end; ++r) {
                  const int y = r * stride;
                  for (int c = 0; c < cols; ++c) {
                    const int x = c * stride;
                    WindowDescriptor(integral_, x, y, template_width_,
                                     template_height_, options_.hog, window);
                    // The template is mean-centered, so sum(t * (w - mean))
                    // equals sum(t * w).
                    double dot = 0.0;
                    double sum = 0.0;
                    double sum_sq = 0.0;
                    for (size_t i = 0; i < window.size(); ++i) {
                      dot += template_descriptor_[i] * window[i];
                      sum += window[i];
                      sum_sq += static_cast<double>(window[i]) * window[i];
                    }
                    const double centered_sq = sum_sq - sum * sum / n;
                    if (centered_sq <= 0.0) continue;
                    const auto score =
                        static_cast<float>(dot / std::sqrt(centered_sq));
                    if (score > options_.ncc_threshold) {
                      per_row[r].push_back(
                          {x, y, template_width_, template_height_, score});
                    }
                  }
                }
              });

  std::vector<Detection> candidates;
  for (const std::vector<Detection>& row : per_row) {
    candidates.insert(candidates.end(), row.begin(), row.end());
  }
  frame.detections = NonMaxSuppression(std::move(candidates), options_.nms_iou);
  for (Detection& d : frame.detections) {  // Back to frame.rgb pixels.
    d.x *= f;
    d.y *= f;
    d.width *= f;
    d.height *= f;
  }
  return OkStatus();
}

}  // namespace minicam
