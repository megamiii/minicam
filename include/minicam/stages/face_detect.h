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
  // Detect on the image shrunk by this integer factor (block averaging).
  // Faces must be about template-sized at that scale.
  int downscale = 1;
  // Threads for rows of windows; 0 means one per hardware thread.
  int num_threads = 0;
};

// Finds template-like faces in frame.rgb (sRGB-encoded, i.e. after tone
// mapping) and writes them to frame.detections, in frame.rgb coordinates.
//
//   1. Gray = 0.299 R + 0.587 G + 0.114 B, optionally downscaled.
//   2. Sobel gradients and an integral histogram, computed once per frame.
//   3. For every window of the template's size (step `stride`): HOG
//      descriptor, mean-centered; score = normalized cross-correlation (the
//      cosine) with the mean-centered template descriptor.
//   4. Windows above ncc_threshold, reduced by greedy non-maximum
//      suppression.
// Single scale, rigid template: the C++ version of a template-matching HOG
// detector, not a trained classifier.
class HogFaceDetectionStage final : public Stage {
 public:
  // `face_template` is a 1-channel image in [0, 1] at the detection scale.
  static StatusOr<std::unique_ptr<HogFaceDetectionStage>> Create(
      const Image<float>& face_template, FaceDetectionOptions options = {});

  std::string_view Name() const override { return "face_detect_hog"; }
  Status Process(Frame& frame) override;

 private:
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

// Draws 2-pixel rectangle outlines into an RGB image, for visualization.
void DrawDetections(const std::vector<Detection>& detections,
                    Image<float>& rgb);

}  // namespace minicam

#endif  // MINICAM_STAGES_FACE_DETECT_H_
