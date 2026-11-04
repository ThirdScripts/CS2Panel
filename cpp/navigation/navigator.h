// High level navigation: choosing where to go and keeping a route alive.
//
// Unlike the prototype's fixed patrol loop, there is no pre-recorded circuit.
// A goal is chosen from the areas actually reachable from the current position,
// the route is planned over the mesh, and it is thrown away and rebuilt whenever
// the player is no longer making progress or the world moved under them.
#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "navigation/navmesh.h"
#include "navigation/path_planner.h"

namespace cs2n {

enum class GoalStrategy {
  FurthestReachable,  // head for a distant part of the map
  RandomReachable,    // wander, with a bias away from recently visited goals
  Fixed,              // go to one point and stay there
};

struct NavigatorConfig {
  GoalStrategy strategy = GoalStrategy::RandomReachable;
  int reachableAreaMinimum = 2;  // refuse to plan inside a pocket this small
  double minGoalDistance = 320.0; // ignore goals closer than this
  std::uint32_t seed = 0x5EED;
  Vec2 fixedGoal;
};

class Navigator {
 public:
  Navigator() = default;
  Navigator(const NavMesh* mesh, NavigatorConfig config = {}, PathPlannerConfig plannerConfig = {});

  void setMesh(const NavMesh* mesh);
  [[nodiscard]] const NavMesh* mesh() const { return mesh_; }
  void setConfig(const NavigatorConfig& config) { config_ = config; }

  [[nodiscard]] bool ready() const { return mesh_ != nullptr && mesh_->valid(); }

  // Chooses a fresh goal reachable from `position` and plans a route to it.
  // Returns false with a reason when no acceptable goal exists.
  bool planNewRoute(const Vec2& position, double floorZ, Path& out, std::string& error);

  // Replans towards the same goal, used after a recovery manoeuvre.
  bool replan(const Vec2& position, double floorZ, Path& out, std::string& error);

  // Plans straight to an explicit destination.
  bool planTo(const Vec2& position, double floorZ, const Vec2& destination, double destinationZ, Path& out,
              std::string& error);

  [[nodiscard]] std::optional<Vec2> currentGoal() const { return goal_; }
  void clearGoal() { goal_.reset(); }

  // Areas the player has recently asked to reach, so repeated choices spread
  // out instead of hammering the same corner.
  [[nodiscard]] const std::vector<Vec2>& recentGoals() const { return recentGoals_; }

 private:
  std::optional<AreaId> chooseGoal(const Vec2& position, double floorZ) const;
  void rememberGoal(const Vec2& goal);

  const NavMesh* mesh_ = nullptr;
  NavigatorConfig config_;
  PathPlanner planner_;
  std::optional<Vec2> goal_;
  double goalZ_=0;
  std::vector<Vec2> recentGoals_;
  mutable std::mt19937 rng_{config_.seed};
};

}  // namespace cs2n
