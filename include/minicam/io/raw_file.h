// minicam's own simple RAW container, used for synthetic RAW images.
//
// Real camera files (DNG) are read with LibRaw instead; this format exists so
// synthetic test data can be saved with exactly the metadata the pipeline
// uses. Layout (little-endian, no padding):
//
//   char[8]  magic "MCAMRAW1"
//   u32      width, height
//   u8       cfa pattern (CfaPattern), u8 orientation (clockwise quarter
//            turns, 0-3), u8[2] reserved (0)
//   u16      white_level, black_level[4]
//   f32      wb_gains[3], color_matrix[9]
//   f64      exposure_time_s
//   f32      analog_gain
//   u64      frame_id
//   i64      timestamp_ns
//   u16      pixels[width * height], row-major

#ifndef MINICAM_IO_RAW_FILE_H_
#define MINICAM_IO_RAW_FILE_H_

#include <string>

#include "minicam/frame.h"
#include "minicam/status.h"

namespace minicam {

// Writes frame.raw and frame.meta.
Status WriteRawFile(const Frame& frame, const std::string& path);

// Reads into frame.raw and frame.meta. Other buffers are left untouched.
Status ReadRawFile(const std::string& path, Frame& frame);

}  // namespace minicam

#endif  // MINICAM_IO_RAW_FILE_H_
