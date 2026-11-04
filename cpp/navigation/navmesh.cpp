#include "navigation/navmesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2n {
namespace {

bool pointInConvexPolygon(const std::vector<Vec2>& polygon, const Vec2& point, double epsilon) {
  if (polygon.size() < 3) return false;
  double previous = 0.0;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const Vec2& a = polygon[i];
    const Vec2& b = polygon[(i + 1) % polygon.size()];
    const double cross = (b - a).cross(point - a);
    if (std::fabs(cross) <= epsilon) {
      continue;
    }
    if (previous != 0.0 && cross * previous < 0.0) return false;
    previous = cross;
  }
  return true;
}

Vec2 closestPointOnSegment(const Vec2& a, const Vec2& b, const Vec2& point) {
  const Vec2 edge = b - a;
  const double lengthSq = edge.lengthSq();
  if (lengthSq < 1e-12) return a;
  const double t = clamp((point - a).dot(edge) / lengthSq, 0.0, 1.0);
  return a + edge * t;
}

}  // namespace

bool NavArea::contains(const Vec2& point, double epsilon) const {
  return pointInConvexPolygon(polygon, point, epsilon);
}

double NavArea::heightAt(const Vec2& point) const {
  if(heights.size()!=polygon.size() || polygon.size()<3) return floorZ;
  const Vec2 a=polygon[0], b=polygon[1], c=polygon[2];
  double det=(b-a).cross(c-a);
  if(std::abs(det)<1e-9) return floorZ;
  return heights[0]+(point-a).cross(c-a)/det*(heights[1]-heights[0])+
         (b-a).cross(point-a)/det*(heights[2]-heights[0]);
}

Vec2 NavArea::centroid() const {
  if (polygon.empty()) return {};
  Vec2 sum;
  double weight = 0.0;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const Vec2& a = polygon[i];
    const Vec2& b = polygon[(i + 1) % polygon.size()];
    const double area = 0.5 * a.cross(b);
    sum += (a + b) * area;
    weight += area;
  }
  if (std::fabs(weight) < 1e-9) {
    for (const Vec2& point : polygon) sum += point;
    return sum / static_cast<double>(polygon.size());
  }
  return sum / (3.0 * weight);
}

double NavArea::radius() const {
  if (polygon.empty()) return 0.0;
  const Vec2 center = centroid();
  double best = 0.0;
  for (const Vec2& point : polygon) best = std::max(best, distance(center, point));
  return best;
}

Vec2 NavArea::closestPoint(const Vec2& point) const {
  if (polygon.empty()) return point;
  if (contains(point)) return point;
  Vec2 best = polygon[0];
  double bestDistance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const Vec2 candidate = closestPointOnSegment(polygon[i], polygon[(i + 1) % polygon.size()], point);
    const double d = distanceSq(candidate, point);
    if (d < bestDistance) {
      bestDistance = d;
      best = candidate;
    }
  }
  return best;
}

const NavArea* NavMesh::area(AreaId id) const {
  if (id < 0 || static_cast<std::size_t>(id) >= areas_.size()) return nullptr;
  return &areas_[static_cast<std::size_t>(id)];
}

void NavMesh::clear() {
  areas_.clear();
  cells_.clear();allAreaIds_.clear();
  connections_.clear();
  adjacency_.clear();
  name_.clear();
  boundsMin_ = {};
  boundsMax_ = {};
}

AreaId NavMesh::addArea(NavArea area) {
  cells_.clear();
  area.id = static_cast<AreaId>(areas_.size());
  allAreaIds_.push_back(area.id);
  if (areas_.empty()) {
    boundsMin_ = boundsMax_ = area.centroid();
  }
  for (const Vec2& point : area.polygon) {
    boundsMin_.x = std::min(boundsMin_.x, point.x);
    boundsMin_.y = std::min(boundsMin_.y, point.y);
    boundsMax_.x = std::max(boundsMax_.x, point.x);
    boundsMax_.y = std::max(boundsMax_.y, point.y);
  }
  areas_.push_back(std::move(area));
  return areas_.back().id;
}

void NavMesh::addConnection(Connection connection) {
  connections_.push_back(connection);
}

void NavMesh::finalize() {
  cells_.clear();
  for(const auto& a:areas_) {
    if(a.polygon.empty())continue;
    Vec2 lo=a.polygon.front(),hi=lo;
    for(const auto& p:a.polygon){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
    for(int x=int(std::floor((lo.x-2)/128));x<=int(std::floor((hi.x+2)/128));++x)
      for(int y=int(std::floor((lo.y-2)/128));y<=int(std::floor((hi.y+2)/128));++y)
        cells_[(std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y)].push_back(a.id);
  }
  adjacency_.assign(areas_.size(), {});
  for (const Connection& connection : connections_) {
    if (connection.from < 0 || connection.to < 0) continue;
    if (static_cast<std::size_t>(connection.from) >= areas_.size()) continue;
    if (static_cast<std::size_t>(connection.to) >= areas_.size()) continue;
    adjacency_[static_cast<std::size_t>(connection.from)].push_back(connection);
    if (!(connection.flags & ConnOneWay)) {
      Connection reverse = connection;
      std::swap(reverse.from, reverse.to);
      reverse.heightStep = -connection.heightStep;
      adjacency_[static_cast<std::size_t>(connection.to)].push_back(reverse);
    }
  }
}

const std::vector<Connection>& NavMesh::neighbours(AreaId id) const {
  if (id < 0 || static_cast<std::size_t>(id) >= adjacency_.size()) return emptyAdjacency_;
  return adjacency_[static_cast<std::size_t>(id)];
}

AreaId NavMesh::findArea(const Vec2& point, double maxDistance) const {
  return findAreaAtHeight(point, std::numeric_limits<double>::quiet_NaN(), maxDistance);
}

AreaId NavMesh::findAreaAtHeight(const Vec2& point, double referenceZ, double maxDistance) const {
  const std::vector<AreaId>* ids=&allAreaIds_;
  if(maxDistance<=1 && !cells_.empty()) {
    auto key=(std::uint64_t(std::uint32_t(int(std::floor(point.x/128))))<<32)|std::uint32_t(int(std::floor(point.y/128)));
    auto found=cells_.find(key);if(found==cells_.end())return kInvalidArea;
    ids=&found->second;
  }
  AreaId contained = kInvalidArea;
  double containedScore = std::numeric_limits<double>::max();
  const bool haveHeight = std::isfinite(referenceZ);
  for (AreaId id : *ids) {
    const auto& candidate=areas_[id];
    if (!candidate.contains(point, 1.0)) continue;
    const double score = haveHeight ? std::fabs(candidate.heightAt(point) - referenceZ) : 0.0;
    if(haveHeight && score > 90) continue;
    if (score < containedScore) {
      containedScore = score;
      contained = candidate.id;
    }
  }
  if (contained != kInvalidArea) return contained;

  AreaId nearest = kInvalidArea;
  double bestScore = std::numeric_limits<double>::max();
  for (AreaId id : *ids) {
    const auto& candidate=areas_[id];
    const Vec2 closest = candidate.closestPoint(point);
    const double planar = distance(closest, point);
    if (planar > maxDistance) continue;
    const double vertical = haveHeight ? std::fabs(candidate.heightAt(closest) - referenceZ) : 0.0;
    if(vertical>90) continue;
    const double height = vertical;
    const double score = planar + height;
    if (score < bestScore) {
      bestScore = score;
      nearest = candidate.id;
    }
  }
  return nearest;
}

bool NavMesh::lineOfSight(const Vec2& from, const Vec2& to) const {
  const double length = distance(from, to);
  // The step must be smaller than the narrowest feature that can matter (a wall
  // one block wide); sampling coarser than that walks straight over obstacles.
  constexpr double kSampleStep = 8.0;
  const int samples = clamp(static_cast<int>(length / kSampleStep) + 1, 2, 2048);
  AreaId previousArea = kInvalidArea;
  for (int i = 0; i <= samples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(samples);
    const Vec2 point{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
    const AreaId id = findArea(point, 1.0);
    if (id == kInvalidArea) return false;
    if (previousArea != kInvalidArea && id != previousArea) {
      const NavArea* previous = area(previousArea);
      const NavArea* current = area(id);
      if (previous != nullptr && current != nullptr &&
          std::fabs(previous->floorZ - current->floorZ) > kMaxWalkableStep) {
        return false;  // crossing between vertical layers, treat as a wall
      }
      // Crossing between areas is only possible where they are linked. A step
      // that jumps from one area to an unlinked one would otherwise skip a thin
      // wall; treating that as blocked is the safe direction.
      bool linked = false;
      for (const Connection& connection : neighbours(previousArea)) {
        if (connection.to == id) {
          linked = true;
          break;
        }
      }
      if (!linked) return false;
    }
    previousArea = id;
  }
  return true;
}

bool NavMesh::walkable(const Vec2& point) const { return findArea(point, 1.0) != kInvalidArea; }

bool NavMesh::surfaceSegment(const Vec2& from, double fromZ, const Vec2& to, double toZ) const {
  // Physics can leave the centre a few units outside the eroded nav polygon.
  // Permit only an inward return from this narrow fringe, not arbitrary gaps.
  if(findAreaAtHeight(from,fromZ,1)==kInvalidArea) {
    const auto* entry=area(findAreaAtHeight(from,fromZ,6));
    if(!entry)return false;
    const Vec2 projected=entry->closestPoint(from),inward=projected-from,travel=to-from;
    if(distance(from,projected)>6 || inward.x*travel.x+inward.y*travel.y<=0 ||
       std::abs(entry->heightAt(projected)-fromZ)>19)return false;
    return surfaceSegment(projected,entry->heightAt(projected),to,toZ);
  }
  const int count=std::max(2,static_cast<int>(distance(from,to)/5)+1);
  AreaId prev=kInvalidArea;
  double z=fromZ;
  for(int i=0;i<=count;++i) {
    Vec2 p=from+(to-from)*(static_cast<double>(i)/count);
    AreaId id=findAreaAtHeight(p,z,1);
    const auto* a=area(id);
    if(!a || !a->contains(p,2)) return false;
    double nextZ=a->heightAt(p);
    if(std::abs(nextZ-z)>19) return false;
    if(prev!=kInvalidArea && prev!=id) {
      bool linked=false;
      for(const auto& c:neighbours(prev)) if(c.to==id && !(c.flags&(ConnJump|ConnLadder))) {linked=true;break;}
      if(!linked) return false;
    }
    z=nextZ;prev=id;
  }
  return std::abs(z-toZ)<20;
}

double NavMesh::clearanceAt(const Vec2& point) const {
  if (!walkable(point)) return 0.0;
  constexpr int kDirections = 8;
  constexpr double kMaxProbe = 96.0;
  double clearance = kMaxProbe;
  for (int i = 0; i < kDirections; ++i) {
    const double angle = 2.0 * kPi * static_cast<double>(i) / kDirections;
    const Vec2 direction{std::cos(angle), std::sin(angle)};
    double low = 0.0;
    double high = kMaxProbe;
    // March outward; the boundary is refined by bisection to ~1 unit.
    for (int step = 0; step < 6; ++step) {
      const double mid = (low + high) * 0.5;
      if (walkable(point + direction * mid)) {
        low = mid;
      } else {
        high = mid;
      }
    }
    clearance = std::min(clearance, (low + high) * 0.5);
  }
  return clearance;
}

}  // namespace cs2n
