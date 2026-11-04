// Radar frame capture via GDI.
//
// Only a rectangle inside the CS2 client area is read. Nothing else on the
// screen is touched, and the capture fails closed when CS2 is not the
// foreground window, so the navigator never acts on stale pixels.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "vision/image.h"

namespace cs2n {

struct CaptureRegion {
  int x = 0;
  int y = 0;
  int width = 320;
  int height = 320;

  [[nodiscard]] bool validate(std::string& error) const;
};

class RadarCapture {
 public:
  RadarCapture() = default;

  // Grabs the configured region from the CS2 client area. Returns an empty
  // optional with a human readable reason when the grab is impossible.
  std::optional<Image> grab(const CaptureRegion& region, std::string& error);

  // Client-area size of the CS2 window, used to sanity check a region.
  static bool cs2ClientSize(int& width, int& height);
};

}  // namespace cs2n
