// Builds a NavMesh from a walkable raster.
//
// This is the bridge between "map data as an image" (the Dust II radar-derived
// walkable mask, or a rasterised .nav export) and the polygonal mesh the planner
// works with. The raster is blocked into cells, walkable blocks are merged into
// convex rectangles, and neighbouring rectangles are connected automatically.
//
// A second, optional raster carries floor height per pixel. That is what encodes
// tunnels: two areas can overlap in X/Y yet never be connected because the height
// difference between them exceeds what the player can walk.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "navigation/navmesh.h"

namespace cs2n {

struct RasterImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> walkable;  // non-zero == walkable
  std::vector<float> floorHeight;      // optional; empty means "flat"

  [[nodiscard]] bool valid() const {
    return width > 0 && height > 0 && static_cast<int>(walkable.size()) == width * height &&
           (floorHeight.empty() || static_cast<int>(floorHeight.size()) == width * height);
  }
  [[nodiscard]] bool isWalkable(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return walkable[static_cast<std::size_t>(y * width + x)] != 0;
  }
  [[nodiscard]] float heightAt(int x, int y) const {
    if (floorHeight.empty()) return 0.0f;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
    return floorHeight[static_cast<std::size_t>(y * width + x)];
  }
};

struct NavMeshBuilderConfig {
  double unitsPerPixel = 1.0;   // map units covered by one raster pixel
  int blockPixels = 8;          // raster pixels merged into one nav block
  double blockFillRatio = 0.6;  // fraction of a block that must be walkable
  double mergeTolerance = 1.0;  // world units; rectangles closer than this connect
  std::string mapName;
};

class NavMeshBuilder {
 public:
  static bool build(const RasterImage& raster, const NavMeshBuilderConfig& config, NavMesh& out,
                    std::string& error);

  // Convenience: thresholds an 8-bit luminance raster, treating values inside
  // [minValue, maxValue] as walkable. This matches how the radar frames encode
  // floor space as mid-grey while walls and props stay dark or bright.
  static RasterImage fromLuminance(const std::vector<std::uint8_t>& luminance, int width, int height,
                                   std::uint8_t minValue, std::uint8_t maxValue);
};

}  // namespace cs2n
