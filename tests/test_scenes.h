// Procedural test images, so tests need no image files.

#ifndef MINICAM_TESTS_TEST_SCENES_H_
#define MINICAM_TESTS_TEST_SCENES_H_

#include <array>

#include "minicam/image.h"

namespace minicam::testing {

// A linear RGB scene with a smooth color gradient (easy for demosaicing), a
// saturated disk with a hard edge, and a patch of thin vertical and
// horizontal stripes (hard: high frequencies near the Bayer sampling limit).
// Values stay inside [0.02, 0.95].
Image<float> MakeTestScene(int width, int height);

// A constant-color linear RGB image.
Image<float> MakeFlatImage(int width, int height, std::array<float, 3> rgb);

}  // namespace minicam::testing

#endif  // MINICAM_TESTS_TEST_SCENES_H_
