// Color transfer functions.

#ifndef MINICAM_COLOR_H_
#define MINICAM_COLOR_H_

namespace minicam {

// The sRGB transfer curve (IEC 61966-2-1). Images on disk are stored
// "gamma encoded" so that 8 bits are spent evenly in perceived brightness;
// sensors measure linear light. Both functions map [0, 1] to [0, 1].
float SrgbToLinear(float encoded);
float LinearToSrgb(float linear);

}  // namespace minicam

#endif  // MINICAM_COLOR_H_
