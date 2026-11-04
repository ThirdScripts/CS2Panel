#include "navigation/navmesh_builder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2n {
namespace {

struct Block {
  bool walkable = false;
  float floorZ = 0.0f;
  bool visited = false;
};

bool sameHeightBand(const Block& a, const Block& b, double tolerance) {
  return std::fabs(a.floorZ - b.floorZ) <= tolerance;
}

}  // namespace

RasterImage NavMeshBuilder::fromLuminance(const std::vector<std::uint8_t>& luminance, int width, int height,
                                          std::uint8_t minValue, std::uint8_t maxValue) {
  RasterImage raster;
  raster.width = width;
  raster.height = height;
  raster.walkable.resize(static_cast<std::size_t>(width * height));
  for (int i = 0; i < width * height; ++i) {
    const std::uint8_t value = luminance[static_cast<std::size_t>(i)];
    raster.walkable[static_cast<std::size_t>(i)] = (value >= minValue && value <= maxValue) ? 1 : 0;
  }
  return raster;
}

bool NavMeshBuilder::build(const RasterImage& raster, const NavMeshBuilderConfig& config, NavMesh& out,
                           std::string& error) {
  if (!raster.valid()) {
    error = "raster is empty or inconsistent";
    return false;
  }
  if (config.blockPixels < 2) {
    error = "blockPixels must be at least 2";
    return false;
  }
  if (config.unitsPerPixel <= 0.0) {
    error = "unitsPerPixel must be positive";
    return false;
  }

  const int blocksX = raster.width / config.blockPixels;
  const int blocksY = raster.height / config.blockPixels;
  if (blocksX < 1 || blocksY < 1) {
    error = "raster is smaller than one block";
    return false;
  }

  std::vector<Block> blocks(static_cast<std::size_t>(blocksX * blocksY));
  const double blockWorld = static_cast<double>(config.blockPixels) * config.unitsPerPixel;
  const double heightTolerance = kMaxWalkableStep;

  auto blockAt = [&](int x, int y) -> Block& {
    return blocks[static_cast<std::size_t>(y * blocksX + x)];
  };

  // Rasterise blocks: a block is walkable when enough of its pixels are.
  const int pixelsPerBlock = config.blockPixels * config.blockPixels;
  const int requiredPixels = static_cast<int>(std::ceil(pixelsPerBlock * config.blockFillRatio));
  for (int by = 0; by < blocksY; ++by) {
    for (int bx = 0; bx < blocksX; ++bx) {
      int count = 0;
      double heightSum = 0.0;
      for (int py = 0; py < config.blockPixels; ++py) {
        for (int px = 0; px < config.blockPixels; ++px) {
          const int x = bx * config.blockPixels + px;
          const int y = by * config.blockPixels + py;
          if (!raster.isWalkable(x, y)) continue;
          ++count;
          heightSum += raster.heightAt(x, y);
        }
      }
      Block& block = blockAt(bx, by);
      block.walkable = count >= requiredPixels;
      block.floorZ = block.walkable ? static_cast<float>(heightSum / std::max(1, count)) : 0.0f;
    }
  }

  out.clear();
  out.setName(config.mapName);

  // Greedy rectangle merge: grow right, then grow down while the whole strip is
  // walkable and stays within one height band.
  auto stripUsable = [&](int x0, int width, int y) {
    for (int x = x0; x < x0 + width; ++x) {
      const Block& block = blockAt(x, y);
      if (!block.walkable || block.visited) return false;
      if (!sameHeightBand(block, blockAt(x0, y), heightTolerance)) return false;
    }
    return true;
  };

  int rectangleCount = 0;
  for (int by = 0; by < blocksY; ++by) {
    for (int bx = 0; bx < blocksX; ++bx) {
      Block& seed = blockAt(bx, by);
      if (!seed.walkable || seed.visited) continue;

      int width = 1;
      while (bx + width < blocksX) {
        const Block& next = blockAt(bx + width, by);
        if (!next.walkable || next.visited || !sameHeightBand(next, seed, heightTolerance)) break;
        ++width;
      }

      int height = 1;
      while (by + height < blocksY && stripUsable(bx, width, by + height)) ++height;

      for (int y = by; y < by + height; ++y) {
        for (int x = bx; x < bx + width; ++x) blockAt(x, y).visited = true;
      }

      NavArea area;
      area.floorZ = seed.floorZ;
      area.ceilingZ = seed.floorZ + 72.0;
      const double left = bx * blockWorld;
      const double right = (bx + width) * blockWorld;
      const double top = by * blockWorld;
      const double bottom = (by + height) * blockWorld;
      // Counter-clockwise in the raster's own frame (y grows downward).
      area.polygon = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
      out.addArea(std::move(area));
      ++rectangleCount;
    }
  }

  if (rectangleCount < 2) {
    error = "raster produced fewer than two walkable areas";
    return false;
  }

  autoConnectAreas(out, config.mergeTolerance);
  out.finalize();

  if (out.connectionCount() == 0) {
    error = "built mesh has no connections";
    return false;
  }
  return true;
}

}  // namespace cs2n
