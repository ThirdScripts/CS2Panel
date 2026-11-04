#include "navigation/path_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace cs2n {
namespace {

struct QueueEntry {
  double estimatedTotal = 0.0;
  double costSoFar = 0.0;
  AreaId area = kInvalidArea;

  bool operator<(const QueueEntry& other) const { return estimatedTotal > other.estimatedTotal; }
};

}  // namespace

std::size_t Path::lookaheadIndex(const Vec2& position, double lookaheadDistance, std::size_t fromIndex) const {
  if (points.empty()) return 0;
  for (std::size_t i = std::min(fromIndex, points.size() - 1); i < points.size(); ++i) {
    if (distance(position, points[i].position) > lookaheadDistance) return i;
  }
  return points.size() - 1;
}

double PathPlanner::connectionCost(const Connection& connection) const {
  const NavArea* from = mesh_->area(connection.from);
  const NavArea* to = mesh_->area(connection.to);
  if (from == nullptr || to == nullptr) return std::numeric_limits<double>::max();
  double cost = connection.cost * distance(from->centroid(), to->centroid());
  if (connection.flags & ConnCrouch) cost *= config_.crouchPenalty;
  if (connection.flags & ConnJump) cost *= config_.jumpPenalty;
  if (connection.heightStep < -kMaxWalkableStep) {
    cost *= config_.dropPenalty * (1.0 + std::fabs(connection.heightStep) / 64.0);
  }
  if (connection.flags & ConnLadder) cost *= 1.6;
  return cost;
}

bool PathPlanner::plan(const Vec2& from, double fromZ, const Vec2& to, double toZ, Path& out,
                       std::string& error) const {
  out.clear();
  if (!ready()) {
    error = "navigation mesh is not loaded";
    return false;
  }

  const AreaId startArea = mesh_->findAreaAtHeight(from, fromZ, 128.0);
  const AreaId goalArea = mesh_->findAreaAtHeight(to, toZ, 128.0);
  if (startArea == kInvalidArea) {
    error = "start position is outside the navigation mesh";
    return false;
  }
  if (goalArea == kInvalidArea) {
    error = "destination is outside the navigation mesh";
    return false;
  }

  if (startArea == goalArea) {
    const NavArea* area = mesh_->area(startArea);
    out.points.push_back({from, fromZ, startArea});
    if (mesh_->lineOfSight(from, to)) {
      out.points.push_back({to, toZ, goalArea});
    } else {
      out.points.push_back({area->centroid(), area->floorZ, startArea});
      out.points.push_back({to, toZ, goalArea});
    }
    return true;
  }

  const NavArea* goal = mesh_->area(goalArea);
  const Vec2 goalCenter = goal->centroid();

  std::priority_queue<QueueEntry> open;
  std::unordered_map<AreaId, double> bestCost;
  std::unordered_map<AreaId, AreaId> cameFrom;

  QueueEntry seed;
  seed.area = startArea;
  seed.costSoFar = 0.0;
  seed.estimatedTotal = distance(mesh_->area(startArea)->centroid(), goalCenter);
  bestCost[startArea] = 0.0;
  open.push(seed);

  AreaId reached = kInvalidArea;
  while (!open.empty()) {
    QueueEntry current = open.top();
    open.pop();
    const auto it = bestCost.find(current.area);
    if (it != bestCost.end() && current.costSoFar > it->second) continue;
    if (current.area == goalArea) {
      reached = goalArea;
      break;
    }
    for (const Connection& connection : mesh_->neighbours(current.area)) {
      if(connection.flags & (ConnJump|ConnLadder)) continue;
      const double step = connectionCost(connection);
      if (!std::isfinite(step)) continue;
      const double newCost = current.costSoFar + step;
      const auto known = bestCost.find(connection.to);
      if (known != bestCost.end() && known->second <= newCost) continue;
      bestCost[connection.to] = newCost;
      cameFrom[connection.to] = current.area;
      QueueEntry next;
      next.area = connection.to;
      next.costSoFar = newCost;
      const NavArea* area = mesh_->area(connection.to);
      next.estimatedTotal = newCost + (area != nullptr ? distance(area->centroid(), goalCenter) : 0.0);
      open.push(next);
    }
  }

  if (reached == kInvalidArea) {
    error = "no route between the requested areas";
    return false;
  }

  std::vector<AreaId> areas;
  for (AreaId current = goalArea; current != kInvalidArea; current = cameFrom[current]) {
    areas.push_back(current);
    if (current == startArea) break;
  }
  std::reverse(areas.begin(), areas.end());

  out.points.push_back({from, fromZ, startArea});
  for (std::size_t i = 1; i < areas.size(); ++i) {
    for(const auto& link:mesh_->neighbours(areas[i-1])) {
      if(link.to==areas[i] && link.hasPortal) {
        out.points.push_back({link.portal,link.portalZ,areas[i]});break;
      }
    }
    if(i+1==areas.size()) break;
    const NavArea* area = mesh_->area(areas[i]);
    out.points.push_back({area->centroid(), area->floorZ, areas[i]});
  }
  out.points.push_back({to, toZ, goalArea});

  if (config_.smooth && !smoothPath(out)) {
    // A failed smoothing pass leaves the unsmoothed route, which is still valid.
  }
  return true;
}

bool PathPlanner::smoothPath(Path& path) const {
  if (path.points.size() < 3) return true;
  std::vector<PathPoint> result;
  result.reserve(path.points.size());
  std::size_t index = 0;
  result.push_back(path.points[0]);
  while (index + 1 < path.points.size()) {
    std::size_t furthest = index + 1;
    for (std::size_t candidate = std::min(path.points.size() - 1,index+12); candidate > index + 1; --candidate) {
      const PathPoint& a = path.points[index];
      const PathPoint& b = path.points[candidate];
      if (!mesh_->surfaceSegment(a.position,a.floorZ,b.position,b.floorZ)) continue;
      furthest = candidate;
      break;
    }
    result.push_back(path.points[furthest]);
    index = furthest;
  }
  path.points = std::move(result);
  return true;
}

void PathPlanner::advance(Path& path, const Vec2& position, double arriveRadius) {
  if (path.points.empty()) return;
  // Consume in order: a nearby later point can be on the other side of a wall.
  while(path.points.size()>2 && distance(position,path.points[1].position)<=arriveRadius)
    path.points.erase(path.points.begin()+1);
  if (!path.points.empty()) {
    path.points.front().position = position;
  }
}

}  // namespace cs2n
