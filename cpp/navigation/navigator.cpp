#include "navigation/navigator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace cs2n {

Navigator::Navigator(const NavMesh* mesh, NavigatorConfig config, PathPlannerConfig plannerConfig)
    : mesh_(mesh), config_(config), planner_(mesh, plannerConfig) {}

void Navigator::setMesh(const NavMesh* mesh) {
  mesh_ = mesh;
  planner_.setMesh(mesh);
  goal_.reset();
  recentGoals_.clear();
}

void Navigator::rememberGoal(const Vec2& goal) {
  recentGoals_.push_back(goal);
  if (recentGoals_.size() > 8) recentGoals_.erase(recentGoals_.begin());
}

std::optional<AreaId> Navigator::chooseGoal(const Vec2& position, double floorZ) const {
  if (!ready()) return std::nullopt;
  const AreaId start = mesh_->findAreaAtHeight(position, floorZ, 128.0);
  if (start == kInvalidArea) return std::nullopt;

  std::unordered_set<AreaId> reachable;
  std::queue<AreaId> pending;
  reachable.insert(start);
  pending.push(start);
  while (!pending.empty()) {
    const AreaId current = pending.front();
    pending.pop();
    for (const Connection& connection : mesh_->neighbours(current)) {
      if (!connection.usableInDirection(current, connection.to)) continue;
      if (connection.flags & (ConnJump | ConnLadder)) continue;
      if (reachable.insert(connection.to).second) pending.push(connection.to);
    }
  }
  if (static_cast<int>(reachable.size()) < config_.reachableAreaMinimum) return std::nullopt;

  std::vector<const NavArea*> candidates;
  candidates.reserve(reachable.size());
  for (const AreaId id : reachable) {
    const NavArea* area = mesh_->area(id);
    if (area == nullptr) continue;
    if (distance(area->centroid(), position) < config_.minGoalDistance) continue;
    candidates.push_back(area);
  }
  if (candidates.empty()) {
    // The map is small or the player is in a corner: relax the distance rule
    // rather than refusing to move at all.
    for (const AreaId id : reachable) {
      const NavArea* area = mesh_->area(id);
      if (area != nullptr && distance(area->centroid(), position) > 64.0) candidates.push_back(area);
    }
  }
  if (candidates.empty()) return std::nullopt;

  switch (config_.strategy) {
    case GoalStrategy::FurthestReachable: {
      const NavArea* best = nullptr;
      double bestScore = -1.0;
      for (const NavArea* area : candidates) {
        const double score = distance(area->centroid(), position);
        if (score > bestScore) {
          bestScore = score;
          best = area;
        }
      }
      return best != nullptr ? std::optional<AreaId>(best->id) : std::nullopt;
    }
    case GoalStrategy::Fixed:
      return mesh_->findAreaAtHeight(config_.fixedGoal,floorZ,128);
    case GoalStrategy::RandomReachable:
    default: {
      // Distance plus a novelty bonus, so the route spreads across the map.
      std::vector<std::pair<double, const NavArea*>> scored;
      double total = 0.0;
      for (const NavArea* area : candidates) {
        double score = distance(area->centroid(), position);
        double novelty = recentGoals_.empty() ? 1000.0 : std::numeric_limits<double>::max();
        for (const Vec2& previous : recentGoals_) novelty = std::min(novelty, distance(area->centroid(), previous));
        if (!std::isfinite(novelty)) novelty = 1000.0;
        score += 1.5 * novelty;
        if (score <= 0.0) continue;
        scored.emplace_back(score, area);
        total += score;
      }
      if (scored.empty()) return std::nullopt;
      std::uniform_real_distribution<double> distribution(0.0, total);
      double pick = distribution(rng_);
      for (const auto& [score, area] : scored) {
        pick -= score;
        if (pick <= 0.0) return area->id;
      }
      return scored.back().second->id;
    }
  }
}

bool Navigator::planNewRoute(const Vec2& position, double floorZ, Path& out, std::string& error) {
  out.clear();
  if (!ready()) {
    error = "navigation mesh is not loaded";
    return false;
  }
  auto chosen = chooseGoal(position, floorZ);
  if (!chosen) {
    error = "no acceptable goal reachable from here";
    return false;
  }
  const AreaId goalArea = *chosen;
  const NavArea* area = mesh_->area(goalArea);
  const double goalZ = area != nullptr ? area->floorZ : floorZ;
  if (!planner_.plan(position, floorZ, area->centroid(), goalZ, out, error)) return false;
  goal_ = area->centroid();
  goalZ_ = goalZ;
  rememberGoal(*goal_);
  return true;
}

bool Navigator::replan(const Vec2& position, double floorZ, Path& out, std::string& error) {
  out.clear();
  if (goal_) {
    const AreaId goalArea = mesh_ != nullptr ? mesh_->findAreaAtHeight(*goal_, goalZ_, 128.0) : kInvalidArea;
    const NavArea* area = mesh_ != nullptr ? mesh_->area(goalArea) : nullptr;
    const double goalZ = area != nullptr ? area->floorZ : floorZ;
    if (planner_.plan(position, floorZ, *goal_, goalZ, out, error)) return true;
  }
  return planNewRoute(position, floorZ, out, error);
}

bool Navigator::planTo(const Vec2& position, double floorZ, const Vec2& destination, double destinationZ,
                       Path& out, std::string& error) {
  out.clear();
  if (!ready()) {
    error = "navigation mesh is not loaded";
    return false;
  }
  if (!planner_.plan(position, floorZ, destination, destinationZ, out, error)) return false;
  goal_ = destination;
  goalZ_ = destinationZ;
  rememberGoal(destination);
  return true;
}

}  // namespace cs2n
