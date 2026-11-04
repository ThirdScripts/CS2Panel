#include "vision/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace cs2n {
namespace {

// Reads one whitespace/comments-delimited PNM token.
bool nextToken(std::istream& stream, std::string& token) {
  token.clear();
  char c = 0;
  while (stream.get(c)) {
    if (c == '#') {
      stream.ignore(4096, '\n');
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(c))) {
      token.push_back(c);
      break;
    }
  }
  if (token.empty()) return false;
  while (stream.get(c)) {
    if (c == '#') {
      stream.ignore(4096, '\n');
      break;
    }
    if (std::isspace(static_cast<unsigned char>(c))) break;
    token.push_back(c);
  }
  return true;
}

}  // namespace

Image::Image(int width, int height, Rgb fill)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill) {}

Image Image::crop(int x, int y, int width, int height) const {
  if (x < 0 || y < 0 || width <= 0 || height <= 0) return {};
  if (x + width > width_ || y + height > height_) return {};
  Image result(width, height);
  for (int row = 0; row < height; ++row) {
    const std::size_t source = static_cast<std::size_t>((y + row) * width_ + x);
    const std::size_t target = static_cast<std::size_t>(row * width);
    std::copy(pixels_.begin() + static_cast<std::ptrdiff_t>(source),
              pixels_.begin() + static_cast<std::ptrdiff_t>(source + width),
              result.pixels_.begin() + static_cast<std::ptrdiff_t>(target));
  }
  return result;
}

Hsv Image::toHsv(const Rgb& rgb) {
  const double r = rgb.r / 255.0;
  const double g = rgb.g / 255.0;
  const double b = rgb.b / 255.0;
  const double maxChannel = std::max({r, g, b});
  const double minChannel = std::min({r, g, b});
  const double delta = maxChannel - minChannel;

  Hsv hsv;
  hsv.value = maxChannel * 255.0;
  hsv.saturation = maxChannel <= 1e-9 ? 0.0 : (delta / maxChannel) * 255.0;

  if (delta <= 1e-9) {
    hsv.hue = 0.0;
  } else {
    double hueDeg = 0.0;  // full-circle degrees, 0..360
    if (maxChannel == r) {
      hueDeg = 60.0 * std::fmod((g - b) / delta, 6.0);
    } else if (maxChannel == g) {
      hueDeg = 60.0 * ((b - r) / delta + 2.0);
    } else {
      hueDeg = 60.0 * ((r - g) / delta + 4.0);
    }
    if (hueDeg < 0.0) hueDeg += 360.0;
    hsv.hue = hueDeg * 0.5;  // 0..179, the scale the colour bands use
  }
  return hsv;
}

std::vector<Hsv> Image::hsv() const {
  std::vector<Hsv> out(pixels_.size());
  for (std::size_t i = 0; i < pixels_.size(); ++i) out[i] = toHsv(pixels_[i]);
  return out;
}

bool Image::loadPnm(const std::string& path, Image& out, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open " + path;
    return false;
  }
  std::string magic;
  std::string widthToken, heightToken, maxToken;
  if (!nextToken(file, magic) || (magic != "P6" && magic != "P3" && magic != "P5")) {
    error = path + ": unsupported PNM magic";
    return false;
  }
  if (!nextToken(file, widthToken) || !nextToken(file, heightToken) || !nextToken(file, maxToken)) {
    error = path + ": truncated header";
    return false;
  }
  const int width = std::atoi(widthToken.c_str());
  const int height = std::atoi(heightToken.c_str());
  const int maxValue = std::atoi(maxToken.c_str());
  if (width <= 0 || height <= 0 || maxValue <= 0 || maxValue > 255) {
    error = path + ": invalid dimensions or range";
    return false;
  }
  out = Image(width, height);
  if (magic == "P6") {
    std::vector<char> buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() != static_cast<std::streamsize>(buffer.size())) {
      error = path + ": truncated pixel data";
      return false;
    }
    for (std::size_t i = 0, p = 0; i < buffer.size(); i += 3, ++p) {
      out.pixels()[p] = Rgb{static_cast<std::uint8_t>(buffer[i]), static_cast<std::uint8_t>(buffer[i + 1]),
                            static_cast<std::uint8_t>(buffer[i + 2])};
    }
  } else if (magic == "P5") {
    std::vector<char> buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() != static_cast<std::streamsize>(buffer.size())) {
      error = path + ": truncated pixel data";
      return false;
    }
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      const auto value = static_cast<std::uint8_t>(buffer[i]);
      out.pixels()[i] = Rgb{value, value, value};
    }
  } else {
    for (int i = 0; i < out.pixelCount(); ++i) {
      std::string rToken, gToken, bToken;
      if (!nextToken(file, rToken) || !nextToken(file, gToken) || !nextToken(file, bToken)) {
        error = path + ": truncated pixel data";
        return false;
      }
      out.pixels()[static_cast<std::size_t>(i)] =
          Rgb{static_cast<std::uint8_t>(std::atoi(rToken.c_str())),
              static_cast<std::uint8_t>(std::atoi(gToken.c_str())),
              static_cast<std::uint8_t>(std::atoi(bToken.c_str()))};
    }
  }
  return true;
}

bool Image::savePpm(const Image& image, const std::string& path, std::string& error) {
  if (image.empty()) {
    error = "image is empty";
    return false;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "cannot write " + path;
    return false;
  }
  file << "P6\n" << image.width() << " " << image.height() << "\n255\n";
  std::vector<char> buffer(image.pixels().size() * 3);
  for (std::size_t i = 0; i < image.pixels().size(); ++i) {
    buffer[i * 3 + 0] = static_cast<char>(image.pixels()[i].r);
    buffer[i * 3 + 1] = static_cast<char>(image.pixels()[i].g);
    buffer[i * 3 + 2] = static_cast<char>(image.pixels()[i].b);
  }
  file.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  return true;
}

bool Image::loadGrayPgm(const std::string& path, std::vector<std::uint8_t>& out, int& width, int& height,
                        std::string& error) {
  Image image;
  if (!loadPnm(path, image, error)) return false;
  width = image.width();
  height = image.height();
  out.resize(static_cast<std::size_t>(image.pixelCount()));
  for (int i = 0; i < image.pixelCount(); ++i) {
    const Rgb& pixel = image.pixels()[static_cast<std::size_t>(i)];
    out[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((pixel.r * 299 + pixel.g * 587 + pixel.b * 114) / 1000);
  }
  return true;
}

bool Image::savePgm(const std::vector<std::uint8_t>& gray, int width, int height, const std::string& path,
                    std::string& error) {
  if (width <= 0 || height <= 0 || gray.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    error = "gray buffer size does not match dimensions";
    return false;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "cannot write " + path;
    return false;
  }
  file << "P5\n" << width << " " << height << "\n255\n";
  file.write(reinterpret_cast<const char*>(gray.data()), static_cast<std::streamsize>(gray.size()));
  return true;
}

}  // namespace cs2n
