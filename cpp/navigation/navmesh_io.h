// NavMesh (de)serialisation. Format documented in docs/navmesh-format.md.
#pragma once

#include <string>

#include "core/json.h"
#include "navigation/navmesh.h"

namespace cs2n {

class NavMeshIo {
 public:
  // Parses the canonical JSON mesh description. `autoConnect` links areas whose
  // polygons share an edge when the file does not list connections explicitly.
  static bool parse(const JsonValue& root, NavMesh& out, std::string& error, bool autoConnect = true);
  static bool loadFile(const std::string& path, NavMesh& out, std::string& error, bool autoConnect = true);

  static JsonValue serialize(const NavMesh& mesh);
  static bool saveFile(const NavMesh& mesh, const std::string& path, std::string& error);
};

}  // namespace cs2n
