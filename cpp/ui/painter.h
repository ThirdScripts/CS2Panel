// Owner-drawn panel that renders the radar frame, the navmesh and the route.
#pragma once

#include <Windows.h>

#include <mutex>
#include <vector>

#include "navigation/navmesh.h"
#include "navigation/path_planner.h"
#include "vision/image.h"

namespace cs2n {

struct DrawModel {
  Image frame;                       // most recent radar capture, may be empty
  std::vector<PathPoint> path;       // current route, radar pixel coordinates
  Vec2 position;                     // player position, radar pixel coordinates
  double yaw = 0.0;
  bool havePose = false;
  const NavMesh* mesh = nullptr;     // optional overlay, world coordinates
};

class Painter {
 public:
  void set(const DrawModel& model);
  [[nodiscard]] DrawModel get() const;

  // Renders into the given device context at the given size.
  void paint(HDC device, int width, int height, int theme = 0) const;

  // Converts radar pixels to client coordinates, valid after paint().
  void setTransform(double scale, double offsetX, double offsetY) const;
  [[nodiscard]] double scale() const { return scale_; }

 private:
  mutable std::mutex mutex_;
  DrawModel model_;
  mutable double scale_ = 1.0;
  mutable double offsetX_ = 0.0;
  mutable double offsetY_ = 0.0;
};

}  // namespace cs2n
