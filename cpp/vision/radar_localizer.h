// Locating the player on the radar crop.
//
// This is the C++ counterpart of the working Python detector: the own-player
// disc plus the small white wedge that points where the view aims. Detection is
// a colour-threshold pass followed by connected components, then a search for a
// wedge adjacent to the disc. The output is the radar pixel position and the
// view direction expressed in the radar frame.
//
// It is deliberately strict: zero candidates, or several plausible candidates,
// produce no pose at all. A wrong pose steers the player into a wall, while a
// missing pose only costs a moment of standing still.
#pragma once

#include <string>
#include <vector>

#include "state/player_state.h"
#include "vision/image.h"

namespace cs2n {

struct MarkerTuning {
  // Own-player disc colours. The default matches the calibrated profile.
  Hsv bodyHueLow{15.0, 125.0, 205.0};
  Hsv bodyHueHigh{40.0, 255.0, 255.0};
  // Alternative disc colour, so both team tints are accepted.
  Hsv bodyAltLow{85.0, 100.0, 205.0};
  Hsv bodyAltHigh{125.0, 255.0, 255.0};
  // Direction wedge.
  Hsv wedgeLow{0.0, 0.0, 195.0};
  Hsv wedgeHigh{179.0, 75.0, 255.0};

  int minDiscArea = 35;
  int maxDiscArea = 360;
  int minDiscSide = 6;
  int maxDiscSide = 25;
  double minDiscFill = 0.57;

  int minWedgeArea = 8;
  int maxWedgeArea = 130;
  int maxWedgeSide = 22;
  double wedgeDistanceMin = 0.8;  // multiples of the disc radius
  double wedgeDistanceMax = 2.1;

  // Radar calibration: pixels -> world units, plus the radar origin in world
  // space, so a radar pixel position can be expressed in navmesh coordinates.
  double unitsPerPixel = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
  bool calibrated = false;
};

struct DetectionResult {
  bool ok = false;
  Vec2 worldPosition;
  double yaw = 0.0;         // radians, radar frame
  double confidence = 0.0;  // 0..1, disc solidity
  std::string reason;
  int candidates = 0;
};

class RadarLocalizer {
 public:
  RadarLocalizer() = default;
  explicit RadarLocalizer(const MarkerTuning& tuning) : tuning_(tuning) {}

  void setTuning(const MarkerTuning& tuning) { tuning_ = tuning; }
  [[nodiscard]] const MarkerTuning& tuning() const { return tuning_; }

  DetectionResult locate(const Image& radar,bool requireHeading=true) const;
  // Bearings only, for the verified north-up radar configuration.
  std::vector<double> enemyBearings(const Image& radar) const;

  // Converts a radar pixel into navmesh world coordinates using the tuning.
  [[nodiscard]] Vec2 toWorld(const Vec2& pixel) const;
  // Inverse, for drawing the radar frame with the navmesh overlaid.
  [[nodiscard]] Vec2 toPixel(const Vec2& world) const;

 private:
  struct Blob {
    double cx = 0.0;
    double cy = 0.0;
    int area = 0;
    int width = 0;
    int height = 0;
  };

  MarkerTuning tuning_;
};

}  // namespace cs2n
