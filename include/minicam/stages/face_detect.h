// HOG template-matching face detection (optional final stage).

#ifndef MINICAM_STAGES_FACE_DETECT_H_
#define MINICAM_STAGES_FACE_DETECT_H_

#include <memory>
#include <string_view>
#include <vector>

#include "minicam/frame.h"
#include "minicam/hog.h"
#include "minicam/image.h"
#include "minicam/stage.h"
#include "minicam/status.h"

namespace minicam {

struct FaceDetectionOptions {
  HogParams hog;
  // A window is a candidate when its NCC with the template exceeds this.
  float ncc_threshold = 0.49f;
  // Non-maximum suppression drops boxes overlapping a better one by more
  // than this intersection-over-union.
  float nms_iou = 0.5f;
  // Window step in pixels (of the downscaled image).
  int stride = 3;
  // Integer factors by which the image is shrunk (block averaging) before
  // scanning: an image pyramid. A face is found at the factor where it is
  // about template-sized, so several factors cover several face sizes;
  // neighboring factors should differ by at most ~20%.
  std::vector<int> downscales = {1};
  // Threads for rows of windows; 0 means one per hardware thread.
  int num_threads = 0;
};

// Finds template-like faces in frame.rgb (sRGB-encoded, i.e. after tone
// mapping) and writes them to frame.detections, in frame.rgb coordinates.
//
//   1. For each factor in `downscales`: gray = 0.299 R + 0.587 G + 0.114 B,
//      averaged over factor x factor blocks.
//   2. Sobel gradients and an integral histogram, once per scale.
//   3. For every window of the template's size (step `stride`): HOG
//      descriptor, mean-centered; score = normalized cross-correlation (the
//      cosine) with the mean-centered template descriptor.
//   4. Windows above ncc_threshold from all scales, converted to frame.rgb
//      pixels and reduced by one greedy non-maximum suppression.
// A rigid template, matched over a small image pyramid: the C++ version of a
// template-matching HOG detector, not a trained classifier.
class HogFaceDetectionStage final : public Stage {
 public:
  // `face_template` is a 1-channel image in [0, 1] at the detection scale.
  static StatusOr<std::unique_ptr<HogFaceDetectionStage>> Create(
      const Image<float>& face_template, FaceDetectionOptions options = {});

  std::string_view Name() const override { return "face_detect_hog"; }
  Status Process(Frame& frame) override;

 private:
  // Scores every window at one downscale factor and appends the candidates
  // (in frame.rgb pixels).
  void ScanScale(const Image<float>& rgb, int factor,
                 std::vector<Detection>& candidates);

  HogFaceDetectionStage(FaceDetectionOptions options, int template_width,
                        int template_height, std::vector<float> descriptor);

  FaceDetectionOptions options_;
  int template_width_;
  int template_height_;
  std::vector<float> template_descriptor_;  // Mean-centered, unit length.
  // Per-frame scratch, reused.
  Image<float> gray_;
  GradientField gradients_;
  IntegralHistogram integral_;
};

// Greedy non-maximum suppression: repeatedly keep the highest-scoring box and
// drop the remaining boxes that overlap it by more than `max_iou`.
std::vector<Detection> NonMaxSuppression(std::vector<Detection> boxes,
                                         float max_iou);

float IntersectionOverUnion(const Detection& a, const Detection& b);

// Converts an RGB image to gray with the Rec.601 weights (as OpenCV does).
void RgbToGray(const Image<float>& rgb, Image<float>& gray);

// Gray image averaged over factor x factor blocks: exactly what the detector
// scans at that factor. Use it to cut templates from the same domain.
void DownscaledGray(const Image<float>& rgb, int factor, Image<float>& gray);

// Draws rectangle outlines (at least 2 pixels, thicker for large boxes) into
// an RGB image, for visualization.
void DrawDetections(const std::vector<Detection>& detections,
                    Image<float>& rgb);

}  // namespace minicam

#endif  // MINICAM_STAGES_FACE_DETECT_H_
