#include <chrono>
#include <iostream>
#include "navigation/navmesh_io.h"
#include "navigation/navigator.h"
#include "state/cs2_memory.h"
#include "movement/move_controller.h"
int main(int argc,char** argv) {
  using namespace cs2n;
  std::string base=argc>1?argv[1]:"assets";
  NavMesh mesh; std::string error;
  if(!NavMeshIo::loadFile(base+"/dust2.nav.json",mesh,error,false)) {std::cerr<<error;return 1;}
  Cs2MemoryProvider memory(base+"/memory.json");auto state=memory.poll();
  if(argc>4) {state=PlayerState{};state->position={std::stod(argv[2]),std::stod(argv[3]),std::stod(argv[4])};}
  if(!state) {std::cerr<<memory.error();return 2;}
  auto start=mesh.findAreaAtHeight(state->position2(),state->position.z,128);
  std::cout<<"areas="<<mesh.areaCount()<<" links="<<mesh.connectionCount()<<" local_area="<<start<<std::endl;
  if(argc>4&&mesh.area(start)) {
    auto a=mesh.area(start);auto nearestPoint=a->closestPoint(state->position2());
    std::cout<<"nearest="<<nearestPoint.x<<","<<nearestPoint.y<<" distance="<<distance(nearestPoint,state->position2())<<" z="<<a->heightAt(nearestPoint)<<std::endl;
    for(auto p:a->polygon)std::cout<<"vertex="<<p.x<<","<<p.y<<std::endl;
  }
  Navigator navigator(&mesh);int successes=0;
  for(int i=0;i<5;++i) {
    Path path;auto begin=std::chrono::steady_clock::now();
    bool ok=navigator.planNewRoute(state->position2(),state->position.z,path,error);
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"route="<<i<<" ok="<<ok<<" points="<<path.size()<<" length="<<path.length()<<" ms="<<ms<<" error="<<error<<std::endl;
    successes+=ok;
    if(argc>4&&ok) {
      MoveController move;move.setMesh(&mesh);
      for(std::size_t j=0;j<std::min(path.size(),std::size_t(5));++j)
        std::cout<<"point "<<path.points[j].position.x<<","<<path.points[j].position.y<<","<<path.points[j].floorZ<<std::endl;
      auto intent=move.evaluate(path,state->position2(),-106.6*kPi/180,state->position.z);
      std::cout<<"forward="<<intent.forward<<" strafe="<<intent.strafe<<" aim="<<intent.aimYaw*180/kPi<<std::endl;
    }
  }
  return successes==5?0:3;
}

