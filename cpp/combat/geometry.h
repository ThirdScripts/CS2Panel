#pragma once
#include <array>
#include <string>
#include <vector>
#include "core/types.h"
namespace cs2n {
// Immutable BVH over the version-matched map's static collision mesh.
class VisibilityMesh {
 public:
  bool load(const std::string& path,std::string& error);
  bool clearRay(const Vec3& from,const Vec3& to) const;
  bool ready() const {return !nodes_.empty();}
  std::size_t triangles() const {return triangles_.size();}
  void buildForTest(const std::vector<std::array<Vec3,3>>& triangles);
 private:
  struct Triangle {Vec3 a,b,c,center;};
  struct Node {Vec3 low,high;int left=-1,right=-1;std::size_t begin=0,end=0;};
  int build(std::size_t begin,std::size_t end);
  std::vector<Triangle> triangles_;
  std::vector<Node> nodes_;
};
}
