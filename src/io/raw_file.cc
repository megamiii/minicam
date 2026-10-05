#include "minicam/io/raw_file.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>

namespace minicam {
namespace {

static_assert(std::endian::native == std::endian::little,
              "the raw file reader assumes a little-endian host");

constexpr std::array<char, 8> kMagic = {'M', 'C', 'A', 'M', 'R', 'A', 'W', '1'};
constexpr uint32_t kMaxDimension = 1u << 16;

template <typename T>
void Put(std::ofstream& out, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool Get(std::ifstream& in, T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

}  // namespace

Status WriteRawFile(const Frame& frame, const std::string& path) {
  const Image<uint16_t>& raw = frame.raw;
  if (raw.empty() || raw.channels() != 1) {
    return InvalidArgumentError("frame has no 1-channel raw mosaic");
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) return NotFoundError("cannot open '" + path + "' for writing");

  const FrameMetadata& m = frame.meta;
  Put(out, kMagic);
  Put(out, static_cast<uint32_t>(raw.width()));
  Put(out, static_cast<uint32_t>(raw.height()));
  Put(out, static_cast<uint8_t>(m.cfa));
  Put(out, static_cast<uint8_t>(m.orientation_quarter_turns & 3));
  Put(out, std::array<uint8_t, 2>{});
  Put(out, m.white_level);
  Put(out, m.black_level);
  Put(out, m.wb_gains);
  Put(out, m.color_matrix);
  Put(out, m.exposure_time_s);
  Put(out, m.analog_gain);
  Put(out, m.frame_id);
  Put(out, m.timestamp_ns);
  out.write(reinterpret_cast<const char*>(raw.pixels().data()),
            static_cast<std::streamsize>(raw.size() * sizeof(uint16_t)));
  if (!out) return DataLossError("failed while writing '" + path + "'");
  return OkStatus();
}

Status ReadRawFile(const std::string& path, Frame& frame) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return NotFoundError("cannot open '" + path + "'");

  std::array<char, 8> magic{};
  if (!Get(in, magic) || magic != kMagic) {
    return DataLossError("'" + path + "' is not a minicam raw file");
  }

  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t cfa = 0;
  uint8_t orientation = 0;
  std::array<uint8_t, 2> reserved{};
  FrameMetadata m;
  if (!Get(in, width) || !Get(in, height) || !Get(in, cfa) ||
      !Get(in, orientation) || !Get(in, reserved) || !Get(in, m.white_level) ||
      !Get(in, m.black_level) || !Get(in, m.wb_gains) ||
      !Get(in, m.color_matrix) || !Get(in, m.exposure_time_s) ||
      !Get(in, m.analog_gain) || !Get(in, m.frame_id) ||
      !Get(in, m.timestamp_ns)) {
    return DataLossError("truncated header in '" + path + "'");
  }
  if (width == 0 || height == 0 || width > kMaxDimension ||
      height > kMaxDimension) {
    return DataLossError("invalid image size in '" + path + "'");
  }
  if (cfa > static_cast<uint8_t>(CfaPattern::kGbrg)) {
    return DataLossError("invalid CFA pattern in '" + path + "'");
  }
  m.cfa = static_cast<CfaPattern>(cfa);
  m.orientation_quarter_turns = orientation & 3;

  frame.raw.Resize(static_cast<int>(width), static_cast<int>(height), 1);
  in.read(reinterpret_cast<char*>(frame.raw.pixels().data()),
          static_cast<std::streamsize>(frame.raw.size() * sizeof(uint16_t)));
  if (!in) return DataLossError("truncated pixel data in '" + path + "'");

  frame.meta = m;
  return OkStatus();
}

}  // namespace minicam
