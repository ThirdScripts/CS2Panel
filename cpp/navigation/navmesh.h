// Navigation mesh model.
//
// The mesh is a set of convex walkable areas. Each area carries a floor height
// and a ceiling height, so vertical geometry (stairs, tunnels, catwalks) is
// represented explicitly instead of being flattened into a 2-D bitmap. Areas are
// linked by connections that record the height step between them, which is what
// lets the planner refuse a drop that would hurt the player but accept a stair.
//
// Coordinates are map units, matching the source data (Source uses 1 unit ~=
// 0.019 m). The mesh is independent of how the player position was obtained.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "core/types.h"

namespace cs2n {

using AreaId = std::int32_t;
inline constexpr AreaId kInvalidArea = -1;

enum ConnectionFlags : std::uint32_t {
  ConnNone = 0,
  ConnCrouch = 1u << 0,   // must duck to pass
  ConnJump = 1u << 1,     // requires a jump
  ConnOneWay = 1u << 2,   // usable only in the from -> to direction
  ConnLadder = 1u << 3,   // vertical traversal
};

struct Connection {
  AreaId from = kInvalidArea;
  AreaId to = kInvalidArea;
  double heightStep = 0.0;  // floor(to) - floor(from)
  std::uint32_t flags = ConnNone;
  double cost = 1.0;        // multiplier applied by the planner
  Vec2 portal;
  double portalZ = 0;
  bool hasPortal = false;

  [[nodiscard]] bool usableInDirection(AreaId a, AreaId b) const {
    if (from == a && to == b) return true;
    if (flags & ConnOneWay) return false;
    return from == b && to == a;
  }
};

struct NavArea {
  AreaId id = kInvalidArea;
  std::vector<Vec2> polygon;  // convex, counter-clockwise
  std::vector<double> heights;
  double floorZ = 0.0;
  double ceilingZ = 64.0;  // default Source standing clearance
  std::uint32_t flags = 0;

  [[nodiscard]] bool contains(const Vec2& point, double epsilon = 0.0) const;
  [[nodiscard]] Vec2 centroid() const;
  [[nodiscard]] double heightAt(const Vec2& point) const;
  [[nodiscard]] double radius() const;
  // Closest point on the polygon boundary or interior; used to snap a stray pose.
  [[nodiscard]] Vec2 closestPoint(const Vec2& point) const;
};

class NavMesh {
 public:
  [[nodiscard]] bool valid() const { return !areas_.empty(); }
  [[nodiscard]] const std::string& name() const { return name_; }
  void setName(std::string name) { name_ = std::move(name); }

  [[nodiscard]] const std::vector<NavArea>& areas() const { return areas_; }
  [[nodiscard]] const std::vector<Connection>& connections() const { return connections_; }
  [[nodiscard]] const NavArea* area(AreaId id) const;
  [[nodiscard]] std::size_t areaCount() const { return areas_.size(); }
  [[nodiscard]] std::size_t connectionCount() const { return connections_.size(); }

  void clear();
  AreaId addArea(NavArea area);
  void addConnection(Connection connection);

  // Rebuilds the adjacency index; must be called after bulk edits.
  void finalize();

  [[nodiscard]] const std::vector<Connection>& neighbours(AreaId id) const;

  // Nearest area whose polygon contains the point, preferring the smallest
  // height difference when several qualify. Returns kInvalidArea when the point
  // is outside the mesh and no area is within maxDistance.
  AreaId findArea(const Vec2& point, double maxDistance = 128.0) const;

  // Same, but areas whose floor is close to referenceZ win ties. This is what
  // disambiguates a tunnel from the surface that sits above it on a 2-D map.
  AreaId findAreaAtHeight(const Vec2& point, double referenceZ, double maxDistance = 128.0) const;

  // True when the straight segment stays inside walkable areas. Used to smooth
  // paths and to reject shortcuts through walls.
  [[nodiscard]] bool lineOfSight(const Vec2& from, const Vec2& to) const;
  [[nodiscard]] bool surfaceSegment(const Vec2& from, double fromZ, const Vec2& to, double toZ) const;

  // Walkable clearance around a point, in map units; 0 means the point itself
  // is not walkable. Used by the movement controller to keep off walls.
  [[nodiscard]] double clearanceAt(const Vec2& point) const;

  [[nodiscard]] bool walkable(const Vec2& point) const;

  [[nodiscard]] Vec2 boundsMin() const { return boundsMin_; }
  [[nodiscard]] Vec2 boundsMax() const { return boundsMax_; }

 private:
  std::string name_;
  std::vector<NavArea> areas_;
  std::vector<AreaId> allAreaIds_;
  std::unordered_map<std::uint64_t,std::vector<AreaId>> cells_;
  std::vector<Connection> connections_;
  std::vector<std::vector<Connection>> adjacency_;
  Vec2 boundsMin_;
  Vec2 boundsMax_;
  mutable std::vector<Connection> emptyAdjacency_;
};

// Maximum height step the player can walk up without jumping.
inline constexpr double kMaxWalkableStep = 18.0;
// Maximum drop tolerated before the planner treats an edge as dangerous.
inline constexpr double kMaxWalkableDrop = 48.0;

// Links areas whose polygons come within `tolerance` of each other and whose
// height step the player can traverse. Used both by the JSON loader (when a file
// lists no connections) and by the raster builder.
void autoConnectAreas(NavMesh& mesh, double tolerance = 6.0);

}  // namespace cs2n
