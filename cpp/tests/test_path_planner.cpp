#include "navigation/navigator.h"
#include "navigation/path_planner.h"
#include "tests/framework.h"
#include "tests/mesh_fixture.h"

using namespace cs2n;

CS2N_TEST(PathPlanner_RoutesAroundWall) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(5, 5, 100.0, 2, 2);
  PathPlanner planner(&mesh);
  Path path;
  std::string error;

  CHECK_TRUE(planner.plan({50, 450}, 0.0, {450, 450}, 0.0, path, error));
  CHECK_TRUE(path.size() >= 3);

  // Every consecutive pair must be mutually visible, which is what makes the
  // route walkable in practice.
  for (std::size_t i = 1; i < path.points.size(); ++i) {
    CHECK_TRUE(mesh.lineOfSight(path.points[i - 1].position, path.points[i].position));
  }

  // The direct line crosses the wall, so the route must detour through the gap.
  CHECK_FALSE(mesh.lineOfSight({50, 450}, {450, 450}));
}

CS2N_TEST(PathPlanner_SmoothingRemovesRedundantPoints) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  PathPlanner planner(&mesh);
  Path path;
  std::string error;
  CHECK_TRUE(planner.plan({50, 50}, 0.0, {350, 350}, 0.0, path, error));
  const std::size_t smoothed = path.size();
  CHECK_TRUE(smoothed <= 3);  // start, maybe a mid point, goal

  PathPlannerConfig noSmoothing;
  noSmoothing.smooth = false;
  PathPlanner raw(&mesh, noSmoothing);
  Path rawPath;
  CHECK_TRUE(raw.plan({50, 50}, 0.0, {350, 350}, 0.0, rawPath, error));
  CHECK_TRUE(rawPath.size() >= smoothed);
}

CS2N_TEST(PathPlanner_RejectsPointsOutsideMesh) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(3, 3, 100.0, -1, 0);
  PathPlanner planner(&mesh);
  Path path;
  std::string error;
  CHECK_FALSE(planner.plan({-500, -500}, 0.0, {50, 50}, 0.0, path, error));
  CHECK_FALSE(error.empty());
  CHECK_FALSE(planner.plan({50, 50}, 0.0, {9000, 9000}, 0.0, path, error));
}

CS2N_TEST(PathPlanner_UnreachableGoalIsReported) {
  NavMesh mesh;
  mesh.setName("split");
  for (int block = 0; block < 2; ++block) {
    NavArea area;
    const double left = block * 300.0;
    area.polygon = {{left, 0}, {left + 100, 0}, {left + 100, 100}, {left, 100}};
    mesh.addArea(std::move(area));
  }
  // Deliberately no connection between the two islands.
  mesh.finalize();
  PathPlanner planner(&mesh);
  Path path;
  std::string error;
  CHECK_FALSE(planner.plan({50, 50}, 0.0, {350, 50}, 0.0, path, error));
}

CS2N_TEST(Path_LookaheadSkipsNearPoints) {
  Path path;
  path.points.push_back({{0, 0}, 0.0, 0});
  path.points.push_back({{10, 0}, 0.0, 0});
  path.points.push_back({{200, 0}, 0.0, 0});
  path.points.push_back({{400, 0}, 0.0, 0});
  CHECK_EQ(path.lookaheadIndex({0, 0}, 50.0, 0), std::size_t(2));
  CHECK_EQ(path.lookaheadIndex({0, 0}, 1000.0, 0), std::size_t(3));
}

CS2N_TEST(PathPlanner_AdvanceDropsReachedPoints) {
  Path path;
  path.points.push_back({{0, 0}, 0.0, 0});
  path.points.push_back({{30, 0}, 0.0, 0});
  path.points.push_back({{300, 0}, 0.0, 0});
  PathPlanner::advance(path, {30, 0}, 25.0);
  CHECK_EQ(path.size(), std::size_t(2));
  // The head is re-anchored to the current position so steering starts clean.
  CHECK_NEAR(path.points[0].position.x, 30.0, 1e-9);
}

CS2N_TEST(Navigator_ChoosesReachableGoalAndReachesIt) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(5, 5, 100.0, -1, 0);
  NavigatorConfig config;
  config.strategy = GoalStrategy::FurthestReachable;
  Navigator navigator(&mesh, config);
  CHECK_TRUE(navigator.ready());

  Path path;
  std::string error;
  CHECK_TRUE(navigator.planNewRoute({50, 50}, 0.0, path, error));
  CHECK_TRUE(navigator.currentGoal().has_value());

  // The chosen goal must be inside the mesh.
  CHECK_TRUE(mesh.walkable(*navigator.currentGoal()));
}

CS2N_TEST(Navigator_SpreadsGoalsAcrossMap) {
  const NavMesh mesh = cs2n::test::makeRoomMesh(6, 6, 100.0, -1, 0);
  NavigatorConfig config;
  config.strategy = GoalStrategy::RandomReachable;
  config.minGoalDistance = 200.0;
  Navigator navigator(&mesh, config);

  std::vector<Vec2> goals;
  for (int i = 0; i < 6; ++i) {
    Path path;
    std::string error;
    CHECK_TRUE(navigator.planNewRoute({50, 50}, 0.0, path, error));
    goals.push_back(*navigator.currentGoal());
  }

  // Goals should not all collapse onto the same area.
  std::size_t distinct = 0;
  for (std::size_t i = 0; i < goals.size(); ++i) {
    for (std::size_t j = i + 1; j < goals.size(); ++j) {
      if (distance(goals[i], goals[j]) > 50.0) ++distinct;
    }
  }
  CHECK_GT(distinct, std::size_t(0));
}

CS2N_TEST(Navigator_FirstGoalWeightsRemainFiniteAndVary) {
  const auto mesh=cs2n::test::makeRoomMesh(8,8,100.0,-1,0);
  NavigatorConfig config;config.minGoalDistance=100;
  Navigator navigator(&mesh,config);Path path;std::string error;
  CHECK_TRUE(navigator.planNewRoute({50,50},0,path,error));
  const auto first=navigator.currentGoal();CHECK_TRUE(first.has_value());
  bool varied=false;
  for(int i=0;i<100;++i){navigator.setMesh(&mesh);CHECK_TRUE(navigator.planNewRoute({50,50},0,path,error));auto goal=navigator.currentGoal();CHECK_TRUE(goal.has_value());if(distance(*goal,*first)>1)varied=true;}
  CHECK_TRUE(varied);
}
