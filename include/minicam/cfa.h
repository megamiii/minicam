// Color filter array (CFA) helpers.
//
// A Bayer sensor has one color filter per pixel, repeating in a 2x2 tile.
// The pattern is named by the top-left tile read row by row: RGGB means
//   R G
//   G B

#ifndef MINICAM_CFA_H_
#define MINICAM_CFA_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace minicam {

// Channel indices used throughout the RGB images.
inline constexpr int kRed = 0;
inline constexpr int kGreen = 1;
inline constexpr int kBlue = 2;

enum class CfaPattern : uint8_t { kRggb = 0, kBggr = 1, kGrbg = 2, kGbrg = 3 };

// Position of pixel (x, y) inside its 2x2 tile, in 0..3 (row-major). Works for
// negative coordinates too, because x & 1 keeps the parity in two's complement.
constexpr int CfaTileIndex(int x, int y) { return ((y & 1) << 1) | (x & 1); }

// Color (kRed, kGreen or kBlue) of the filter over pixel (x, y).
constexpr int CfaColorAt(CfaPattern pattern, int x, int y) {
  constexpr std::array<std::array<int, 4>, 4> kTable = {{
      {kRed, kGreen, kGreen, kBlue},  // RGGB
      {kBlue, kGreen, kGreen, kRed},  // BGGR
      {kGreen, kRed, kBlue, kGreen},  // GRBG
      {kGreen, kBlue, kRed, kGreen},  // GBRG
  }};
  return kTable[static_cast<int>(pattern)][CfaTileIndex(x, y)];
}

std::string_view CfaPatternName(CfaPattern pattern);
std::optional<CfaPattern> ParseCfaPattern(std::string_view name);

}  // namespace minicam

#endif  // MINICAM_CFA_H_
