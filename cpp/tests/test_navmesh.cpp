#include "navigation/navmesh.h"
#include "navigation/navmesh_builder.h"
#include "navigation/navmesh_io.h"
#include "tests/framework.h"
#include "tests/mesh_fixture.h"
#include "navigation/navigator.h"
#include "combat/geometry.h"
#include <filesystem>
#include <Windows.h>

using namespace cs2n;
#ifdef CS2N_LOCAL_MAP_TEST
CS2N_TEST(NavMesh_ShippedMirageAndDust2BuildRoutes) {
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
  const auto assets=std::filesystem::path(exe).parent_path()/"assets";
  for(const auto* stem:{"dust2","mirage","vertigo","cache","inferno"}) {
    NavMesh mesh;VisibilityMesh visibility;std::string error;
    CHECK_TRUE(NavMeshIo::loadFile((assets/(std::string(stem)+".nav.json")).string(),mesh,error,false));
    CHECK_EQ(mesh.name(),std::string("de_")+stem);
    CHECK_TRUE(visibility.load((assets/(std::string(stem)+".mesh")).string(),error));
    CHECK_TRUE(visibility.triangles()>1000);
    Navigator nav(&mesh);int successful=0;
    for(std::size_t i=0;i<mesh.areaCount();i+=std::max(std::size_t(1),mesh.areaCount()/16)) {
      auto a=mesh.area(static_cast<AreaId>(i));Path path;
      if(nav.planNewRoute(a->centroid(),a->heightAt(a->centroid()),path,error)) {
        ++successful;CHECK_FALSE(path.empty());CHECK_TRUE(path.length()>0);
      }
    }
    CHECK_TRUE(successful>=8);
  }
}
#endif
CS2N_TEST(NavMesh_SpatialIndexMatchesFullSearchAcrossCellsAndFloors) {
  auto mesh=test::makeRoomMesh(8,8);
  NavArea lower;lower.polygon={{-260,-260},{0,-260},{0,0},{-260,0}};lower.floorZ=0;
  mesh.addArea(lower);lower.floorZ=180;mesh.addArea(lower);mesh.finalize();
  for(double x=-265;x<800;x+=17.7)for(double y=-265;y<800;y+=21.3)for(double z:{0.,180.}) {
    auto fast=mesh.findAreaAtHeight({x,y},z,1);
    auto full=mesh.findAreaAtHeight({x,y},z,1.000001);
    CHECK_EQ(fast,full);
  }
}

CS2N_TEST(NavArea_DoesNotTreatAnEdgeExtensionAsInside) {
  NavArea a; a.polygon={{0,0},{100,0},{100,100},{0,100}};
  CHECK_FALSE(a.contains({150,0}));
  CHECK_TRUE(a.contains({50,0}));
}
CS2N_TEST(NavMesh_HeightInterpolationAndFloorSeparation) {
  NavMesh mesh;
  NavArea ramp;ramp.polygon={{0,0},{100,0},{100,100},{0,100}};
  ramp.heights={0,100,100,0};ramp.floorZ=50;
  mesh.addArea(ramp);
  NavArea upper=ramp;upper.heights={200,200,200,200};upper.floorZ=200;
  mesh.addArea(upper);mesh.finalize();
  CHECK_NEAR(mesh.area(0)->heightAt({25,50}),25,1e-6);
  CHECK_EQ(mesh.findAreaAtHeight({25,50},25,1),0);
  CHECK_EQ(mesh.findAreaAtHeight({25,50},200,1),1);
  CHECK_FALSE(mesh.surfaceSegment({25,50},25,{75,50},200));
}

CS2N_TEST(NavMesh_ContainmentAndClosestPoint) {
  NavArea area;
  area.polygon = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  area.floorZ = 0.0;
  area.ceilingZ = 72.0;

  CHECK_TRUE(area.contains({50, 50}));
  CHECK_TRUE(area.contains({1, 1}, 2.0));
  CHECK_FALSE(area.contains({-5, 50}));
  CHECK_FALSE(area.contains({150, 50}));

  // A point outside snaps to the boundary, not to infinity.
  const Vec2 closest = area.closestPoint({-10, 50});
  CHECK_NEAR(closest.x, 0.0, 1e-9);
  CHECK_NEAR(closest.y, 50.0, 1e-9);

  const Vec2 inside = area.closestPoint({50, 50});
  CHECK_NEAR(inside.x, 50.0, 1e-9);
  CHECK_NEAR(inside.y, 50.0, 1e-9);
}

CS2N_TEST(NavMesh_WallBlocksLineOfSight) {
  // Room with a wall down the middle and a single gap at row 1.
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, 2, 1);
  CHECK_TRUE(mesh.valid());

  // Straight across the wall at a row with no gap: blocked.
  CHECK_FALSE(mesh.lineOfSight({50, 350}, {350, 350}));
  // Through the gap: clear.
  CHECK_TRUE(mesh.lineOfSight({50, 150}, {350, 150}));
}

CS2N_TEST(NavMesh_HeightDisambiguatesOverlappingAreas) {
  NavMesh mesh;
  mesh.setName("overlapping");
  NavArea upper;
  upper.polygon = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  upper.floorZ = 200.0;
  upper.ceilingZ = 272.0;
  NavArea lower;
  lower.polygon = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  lower.floorZ = 0.0;
  lower.ceilingZ = 72.0;
  const AreaId upperId = mesh.addArea(upper);
  const AreaId lowerId = mesh.addArea(lower);
  mesh.finalize();

  CHECK_EQ(mesh.findAreaAtHeight({50, 50}, 0.0), lowerId);
  CHECK_EQ(mesh.findAreaAtHeight({50, 50}, 200.0), upperId);

  // The two layers are 200 units apart, so they must not connect.
  bool connected = false;
  for (const Connection& connection : mesh.neighbours(upperId)) {
    if (connection.to == lowerId) connected = true;
  }
  CHECK_FALSE(connected);
}

CS2N_TEST(NavMesh_StairsConnectWithinStepLimit) {
  const NavMesh mesh = cs2n::test::makeStairsMesh();
  CHECK_EQ(mesh.areaCount(), std::size_t(6));
  // 16 units per step is under the walkable limit, so the whole flight joins.
  for (AreaId id = 0; id + 1 < static_cast<AreaId>(mesh.areaCount()); ++id) {
    bool linked = false;
    for (const Connection& connection : mesh.neighbours(id)) {
      if (connection.to == id + 1) linked = true;
    }
    CHECK_TRUE(linked);
  }
}

CS2N_TEST(NavMesh_ClearanceReflectsOpenSpace) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  // Middle of a 100-unit cell, with cells all around: bounded by cell borders.
  const double open = mesh.clearanceAt({150, 150});
  CHECK_GT(open, 40.0);
  // Outside the mesh entirely.
  CHECK_NEAR(mesh.clearanceAt({-500, -500}), 0.0, 1e-9);
}

CS2N_TEST(NavMesh_RoundTripsThroughJson) {
  NavMesh original = cs2n::test::makeRoomMesh(3, 3, 80.0, 1, 1);
  original.setName("round-trip");
  const std::string serialized = NavMeshIo::serialize(original).dump();

  JsonValue root;
  std::string error;
  CHECK_TRUE(JsonValue::parse(serialized, root, error));

  NavMesh reparsed;
  CHECK_TRUE(NavMeshIo::parse(root, reparsed, error, false));
  CHECK_TRUE(reparsed.valid());
  CHECK_EQ(reparsed.areaCount(), original.areaCount());
  CHECK_EQ(reparsed.connectionCount(), original.connectionCount());
  CHECK_EQ(reparsed.name(), std::string("round-trip"));
}

CS2N_TEST(NavMesh_BuilderProducesConnectedMesh) {
  // A 64x64 walkable square split by a wall one block wide, with a gap along
  // the bottom edge. The wall is as thick as one block because a thinner wall
  // cannot be represented at this block size: the builder rasterises by blocks,
  // and a sub-block feature would (correctly) disappear.
  const int size = 64;
  const int wallLeft = 24;
  const int wallRight = 32;  // [24, 32) is exactly one block column at blockPixels=8
  std::vector<std::uint8_t> luminance(static_cast<std::size_t>(size * size), 80);
  for (int y = 0; y < size; ++y) {
    for (int x = wallLeft; x < wallRight; ++x) {
      if (y < 56) luminance[static_cast<std::size_t>(y * size + x)] = 250;  // wall, gap at the bottom
    }
  }
  RasterImage raster = NavMeshBuilder::fromLuminance(luminance, size, size, 32, 125);
  NavMeshBuilderConfig config;
  config.unitsPerPixel = 1.0;
  config.blockPixels = 8;
  config.mapName = "built";
  NavMesh mesh;
  std::string error;
  CHECK_TRUE(NavMeshBuilder::build(raster, config, mesh, error));
  CHECK_TRUE(mesh.valid());
  // Left half, right half and the gap cell: greedy merging collapses each open
  // side into one rectangle, which is the intended shape for a flat grid.
  CHECK_EQ(mesh.areaCount(), std::size_t(3));
  CHECK_GT(mesh.connectionCount(), std::size_t(0));

  // The wall must separate the two halves, and the line below crosses it well
  // away from either edge so no tolerance can bridge the gap.
  CHECK_FALSE(mesh.lineOfSight({10, 10}, {50, 10}));
  CHECK_FALSE(mesh.lineOfSight({10, 30}, {50, 30}));
  // The bottom row is open: both ends are walkable all the way across.
  CHECK_TRUE(mesh.lineOfSight({10, 60}, {50, 60}));
}
