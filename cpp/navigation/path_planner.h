// A* path planning over the navigation mesh, plus path smoothing.
#pragma once

#include <string>
#include <vector>

#include "navigation/navmesh.h"

namespace cs2n {

struct PathPoint {
  Vec2 position;
  double floorZ = 0.0;
  AreaId area = kInvalidArea;
};

struct Path {
  std::vector<PathPoint> points;

  [[nodiscard]] bool empty() const { return points.empty(); }
  [[nodiscard]] std::size_t size() const { return points.size(); }
  void clear() { points.clear(); }

  [[nodiscard]] double length() const {
    double total = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) total += distance(points[i - 1].position, points[i].position);
    return total;
  }

  // Index of the point the controller should steer towards, given the player has
  // already come within `arrivalRadius` of earlier points. Falls back to the
  // final point.
  [[nodiscard]] std::size_t lookaheadIndex(const Vec2& position, double lookaheadDistance,
                                           std::size_t fromIndex) const;
};

struct PathPlannerConfig {
  double crouchPenalty = 1.4;  // cost multiplier for areas requiring ducking
  double jumpPenalty = 2.0;    // cost multiplier for jumps
  double dropPenalty = 1.5;    // cost multiplier per dangerous drop
  double arriveRadius = 24.0;  // how close counts as "reached" during smoothing
  bool smooth = true;
};

class PathPlanner {
 public:
  PathPlanner() = default;
  explicit PathPlanner(const NavMesh* mesh, PathPlannerConfig config = {}) : mesh_(mesh), config_(config) {}

  void setMesh(const NavMesh* mesh) { mesh_ = mesh; }
  void setConfig(const PathPlannerConfig& config) { config_ = config; }

  [[nodiscard]] bool ready() const { return mesh_ != nullptr && mesh_->valid(); }

  // Plans a route from `from` (with the height the player currently stands at)
  // to `to`. Returns false and fills `error` when either endpoint cannot be
  // attached to the mesh or no route exists.
  bool plan(const Vec2& from, double fromZ, const Vec2& to, double toZ, Path& out, std::string& error) const;

  // Drops points the player has already passed and re-anchors the head of the
  // path to the current position. Used while following, not for replanning.
  static void advance(Path& path, const Vec2& position, double arriveRadius);

 private:
  bool smoothPath(Path& path) const;
  double connectionCost(const Connection& connection) const;

  const NavMesh* mesh_ = nullptr;
  PathPlannerConfig config_;
};

}  // namespace cs2n
