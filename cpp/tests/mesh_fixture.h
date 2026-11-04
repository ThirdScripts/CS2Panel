// Shared fixtures: a small deterministic mesh used by several test suites.
#pragma once

#include <string>

#include "navigation/navmesh.h"

namespace cs2n::test {

// Builds a rectangular room subdivided into a grid of areas, optionally with a
// wall that leaves a single gap. `wallColumn` < 0 means "no wall".
inline NavMesh makeRoomMesh(int columns = 4, int rows = 4, double cellSize = 100.0, int wallColumn = -1,
                            int wallGapRow = 1) {
  NavMesh mesh;
  mesh.setName("test-room");
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      if (column == wallColumn && row != wallGapRow) continue;
      NavArea area;
      const double left = column * cellSize;
      const double top = row * cellSize;
      area.polygon = {{left, top},
                      {left + cellSize, top},
                      {left + cellSize, top + cellSize},
                      {left, top + cellSize}};
      area.floorZ = 0.0;
      area.ceilingZ = 72.0;
      mesh.addArea(std::move(area));
    }
  }
  autoConnectAreas(mesh, 1.0);
  mesh.finalize();
  return mesh;
}

// Two floors joined by a short staircase of areas, so vertical handling can be
// exercised independently of the planar mesh.
inline NavMesh makeStairsMesh() {
  NavMesh mesh;
  mesh.setName("test-stairs");
  for (int step = 0; step < 6; ++step) {
    NavArea area;
    const double left = step * 50.0;
    area.polygon = {{left, 0.0}, {left + 50.0, 0.0}, {left + 50.0, 50.0}, {left, 50.0}};
    area.floorZ = step * 16.0;
    area.ceilingZ = area.floorZ + 72.0;
    mesh.addArea(std::move(area));
  }
  // Step up is walkable, so connections come from the height rules.
  autoConnectAreas(mesh, 1.0);
  mesh.finalize();
  return mesh;
}

}  // namespace cs2n::test
