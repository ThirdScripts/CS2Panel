#include "combat/geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
namespace cs2n {
namespace {
Vec3 subtract(Vec3 a,Vec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
double axis(Vec3 a,int n) {return n==0?a.x:n==1?a.y:a.z;}
bool boxHit(Vec3 from,Vec3 d,Vec3 low,Vec3 high) {
  double near=.001,far=.999;
  for(int n=0;n<3;++n) {
    double p=axis(from,n),v=axis(d,n),lo=axis(low,n),hi=axis(high,n);
    if(std::abs(v)<1e-12) {if(p<lo||p>hi)return false;continue;}
    double a=(lo-p)/v,b=(hi-p)/v;if(a>b)std::swap(a,b);
    near=std::max(near,a);far=std::min(far,b);if(near>far)return false;
  }
  return true;
}
bool triangleHit(Vec3 from,Vec3 d,Vec3 a,Vec3 b,Vec3 c) {
  Vec3 e1=subtract(b,a),e2=subtract(c,a),h=cross(d,e2);
  double determinant=dot(e1,h);if(std::abs(determinant)<1e-9)return false;
  double inv=1/determinant;Vec3 s=subtract(from,a);double u=inv*dot(s,h);
  if(u<0||u>1)return false;
  Vec3 q=cross(s,e1);double v=inv*dot(d,q);
  if(v<0||u+v>1)return false;
  double t=inv*dot(e2,q);return t>.001&&t<.999;
}
}
void VisibilityMesh::buildForTest(const std::vector<std::array<Vec3,3>>& input) {
  triangles_.clear();nodes_.clear();
  for(const auto& t:input) triangles_.push_back({t[0],t[1],t[2],{(t[0].x+t[1].x+t[2].x)/3,(t[0].y+t[1].y+t[2].y)/3,(t[0].z+t[1].z+t[2].z)/3}});
  if(!triangles_.empty())build(0,triangles_.size());
}
int VisibilityMesh::build(std::size_t begin,std::size_t end) {
  Node n;n.begin=begin;n.end=end;n.low={1e30,1e30,1e30};n.high={-1e30,-1e30,-1e30};
  for(auto i=begin;i<end;++i)for(auto v:{triangles_[i].a,triangles_[i].b,triangles_[i].c}) {
    n.low.x=std::min(n.low.x,v.x);n.low.y=std::min(n.low.y,v.y);n.low.z=std::min(n.low.z,v.z);
    n.high.x=std::max(n.high.x,v.x);n.high.y=std::max(n.high.y,v.y);n.high.z=std::max(n.high.z,v.z);
  }
  int index=static_cast<int>(nodes_.size());nodes_.push_back(n);
  if(end-begin>8) {
    Vec3 extent=subtract(n.high,n.low);int dim=extent.x>extent.y?0:1;if(extent.z>axis(extent,dim))dim=2;
    auto mid=begin+(end-begin)/2;
    std::nth_element(triangles_.begin()+begin,triangles_.begin()+mid,triangles_.begin()+end,[&](const Triangle& a,const Triangle& b){return axis(a.center,dim)<axis(b.center,dim);});
    int left=build(begin,mid),right=build(mid,end);nodes_[index].left=left;nodes_[index].right=right;
  }
  return index;
}
bool VisibilityMesh::load(const std::string& path,std::string& error) {
  std::ifstream file(path,std::ios::binary);char magic[4]{};std::uint32_t version=0,vertices=0,triangles=0;
  file.read(magic,4);file.read(reinterpret_cast<char*>(&version),4);file.read(reinterpret_cast<char*>(&vertices),4);file.read(reinterpret_cast<char*>(&triangles),4);
  if(!file||std::string(magic,4)!="AWMH"||version!=1||vertices>2000000||triangles>4000000) {error="Invalid visibility mesh header";return false;}
  std::vector<Vec3> points(vertices);
  for(auto& p:points) {float v[3];file.read(reinterpret_cast<char*>(v),12);p={v[0],v[1],v[2]};if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){error="Nonfinite mesh vertex";return false;}}
  std::vector<std::array<Vec3,3>> input;input.reserve(triangles);
  for(std::uint32_t i=0;i<triangles;++i) {std::uint32_t ids[3]{};file.read(reinterpret_cast<char*>(ids),12);if(!file||ids[0]>=vertices||ids[1]>=vertices||ids[2]>=vertices){error="Invalid mesh index";return false;}input.push_back({points[ids[0]],points[ids[1]],points[ids[2]]});}
  buildForTest(input);return ready();
}
bool VisibilityMesh::clearRay(const Vec3& from,const Vec3& to) const {
  if(!ready())return false;
  Vec3 d=subtract(to,from);if(d.length()<1)return false;
  std::vector<int> stack{0};
  while(!stack.empty()) {
    int i=stack.back();stack.pop_back();const auto& n=nodes_[i];
    if(!boxHit(from,d,n.low,n.high))continue;
    if(n.left>=0) {stack.push_back(n.left);stack.push_back(n.right);continue;}
    for(auto j=n.begin;j<n.end;++j) {const auto& t=triangles_[j];if(triangleHit(from,d,t.a,t.b,t.c))return false;}
  }
  return true;
}
}
