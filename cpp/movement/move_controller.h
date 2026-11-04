// Turning a planned path into concrete movement intent.
//
// Continuous lookahead along the route and mesh-checked directional movement.
#pragma once

#include "core/types.h"
#include "navigation/navmesh.h"
#include "navigation/path_planner.h"

namespace cs2n {

struct MovementIntent {
  bool forward = false;
  bool backward = false;
  int strafe = 0;          // -1 left, 0 none, +1 right
  double aimYaw = 0.0;     // where the view should point, radians
  bool arrived = false;
  std::size_t targetIndex = 0;
};

struct MoveConfig {
  double lookaheadDistance = 120.0;   // distance along the route to the steering point
  double arriveRadius = 12.0;         // distance at which a waypoint is considered reached
  double walkAngleDeg = 35.0;         // only walk when within this of the aim
  double wallClearance = 40.0;        // desired clearance from walls while walking
  double strafeClearance = 28.0;      // below this, strafe away from the wall
  double blockedLookahead = 72.0;     // probe distance for "wall ahead"
  bool allowStrafe = true;
};

class MoveController {
 public:
  MoveController() = default;
  explicit MoveController(const MoveConfig& config) : config_(config) {}

  void setConfig(const MoveConfig& config) { config_ = config; }
  [[nodiscard]] const MoveConfig& config() const { return config_; }
  [[nodiscard]] double walkToleranceRadians() const { return config_.walkAngleDeg * kPi / 180.0; }

  void setMesh(const NavMesh* mesh) { mesh_ = mesh; }

  // Works out what to do for the next tick. `path` is advanced in place as
  // waypoints are reached.
  MovementIntent evaluate(Path& path, const Vec2& position, double currentYaw, double floorZ = 0);

  [[nodiscard]] std::size_t targetIndex() const { return targetIndex_; }

 private:
  // Returns -1/0/+1 depending on which side has more room, used when strafing.
  int chooseStrafeSide(const Vec2& position, const Vec2& towards) const;

  MoveConfig config_;
  const NavMesh* mesh_ = nullptr;
  std::size_t targetIndex_ = 0;
  bool forwardHeld_ = false;
  int strafeHeld_ = 0;
};

}  // namespace cs2n
