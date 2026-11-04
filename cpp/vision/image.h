// Minimal image container and colour helpers.
//
// The C++ build deliberately avoids a heavy imaging dependency: the only images
// the navigator needs are the small radar crop and the pre-built walkable
// raster, both of which are handled here. PPM/PGM support exists so test frames
// captured by the Python tooling can be replayed into the C++ tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cs2n {

struct Hsv {
  double hue = 0.0;        // 0..179, matching the OpenCV convention
  double saturation = 0.0; // 0..255
  double value = 0.0;      // 0..255
};

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};
// A Windows 32-bit BI_RGB DIB stores B,G,R,X bytes (0x00RRGGBB).
inline Rgb rgbFromDib(std::uint32_t pixel) {
  return {static_cast<std::uint8_t>((pixel>>16)&255),static_cast<std::uint8_t>((pixel>>8)&255),static_cast<std::uint8_t>(pixel&255)};
}

class Image {
 public:
  Image() = default;
  Image(int width, int height, Rgb fill = {});

  [[nodiscard]] bool empty() const { return width_ <= 0 || height_ <= 0; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int pixelCount() const { return width_ * height_; }
  [[nodiscard]] const std::vector<Rgb>& pixels() const { return pixels_; }
  [[nodiscard]] std::vector<Rgb>& pixels() { return pixels_; }

  [[nodiscard]] bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
  [[nodiscard]] const Rgb& at(int x, int y) const { return pixels_[static_cast<std::size_t>(y * width_ + x)]; }
  Rgb& at(int x, int y) { return pixels_[static_cast<std::size_t>(y * width_ + x)]; }

  void set(int x, int y, const Rgb& value) { if (inBounds(x, y)) at(x, y) = value; }

  // Sub-image in pixel coordinates; returns an empty image when the rectangle
  // leaves the frame.
  [[nodiscard]] Image crop(int x, int y, int width, int height) const;

  static Hsv toHsv(const Rgb& rgb);
  [[nodiscard]] std::vector<Hsv> hsv() const;

  // Binary (P5) and colour (P6) Netpbm, plus the ASCII variants for fixtures.
  static bool loadPnm(const std::string& path, Image& out, std::string& error);
  static bool savePpm(const Image& image, const std::string& path, std::string& error);
  static bool loadGrayPgm(const std::string& path, std::vector<std::uint8_t>& out, int& width, int& height,
                          std::string& error);
  static bool savePgm(const std::vector<std::uint8_t>& gray, int width, int height, const std::string& path,
                      std::string& error);

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<Rgb> pixels_;
};

}  // namespace cs2n
