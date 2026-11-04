#include <limits>
#include "combat/controller.h"
#include "tests/framework.h"
#include "perception/awareness.h"
#include "perception/radar_pursuit.h"
using namespace cs2n;
namespace {
PlayerState radarScene(double now) {
  PlayerState p;p.valid=true;p.alive=true;p.timestamp=now;
  Opponent e;e.id=42;e.alive=true;e.dormant=false;e.spottedByLocal=true;
  e.position={500,0,64};e.timestamp=now;p.opponents.push_back(e);return p;
}
}
CS2N_TEST(RadarPursuit_RequiresTwoDistinctConfirmedFrames) {
  RadarPursuit pursuit;auto p=radarScene(1);
  CHECK_FALSE(pursuit.update(p,{1,{0}},1).has_value());
  CHECK_FALSE(pursuit.update(p,{1,{0}},1.05).has_value());
  p=radarScene(1.1);auto goal=pursuit.update(p,{1.1,{0}},1.1);
  CHECK_TRUE(goal.has_value());if(goal){CHECK_EQ(goal->id,42u);CHECK_NEAR(goal->position.z,64,.001);}
}
CS2N_TEST(RadarPursuit_DoesNotFollowHiddenLiveCoordinates) {
  RadarPursuit pursuit;pursuit.update(radarScene(1),{1,{0}},1);
  pursuit.update(radarScene(1.1),{1.1,{0}},1.1);
  auto p=radarScene(1.2);p.opponents[0].position.x=900;
  auto goal=pursuit.update(p,{1.2,{}},1.2);
  CHECK_TRUE(goal.has_value());if(goal)CHECK_NEAR(goal->position.x,500,.001);
  CHECK_FALSE(pursuit.update(radarScene(4),{4,{}},4).has_value());
}
CS2N_TEST(RadarPursuit_RejectsAmbiguityButAcceptsRadarBeforeLocalSpot) {
  RadarPursuit pursuit;auto p=radarScene(1);auto other=p.opponents[0];other.id=43;other.position.x=800;p.opponents.push_back(other);
  pursuit.update(p,{1,{0}},1);for(auto& e:p.opponents)e.timestamp=1.1;
  CHECK_FALSE(pursuit.update(p,{1.1,{0}},1.1).has_value());
  pursuit.reset();p=radarScene(2);p.opponents[0].spottedByLocal=false;
  pursuit.update(p,{2,{0}},2);p.opponents[0].timestamp=2.1;
  CHECK_TRUE(pursuit.update(p,{2.1,{0}},2.1).has_value());
}
CS2N_TEST(RadarPursuit_RefreshesMovingContactAndStopsAtArrival) {
  RadarPursuit pursuit;pursuit.update(radarScene(1),{1,{0}},1);
  pursuit.update(radarScene(1.1),{1.1,{0}},1.1);
  auto p=radarScene(1.2);p.opponents[0].position={650,0,80};
  auto goal=pursuit.update(p,{1.2,{0}},1.2);CHECK_TRUE(goal.has_value());
  if(goal)CHECK_NEAR(goal->position.x,650,.001);
  p.position={620,0,80};p.opponents[0].timestamp=1.3;
  CHECK_FALSE(pursuit.update(p,{1.3,{0}},1.3).has_value());
}
CS2N_TEST(RadarPursuit_AbandonsVisibleButStalledGoal) {
  RadarPursuit pursuit;
  for(int i=0;i<51;++i){double now=1+i*.1;pursuit.update(radarScene(now),{now,{0}},now);}
  CHECK_FALSE(pursuit.update(radarScene(6.3),{6.3,{0}},6.3).has_value());
}
namespace {
PlayerState scene(double now);
VisibilityMesh emptyRoom() {
  VisibilityMesh m;
  m.buildForTest({{{{-200,-200,-100},{200,-200,-100},{200,200,-100}}}});
  return m;
}
CS2N_TEST(Awareness_DamageChecksRearAndExpires) {
  Awareness a;auto p=scene(1);a.update(p,{}, {},false,1);
  p.health=80;auto result=a.update(p,{}, {},false,1.1);
  CHECK_TRUE(result.active);CHECK_NEAR(std::abs(result.yaw),kPi,.001);
  CHECK_FALSE(a.update(p,{}, {},false,2.1).active);
}
CS2N_TEST(Awareness_NearShotFromBehindUsesObservedDirection) {
  auto map=emptyRoom();Awareness a;auto p=scene(1);
  p.opponents[0].headPoint={-500,0,40};p.opponents[0].shotDataValid=true;p.opponents[0].shotYaw=0;
  a.update(p,{}, {},false,1,&map);
  p.opponents[0].shotsFired=1;p.opponents[0].timestamp=1.05;
  auto result=a.update(p,{}, {},false,1.05,&map);
  CHECK_TRUE(result.active);CHECK_NEAR(std::abs(result.yaw),kPi,.001);
}
CS2N_TEST(Awareness_StaleRadarAndOwnFireDoNotInterrupt) {
  Awareness a;auto p=scene(10);RadarHints radar{8,{kPi}};
  CHECK_FALSE(a.update(p,{},radar,false,10).active);
  a.update(p,{}, {},true,10);
  CHECK_FALSE(a.update(p,{10.1,1,.2},{},false,10.1).active);
}
CS2N_TEST(Awareness_CenteredSharpSoundChecksRearBeforeDamage) {
  Awareness a;auto p=scene(10);
  auto attention=a.update(p,{10,0,.1,true},{},false,10);
  CHECK_TRUE(attention.active);CHECK_NEAR(std::abs(attention.yaw),kPi,.001);
  CHECK_FALSE(a.update(p,{10,0,.1,true},{},false,11).active);
}
CS2N_TEST(Combat_DistantAutomaticKeepsControlledFire) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.opponents[0].aimPoint.x=1800;
  c.update(p,map,1,.007);
  for(int i=0;i<60;++i){double now=1.3+i*.01;p.timestamp=p.opponents[0].timestamp=now;
    p.weapon.ammo=30-i/10;CHECK_TRUE(c.update(p,map,now,.01).fire);}
}
CS2N_TEST(Combat_HeadSelectionApproximatelySixtyPercent) {
  auto map=emptyRoom();int heads=0;
  for(unsigned id=1;id<=1000;++id){CombatController c;auto p=scene(1);p.opponents[0].id=id;
    p.opponents[0].headValid=true;p.opponents[0].headPoint={500,0,65};heads+=c.update(p,map,1,.007).headAim;}
  CHECK_TRUE(heads>570&&heads<630);
}
PlayerState scene(double now) {
  PlayerState p;p.valid=true;p.alive=true;p.combatDataValid=true;p.timestamp=now;
  p.eye={0,0,60};p.radiansPerMouseCount=.0008;p.weapon={7,30,false,true};
  Opponent e;e.id=123;e.alive=true;e.dormant=false;e.aimPoint={500,0,60};e.timestamp=now;p.opponents.push_back(e);
  return p;
}
}
CS2N_TEST(Combat_AcquiresAndFiresWhenAligned) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);
  auto first=c.update(p,map,1,.007);CHECK_TRUE(first.engaged);CHECK_FALSE(first.fire);
  p.timestamp=1.23;p.opponents[0].timestamp=1.23;
  auto next=c.update(p,map,1.23,.007);CHECK_TRUE(next.engaged);CHECK_TRUE(next.fire);
}
CS2N_TEST(Combat_RejectsDeadDormantAndStale) {
  auto map=emptyRoom();CombatController c;
  auto p=scene(1);p.opponents[0].alive=false;CHECK_FALSE(c.update(p,map,1,.007).engaged);
  p=scene(1);p.opponents[0].dormant=true;CHECK_FALSE(c.update(p,map,1,.007).engaged);
  p=scene(1);p.opponents[0].timestamp=.5;CHECK_FALSE(c.update(p,map,1,.007).engaged);
  p=scene(1);p.alive=false;CHECK_FALSE(c.update(p,map,1,.007).fire);
}
CS2N_TEST(Combat_DoesNotAimThroughWall) {
  VisibilityMesh wall;wall.buildForTest({{{{200,-100,0},{200,100,0},{200,0,200}}}});
  CHECK_FALSE(wall.clearRay({0,0,60},{500,0,60}));
  CHECK_TRUE(wall.clearRay({0,300,60},{500,300,60}));
  CombatController c;CHECK_FALSE(c.update(scene(1),wall,1,.007).engaged);
}
CS2N_TEST(Combat_StopsFiringOnTargetLossAndWhileMoving) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);c.update(p,map,1,.007);
  p=scene(1.23);p.velocity={200,0,0};CHECK_FALSE(c.update(p,map,1.23,.007).fire);
  p=scene(1.24);p.opponents.clear();auto out=c.update(p,map,1.24,.007);CHECK_FALSE(out.fire);CHECK_TRUE(out.engaged);
  p=scene(2.1);p.opponents.clear();CHECK_FALSE(c.update(p,map,2.1,.007).engaged);
}
CS2N_TEST(Combat_SpawnProtectionHasBoundedWait) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.opponents[0].immune=true;
  auto out=c.update(p,map,1,.007);CHECK_TRUE(out.engaged);CHECK_FALSE(out.fire);
  p=scene(2.3);p.opponents[0].immune=true;out=c.update(p,map,2.3,.007);CHECK_FALSE(out.engaged);CHECK_FALSE(out.fire);
}
CS2N_TEST(Combat_AutomaticFireHasNoScheduledBurstGaps) {
  auto map=emptyRoom();CombatController c;c.update(scene(1),map,1,.007);
  for(double now:{1.23,1.3,1.4,1.55,1.7}) {
    auto p=scene(now);CHECK_TRUE(c.update(p,map,now,.007).fire);
  }
}
CS2N_TEST(Combat_ReloadsLowMagazineOnlyAfterThreatClears) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.weapon.ammo=3;
  CHECK_FALSE(c.update(p,map,1,.007).reload);
  p=scene(2.2);p.weapon.ammo=3;p.opponents.clear();CHECK_TRUE(c.update(p,map,2.2,.007).reload);
  p=scene(2.3);p.weapon.reloading=true;auto out=c.update(p,map,2.3,.007);CHECK_FALSE(out.fire);CHECK_FALSE(out.reload);
}
CS2N_TEST(Combat_ReloadAndNonFirearm) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.weapon.ammo=0;
  CHECK_TRUE(c.update(p,map,1,.007).reload);CHECK_FALSE(c.update(p,map,1.01,.007).reload);
  p=scene(1);p.weapon.item=42;CHECK_FALSE(c.update(p,map,1,.007).engaged);
}
CS2N_TEST(Combat_PitchAndYawTrackCorrectDirections) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.opponents[0].aimPoint={500,100,100};
  auto out=c.update(p,map,1,.02);CHECK_TRUE(out.engaged);CHECK_GT(out.yawCounts,0.0);CHECK_TRUE(out.pitchCounts<0);
}
CS2N_TEST(Combat_SniperScopesBeforeFiring) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.weapon.item=40;
  auto result=c.update(p,map,1,.007);CHECK_TRUE(result.scope);CHECK_FALSE(result.fire);
  p.timestamp=2.16;p.opponents[0].timestamp=2.16;p.weapon.scoped=true;
  result=c.update(p,map,2.16,.007);CHECK_FALSE(result.scope);CHECK_TRUE(result.fire);
}
CS2N_TEST(Combat_CrouchHoldsBetweenBurstsAndReleasesOnLoss) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);
  CHECK_FALSE(c.update(p,map,1,.007).crouch);
  for(double now:{1.23,1.35,1.48}) {
    p=scene(now);CHECK_TRUE(c.update(p,map,now,.007).crouch);
  }
  p=scene(1.49);p.opponents.clear();CHECK_FALSE(c.update(p,map,1.49,.007).crouch);
}
CS2N_TEST(Combat_CrouchAvoidsCloseRangeMovementAndLowCover) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);c.update(p,map,1,.007);
  p=scene(1.3);p.opponents[0].aimPoint.x=200;CHECK_FALSE(c.update(p,map,1.3,.007).crouch);
  p=scene(1.4);p.velocity={100,0,0};CHECK_FALSE(c.update(p,map,1.4,.007).crouch);
  VisibilityMesh cover;cover.buildForTest({{{{200,-100,0},{200,100,0},{200,0,110}}}});
  // Top at centre is 110; use a broad rectangular low wall ending at z=55.
  cover.buildForTest({{{{200,-100,0},{200,100,0},{200,100,55}}},
                     {{{200,-100,0},{200,100,55},{200,-100,55}}}});
  p=scene(1.5);auto out=c.update(p,cover,1.5,.007);
  CHECK_TRUE(out.engaged);CHECK_FALSE(out.crouch);
}
CS2N_TEST(Combat_CounterStrafeBrakesAndReleases) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.onGround=true;p.velocity={200,100,0};
  auto out=c.update(p,map,1,.007);CHECK_TRUE(out.backward);CHECK_TRUE(out.strafe==1);CHECK_FALSE(out.fire);
  p=scene(1.1);p.onGround=true;p.velocity={20,0,0};out=c.update(p,map,1.1,.007);
  CHECK_FALSE(out.backward);CHECK_FALSE(out.forward);CHECK_TRUE(out.strafe==0);
}
CS2N_TEST(Combat_RecoilCorrectsBothAxesWithoutStaleAccumulation) {
  auto map=emptyRoom();CombatController c;auto p=scene(1);p.recoilValid=true;p.shotsFired=3;
  p.recoil={-2*kPi/180,1*kPi/180};auto out=c.update(p,map,1,.02);
  CHECK_GT(out.pitchCounts,0.0);CHECK_LT(out.yawCounts,0.0);CHECK_FALSE(out.fire);
  c.reset();p=scene(2);out=c.update(p,map,2,.02);
  CHECK_EQ(out.pitchCounts,0.0);CHECK_EQ(out.yawCounts,0.0);
}
CS2N_TEST(Combat_PrefersHeadAndTracksMovingTarget) {
  auto map=emptyRoom();CombatController c;c.setHeadChance(1);auto p=scene(1);double error=100;
  for(int tick=0;tick<144;++tick) {
    double now=1+tick/144.;p.timestamp=now;p.opponents[0].timestamp=now;
    p.opponents[0].headValid=true;p.opponents[0].headPoint={500,100+100*(now-1),65};p.opponents[0].velocity={0,100,0};
    auto out=c.update(p,map,now,1./144);CHECK_TRUE(out.headAim);
    p.yaw=wrapAngle(p.yaw+out.yawCounts*p.radiansPerMouseCount);
    p.pitch+=out.pitchCounts*p.radiansPerMouseCount;error=out.angularError;
  }
  CHECK_LT(error,1.0);
}
CS2N_TEST(Combat_FollowsStrafingTargetAcrossAllPresets) {
  auto map=emptyRoom();
  for(double intensity:{0.,.5,1.,1.5}) {
    CombatController c;c.setIntensity(intensity);c.setContinuousCamera(true);
    auto p=scene(1);int shots=0;const double dt=1./144,rate=.6;
    for(int tick=0;tick<288;++tick) {
      double now=1+tick*dt,a=(now-1)*rate;
      p.timestamp=p.opponents[0].timestamp=now;
      p.opponents[0].aimPoint={500*std::cos(a),500*std::sin(a),60};
      p.opponents[0].velocity={-500*rate*std::sin(a),500*rate*std::cos(a),0};
      auto out=c.update(p,map,now,dt);
      if(tick>144){CHECK_LT(out.angularError,.2);shots+=out.fire;}
      p.yaw=wrapAngle(p.yaw+out.yawCounts*p.radiansPerMouseCount);
      p.pitch+=out.pitchCounts*p.radiansPerMouseCount;
    }
    CHECK_GT(shots,100);
  }
}
CS2N_TEST(Combat_BackgroundRecoilHasNoUnboundedImpulseAndSettles) {
  auto map=emptyRoom();CombatController c;c.setContinuousCamera(true);
  auto p=scene(1);p.recoilValid=true;const double dt=1./144;
  double previousSpeed=0;
  for(int tick=0;tick<400;++tick) {
    double now=1+tick*dt;p.timestamp=p.opponents[0].timestamp=now;
    p.shotsFired=tick<250?tick/14:0;
    double punch=tick<140?-.4*(tick/14):tick<250?-4.:std::min(0.,-4.+(tick-250)*.08);
    p.recoil={punch*kPi/180,0};
    auto out=c.update(p,map,now,dt);
    const double speed=out.pitchCounts*p.radiansPerMouseCount/dt*180/kPi;
    CHECK_TRUE(std::abs(speed)<=60.00001);
    CHECK_TRUE(std::abs(speed-previousSpeed)<=11000*dt);
    previousSpeed=speed;
    p.pitch+=out.pitchCounts*p.radiansPerMouseCount;
    CHECK_TRUE(p.pitch>=-.002); // never kicks above the stationary target
  }
  CHECK_NEAR(p.pitch,0,.005);
}

CS2N_TEST(Combat_IntensityRejectsNonFiniteAndBoundsMonster) {
 auto map=emptyRoom();
 for(double value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-100.,100.,1.5}){
  CombatController c;c.setIntensity(value);c.setContinuousCamera(true);
  auto p=scene(1);
  for(int i=0;i<100;++i){const double now=1+i/144.;p.timestamp=p.opponents[0].timestamp=now;auto out=c.update(p,map,now,1./144);
   CHECK_TRUE(std::isfinite(out.yawCounts));CHECK_TRUE(std::isfinite(out.pitchCounts));
  }
 }
}
