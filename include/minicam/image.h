// A simple owning image buffer.
//
// Pixels are stored in one contiguous std::vector, row-major and interleaved:
// for a 3-channel image the memory is R G B R G B ... This is the baseline
// layout; a planar layout is evaluated later as a performance optimization.

#ifndef MINICAM_IMAGE_H_
#define MINICAM_IMAGE_H_

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace minicam {

template <typename T>
class Image {
 public:
  Image() = default;
  Image(int width, int height, int channels) {
    Resize(width, height, channels);
  }

  // Changes the dimensions. Pixel values are unspecified afterwards. Memory is
  // only reallocated when the new size exceeds the current capacity, so a
  // buffer that is reused for same-sized frames never allocates again.
  void Resize(int width, int height, int channels) {
    width_ = width;
    height_ = height;
    channels_ = channels;
    data_.resize(static_cast<size_t>(width) * height * channels);
  }

  void Fill(T value) { std::fill(data_.begin(), data_.end(), value); }

  int width() const { return width_; }
  int height() const { return height_; }
  int channels() const { return channels_; }
  bool empty() const { return data_.empty(); }
  size_t size() const { return data_.size(); }
  size_t capacity() const { return data_.capacity(); }

  // Number of elements (not bytes) between the starts of two rows.
  size_t stride() const { return static_cast<size_t>(width_) * channels_; }

  T& at(int x, int y, int c = 0) { return data_[Index(x, y, c)]; }
  const T& at(int x, int y, int c = 0) const { return data_[Index(x, y, c)]; }

  T* row(int y) { return data_.data() + static_cast<size_t>(y) * stride(); }
  const T* row(int y) const {
    return data_.data() + static_cast<size_t>(y) * stride();
  }

  std::span<T> pixels() { return data_; }
  std::span<const T> pixels() const { return data_; }

 private:
  size_t Index(int x, int y, int c) const {
    return (static_cast<size_t>(y) * width_ + x) * channels_ + c;
  }

  int width_ = 0;
  int height_ = 0;
  int channels_ = 0;
  std::vector<T> data_;
};

// Writes `in` rotated clockwise by `quarter_turns` x 90 degrees into `out`.
// `out` must not alias `in`; its memory is reused when large enough.
template <typename T>
void RotateClockwise(const Image<T>& in, int quarter_turns, Image<T>& out) {
  const int turns = ((quarter_turns % 4) + 4) % 4;
  const int w = in.width();
  const int h = in.height();
  const int ch = in.channels();
  if (turns % 2 == 0) {
    out.Resize(w, h, ch);
  } else {
    out.Resize(h, w, ch);
  }
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int ox = x;
      int oy = y;
      if (turns == 1) {  // (x, y) -> (h - 1 - y, x)
        ox = h - 1 - y;
        oy = x;
      } else if (turns == 2) {
        ox = w - 1 - x;
        oy = h - 1 - y;
      } else if (turns == 3) {  // (x, y) -> (y, w - 1 - x)
        ox = y;
        oy = w - 1 - x;
      }
      for (int c = 0; c < ch; ++c) out.at(ox, oy, c) = in.at(x, y, c);
    }
  }
}

}  // namespace minicam

#endif  // MINICAM_IMAGE_H_
