#include "navigation/navmesh_io.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cs2n {
namespace {

bool parsePolygon(const JsonValue& node, std::vector<Vec2>& out, std::string& error) {
  if (!node.isArray() || node.size() < 3) {
    error = "area polygon needs at least 3 points";
    return false;
  }
  out.clear();
  for (const JsonValue& point : node.items()) {
    if (!point.isArray() || point.size() != 2 || !point.at(0).isNumber() || !point.at(1).isNumber()) {
      error = "polygon point must be [x, y]";
      return false;
    }
    const Vec2 value{point.at(0).asNumber(), point.at(1).asNumber()};
    if (!finiteNumber(value.x) || !finiteNumber(value.y)) {
      error = "polygon point is not finite";
      return false;
    }
    out.push_back(value);
  }
  // Reject self-degenerate polygons that would break containment tests.
  double signedArea = 0.0;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const Vec2& a = out[i];
    const Vec2& b = out[(i + 1) % out.size()];
    signedArea += a.cross(b);
  }
  if (std::fabs(signedArea) < 1e-6) {
    error = "polygon has zero area";
    return false;
  }
  return true;
}

}  // namespace

void autoConnectAreas(NavMesh& mesh, double tolerance) {
  const std::vector<NavArea>& areas = mesh.areas();
  for (std::size_t i = 0; i < areas.size(); ++i) {
    for (std::size_t j = i + 1; j < areas.size(); ++j) {
      const NavArea& a = areas[i];
      const NavArea& b = areas[j];
      const double heightStep = b.floorZ - a.floorZ;
      if (heightStep > kMaxWalkableStep || heightStep < -kMaxWalkableDrop) continue;

      // Cheap bounding-box rejection before the exact test.
      Vec2 aMin = a.polygon[0], aMax = a.polygon[0], bMin = b.polygon[0], bMax = b.polygon[0];
      for (const Vec2& p : a.polygon) {
        aMin.x = std::min(aMin.x, p.x); aMax.x = std::max(aMax.x, p.x);
        aMin.y = std::min(aMin.y, p.y); aMax.y = std::max(aMax.y, p.y);
      }
      for (const Vec2& p : b.polygon) {
        bMin.x = std::min(bMin.x, p.x); bMax.x = std::max(bMax.x, p.x);
        bMin.y = std::min(bMin.y, p.y); bMax.y = std::max(bMax.y, p.y);
      }
      if (aMin.x - tolerance > bMax.x || bMin.x - tolerance > aMax.x ||
          aMin.y - tolerance > bMax.y || bMin.y - tolerance > aMax.y) {
        continue;
      }

      bool touching = false;
      double minSeparation = tolerance;
      for (const Vec2& point : a.polygon) {
        const Vec2 closest = b.closestPoint(point);
        const double separation = distance(closest, point);
        if (separation <= tolerance) {
          touching = true;
          break;
        }
        minSeparation = std::min(minSeparation, separation);
      }
      if (!touching) {
        for (const Vec2& point : b.polygon) {
          const Vec2 closest = a.closestPoint(point);
          if (distance(closest, point) <= tolerance) {
            touching = true;
            break;
          }
        }
      }
      (void)minSeparation;
      if (!touching) continue;

      Connection connection;
      connection.from = a.id;
      connection.to = b.id;
      connection.heightStep = heightStep;
      connection.flags = ConnNone;
      if (heightStep < -kMaxWalkableStep) connection.flags |= ConnJump;  // a drop to survive
      mesh.addConnection(connection);
    }
  }
}

bool NavMeshIo::parse(const JsonValue& root, NavMesh& out, std::string& error, bool autoConnect) {
  if (!root.isObject()) {
    error = "navmesh root must be an object";
    return false;
  }
  const int format = root["format"].asInt(0);
  if (format != 1) {
    error = "unsupported navmesh format " + std::to_string(format);
    return false;
  }
  out.clear();
  out.setName(root["name"].asString());

  const JsonValue& areas = root["areas"];
  if (!areas.isArray() || areas.size() == 0) {
    error = "navmesh has no areas";
    return false;
  }
  for (const JsonValue& node : areas.items()) {
    NavArea area;
    std::vector<Vec2> polygon;
    if (!parsePolygon(node["poly"], polygon, error)) return false;
    area.polygon = std::move(polygon);
    for(const auto& h:node["heights"].items()) area.heights.push_back(h.asNumber());
    area.floorZ = node["floor"].asNumber(0.0);
    area.ceilingZ = node["ceiling"].asNumber(area.floorZ + 72.0);
    area.flags = static_cast<std::uint32_t>(node["flags"].asInt(0));
    if (area.ceilingZ <= area.floorZ) {
      error = "area ceiling must be above floor";
      return false;
    }
    out.addArea(std::move(area));
  }

  const JsonValue& connections = root["connections"];
  if (connections.isArray() && connections.size() > 0) {
    for (const JsonValue& node : connections.items()) {
      Connection connection;
      connection.from = static_cast<AreaId>(node["from"].asInt(-1));
      connection.to = static_cast<AreaId>(node["to"].asInt(-1));
      connection.heightStep = node["step"].asNumber(0.0);
      connection.flags = static_cast<std::uint32_t>(node["flags"].asInt(0));
      connection.cost = node["cost"].asNumber(1.0);
      if(node["portal"].size()==2) {
        connection.hasPortal=true;
        connection.portal={node["portal"].at(0).asNumber(),node["portal"].at(1).asNumber()};
        connection.portalZ=node["portalZ"].asNumber();
      }
      if (connection.from < 0 || connection.to < 0 ||
          static_cast<std::size_t>(connection.from) >= out.areaCount() ||
          static_cast<std::size_t>(connection.to) >= out.areaCount()) {
        error = "connection references an unknown area";
        return false;
      }
      if (connection.cost <= 0.0) connection.cost = 1.0;
      out.addConnection(connection);
    }
  } else if (autoConnect) {
    autoConnectAreas(out);
  }

  out.finalize();
  if (out.connectionCount() == 0) {
    error = "navmesh has no connections between areas";
    return false;
  }
  return true;
}

bool NavMeshIo::loadFile(const std::string& path, NavMesh& out, std::string& error, bool autoConnect) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  JsonValue root;
  if (!JsonValue::parse(buffer.str(), root, error)) {
    error = path + ": " + error;
    return false;
  }
  return parse(root, out, error, autoConnect);
}

JsonValue NavMeshIo::serialize(const NavMesh& mesh) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeNumber(1));
  root.set("name", JsonValue::makeString(mesh.name()));

  JsonValue areas = JsonValue::makeArray();
  for (const NavArea& area : mesh.areas()) {
    JsonValue node = JsonValue::makeObject();
    JsonValue polygon = JsonValue::makeArray();
    for (const Vec2& point : area.polygon) {
      JsonValue p = JsonValue::makeArray();
      p.append(JsonValue::makeNumber(point.x));
      p.append(JsonValue::makeNumber(point.y));
      polygon.append(std::move(p));
    }
    node.set("poly", std::move(polygon));
    node.set("floor", JsonValue::makeNumber(area.floorZ));
    node.set("ceiling", JsonValue::makeNumber(area.ceilingZ));
    node.set("flags", JsonValue::makeNumber(static_cast<double>(area.flags)));
    areas.append(std::move(node));
  }
  root.set("areas", std::move(areas));

  JsonValue connections = JsonValue::makeArray();
  for (const Connection& connection : mesh.connections()) {
    JsonValue node = JsonValue::makeObject();
    node.set("from", JsonValue::makeNumber(connection.from));
    node.set("to", JsonValue::makeNumber(connection.to));
    node.set("step", JsonValue::makeNumber(connection.heightStep));
    node.set("flags", JsonValue::makeNumber(static_cast<double>(connection.flags)));
    node.set("cost", JsonValue::makeNumber(connection.cost));
    connections.append(std::move(node));
  }
  root.set("connections", std::move(connections));
  return root;
}

bool NavMeshIo::saveFile(const NavMesh& mesh, const std::string& path, std::string& error) {
  std::error_code ec;
  const std::filesystem::path target(path);
  if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "cannot write " + path;
    return false;
  }
  file << serialize(mesh).dump(2) << "\n";
  return true;
}

}  // namespace cs2n
