#include "movement/move_controller.h"
#include <cmath>
#include <algorithm>
namespace cs2n {
int MoveController::chooseStrafeSide(const Vec2&,const Vec2&) const {return 0;}
MovementIntent MoveController::evaluate(Path& path,const Vec2& pos,double yaw,double z) {
  MovementIntent intent;
  if(path.empty()) {forwardHeld_=false;strafeHeld_=0;return intent;}
  PathPlanner::advance(path,pos,1.0);
  // A rounded corner may pass beside its waypoint. Retire it only after
  // entering the outgoing segment and checking the shortcut on the mesh.
  while(mesh_ && path.points.size()>2) {
    const Vec2 outgoing=path.points[2].position-path.points[1].position;
    const Vec2 offset=pos-path.points[1].position;
    if(outgoing.x*offset.x+outgoing.y*offset.y<=0 || offset.length()>config_.lookaheadDistance ||
       !mesh_->surfaceSegment(pos,z,path.points[2].position,path.points[2].floorZ))break;
    path.points.erase(path.points.begin()+1);
  }
  path.points.front().floorZ=z;
  if(path.points.size()<2) {forwardHeld_=false;strafeHeld_=0;intent.arrived=true;intent.aimYaw=yaw;return intent;}
  targetIndex_=1;
  Vec2 target=path.points[1].position;
  double budget=config_.lookaheadDistance;
  // Interpolate within segments, so the steering point moves continuously.
  // Probe intermediate points too: a visible endpoint alone can skip a corner.
  for(std::size_t i=1;i<path.points.size() && i<12 && budget>0;++i) {
    const auto& a=path.points[i-1];const auto& b=path.points[i];
    const double length=distance(a.position,b.position);
    if(length<.001)continue;
    const double travel=std::min(length,budget);
    bool blocked=false;
    for(double s=std::min(12.0,travel);;s=std::min(s+12.0,travel)) {
      const double t=s/length;
      const Vec2 candidate=a.position+(b.position-a.position)*t;
      const double height=a.floorZ+(b.floorZ-a.floorZ)*t;
      if(mesh_&&!mesh_->surfaceSegment(pos,z,candidate,height)) {blocked=true;break;}
      target=candidate;targetIndex_=i;
      if(s>=travel)break;
    }
    if(blocked)break;
    budget-=travel;
  }
  intent.targetIndex=targetIndex_;
  if(path.points.size()==2 && distance(pos,path.points.back().position)<config_.arriveRadius) {
    forwardHeld_=false;strafeHeld_=0;intent.arrived=true;intent.aimYaw=yaw;return intent;
  }
  intent.aimYaw=yawFromDirection(target-pos);
  const double delta=wrapAngle(intent.aimYaw-yaw),angle=std::abs(delta)*180/kPi;
  // Hysteresis prevents repeated key transitions near a directional boundary.
  forwardHeld_=angle<(forwardHeld_?65.0:55.0);
  int side=delta>0?-1:1;
  if(!config_.allowStrafe||angle>100||angle<(strafeHeld_==side?19.0:27.0))side=0;
  strafeHeld_=side;
  intent.forward=forwardHeld_;intent.strafe=side;
  Vec2 heading{std::cos(yaw),std::sin(yaw)};
  Vec2 step=heading*(intent.forward?1.0:0.0)+Vec2{heading.y,-heading.x}*double(side);
  if(mesh_ && step.length()>.001) {
    Vec2 probe=pos+step*(24.0/step.length());
    const auto* floor=mesh_->area(mesh_->findAreaAtHeight(probe,z,1));
    if(!floor || !mesh_->surfaceSegment(pos,z,probe,floor->heightAt(probe))) {
      intent.forward=false;intent.strafe=0;
      forwardHeld_=false;strafeHeld_=0;
      // A blocked diagonal does not imply both component directions are
      // blocked. Keep moving along a legal component instead of wedging.
      double best=0;const Vec2 desired=target-pos;
      for(int candidate=0;candidate<2;++candidate) {
        if(candidate==1&&side==0)continue;
        const Vec2 direction=candidate==0?heading:Vec2{heading.y,-heading.x}*double(side);
        const double progress=direction.x*desired.x+direction.y*desired.y;
        if(progress<=best)continue;
        const Vec2 endpoint=pos+direction*24;
        const auto* area=mesh_->area(mesh_->findAreaAtHeight(endpoint,z,1));
        if(!area||!mesh_->surfaceSegment(pos,z,endpoint,area->heightAt(endpoint)))continue;
        best=progress;intent.forward=candidate==0;intent.strafe=candidate==1?side:0;
      }
      forwardHeld_=intent.forward;strafeHeld_=intent.strafe;
    }
  }
  // Source's navmesh is already eroded by the player hull radius.
  return intent;
}
}
