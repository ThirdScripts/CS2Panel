#include "movement/move_controller.h"
#include "movement/look_controller.h"
#include "tests/framework.h"
#include "tests/mesh_fixture.h"
#include <cmath>
using namespace cs2n;
CS2N_TEST(Move_ReportedDust2ShortCornerCanReturnToRoute) {
  NavMesh mesh;NavArea area;
  area.polygon={{429.5,1641},{488,1648.5},{488,1747.5},{429.5,1747.5}};
  area.floorZ=6;mesh.addArea(area);mesh.finalize();
  MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{427.4,1750},4},{{458,1660},6}};
  auto intent=move.evaluate(route,{427.4,1750},-73*kPi/180,4);
  CHECK_TRUE(intent.forward);
}
CS2N_TEST(Move_ReentersMeshFromSmallPhysicsFringe) {
  auto mesh=test::makeRoomMesh();MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{100,-2.5},0},{{100,200},0}};
  auto intent=move.evaluate(route,{100,-2.5},kPi/2,0);
  CHECK_TRUE(intent.forward);
  CHECK_FALSE(mesh.surfaceSegment({100,-2.5},0,{100,-30},0));
  CHECK_FALSE(mesh.surfaceSegment({100,-20},0,{100,200},0));
}
CS2N_TEST(Move_ClimbsSeveralSmallStepsWithinProbe) {
  NavMesh mesh;
  for(int i=0;i<8;++i) {
    NavArea area;double x=i*10.;
    area.polygon={{x,0},{x+10,0},{x+10,100},{x,100}};
    area.floorZ=i*12.;area.ceilingZ=area.floorZ+72;
    mesh.addArea(area);
  }
  autoConnectAreas(mesh,1);mesh.finalize();
  MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{5,50},0},{{75,50},84}};
  auto intent=move.evaluate(route,{5,50},0,0);
  CHECK_TRUE(intent.forward);
}
CS2N_TEST(Move_UsesDiagonalAndDoesNotChatterAtOldThreshold) {
  auto mesh=test::makeRoomMesh();MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{100,100}},{{300,100}}};
  for(double angle:{34.,36.,34.,36.}) {
    auto intent=move.evaluate(route,{100,100},angle*kPi/180);
    CHECK_TRUE(intent.forward);CHECK_TRUE(intent.strafe==1);
  }
}
CS2N_TEST(Move_DoesNotStrafeOutsideMesh) {
  auto mesh=test::makeRoomMesh();MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{100,2}},{{300,2}}};
  auto intent=move.evaluate(route,{100,2},30*kPi/180);
  CHECK_TRUE(intent.forward);CHECK_TRUE(intent.strafe==0);
}
CS2N_TEST(Move_LookaheadVariesContinuouslyAcrossWaypoint) {
  auto mesh=test::makeRoomMesh();MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{50,100}},{{150,100}},{{150,300}}};
  double last=0;bool first=true;
  for(double x=50;x<=145;x+=1) {
    auto intent=move.evaluate(route,{x,100},0);
    if(!first)CHECK_TRUE(std::abs(wrapAngle(intent.aimYaw-last))<.04);
    last=intent.aimYaw;first=false;
  }
}
CS2N_TEST(Move_ClosedLoopRoundsCornerAndArrives) {
  auto mesh=test::makeRoomMesh(8,8);MoveController move;move.setMesh(&mesh);
  Path route;route.points={{{100,100}},{{400,100}},{{400,650}}};
  LookConfig config;config.mode=AimMode::Spring;config.springNaturalFreq=7;
  LookController look(config);Vec2 pos{100,100};double yaw=0;bool arrived=false;
  for(int tick=0;tick<144*12;++tick) {
    auto intent=move.evaluate(route,pos,yaw);if(intent.arrived){arrived=true;break;}
    look.setMeasuredYaw(yaw);yaw=wrapAngle(yaw+look.update(intent.aimYaw,1./144)*config.radiansPerMouseCount);
    Vec2 forward{std::cos(yaw),std::sin(yaw)};
    Vec2 velocity=forward*(intent.forward?1.:0.)+Vec2{forward.y,-forward.x}*double(intent.strafe);
    if(velocity.length()>.001)pos=pos+velocity*(200./144/velocity.length());
    CHECK_TRUE(pos.x>0&&pos.y>0&&pos.x<800&&pos.y<800);
  }
  CHECK_TRUE(arrived);CHECK_TRUE(distance(pos,{400,650})<15);
}
