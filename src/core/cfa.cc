#include "minicam/cfa.h"

#include <optional>
#include <string_view>

namespace minicam {

std::string_view CfaPatternName(CfaPattern pattern) {
  switch (pattern) {
    case CfaPattern::kRggb:
      return "RGGB";
    case CfaPattern::kBggr:
      return "BGGR";
    case CfaPattern::kGrbg:
      return "GRBG";
    case CfaPattern::kGbrg:
      return "GBRG";
  }
  return "UNKNOWN";
}

std::optional<CfaPattern> ParseCfaPattern(std::string_view name) {
  for (CfaPattern p : {CfaPattern::kRggb, CfaPattern::kBggr, CfaPattern::kGrbg,
                       CfaPattern::kGbrg}) {
    if (CfaPatternName(p) == name) return p;
  }
  return std::nullopt;
}

}  // namespace minicam
