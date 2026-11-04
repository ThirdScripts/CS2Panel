#include "combat/controller.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cs2n {
CombatController::CombatController() {
  LookConfig c;c.mode=AimMode::Spring;c.springNaturalFreq=32;c.maxAngularSpeedDeg=720;c.accelerationDeg=12000;
  yaw_.setConfig(c);pitch_.setConfig(c);
}
void CombatController::reset() {target_=0;acquiredAt_=0;lastSeenAt_=-1000;firing_=false;havePunch_=false;pulseAmmo_=-1;nextPulseAt_=0;yaw_.reset();pitch_.reset();}
void CombatController::setIntensity(double value) {
  requestedIntensity_=std::isfinite(value)?std::clamp(value,0.0,1.5):.5;
  intensity_=requestedIntensity_;
  // Small middle-preset boost; keep the low and high endpoints unchanged.
  if(intensity_<=1)intensity_+=.08*4*intensity_*(1-intensity_);
  auto c=yaw_.config();c.springNaturalFreq=24+11*intensity_;
  c.maxAngularSpeedDeg=390+390*intensity_;c.accelerationDeg=6600+6600*intensity_;
  c.quantizeCounts=!continuous_;
  if(continuous_)c.maxAngularSpeedDeg=std::min(c.maxAngularSpeedDeg,intensity_>1?460.:400.);
  yaw_.setConfig(c);
  if(continuous_)c.maxAngularSpeedDeg=intensity_>1?75:60;
  pitch_.setConfig(c);
}
bool CombatController::automatic(int item) {
  switch(item) {
    case 7:case 8:case 10:case 13:case 14:case 16:case 17:case 19:case 23:case 24:
    case 26:case 28:case 33:case 34:case 39:case 60:case 63:return true;
    default:return false;
  }
}
bool CombatController::firearm(int item) {
  switch(item) {
    case 1:case 2:case 3:case 4:case 7:case 8:case 9:case 10:case 11:case 13:case 14:case 16:
    case 17:case 19:case 23:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 32:
    case 33:case 34:case 35:case 36:case 38:case 39:case 40:case 60:case 61:case 63:case 64:return true;
    default:return false;
  }
}
CombatDecision CombatController::update(const PlayerState& p,const VisibilityMesh& map,double now,double dt) {
  CombatDecision out;
  if(!p.valid||!p.alive||!p.combatDataValid||!map.ready()||!p.weapon.valid||!firearm(p.weapon.item)||
      now-p.timestamp>.15||p.timestamp>now+.1||p.radiansPerMouseCount<=0||!std::isfinite(p.radiansPerMouseCount)) {
    reset();out.status="combat data unavailable";return out;
  }
  if(lastWeapon_!=p.weapon.item) {reset();lastWeapon_=p.weapon.item;lastAmmo_=p.weapon.ammo;}
  if(p.weapon.reloading) {firing_=false;out.status="reloading";return out;}
  if(p.weapon.ammo==0) {out.reload=now-lastReload_>3.2;if(out.reload)lastReload_=now;out.status="reload";firing_=false;return out;}
  const Opponent* best=nullptr;double bestScore=1e9,bestYaw=0,bestPitch=0,bestDistance=0;Vec3 bestPoint;bool bestHead=false;
  for(const auto& enemy:p.opponents) {
    if(!enemy.alive||enemy.dormant||now-enemy.timestamp>.1||enemy.timestamp>now+.1)continue;
    if(enemy.id==skipTarget_&&now<skipUntil_)continue;
    const double selection=double((enemy.id*2654435761u)%1000)/1000.;
    bool head=enemy.headValid&&selection<headChance_;
    Vec3 point=head?enemy.headPoint:enemy.aimPoint;
    if(!map.clearRay(p.eye,point)) {point=enemy.aimPoint;head=false;}
    // Bring cached observations forward by their age and a small input delay.
    // Check the predicted ray as well, so extrapolation cannot aim through cover.
    double lead=std::clamp(now-enemy.timestamp+.025,0.0,.075);
    Vec3 predicted{point.x+enemy.velocity.x*lead,point.y+enemy.velocity.y*lead,point.z+enemy.velocity.z*lead};
    if(map.clearRay(p.eye,predicted))point=predicted;
    Vec3 d{point.x-p.eye.x,point.y-p.eye.y,point.z-p.eye.z};
    double range=d.length();if(range<24||range>3000||!std::isfinite(range))continue;
    double yaw=std::atan2(d.y,d.x),pitch=-std::atan2(d.z,std::hypot(d.x,d.y));
    double yawError=std::abs(wrapAngle(yaw-p.yaw)),pitchError=std::abs(pitch-p.pitch);
    if(yawError>65*kPi/180||pitchError>45*kPi/180)continue;
    if(!map.clearRay(p.eye,point))continue;
    ++out.visibleTargets;
    // Keep a still-visible target unless a new one is significantly closer to
    // the crosshair. This avoids rapid switching between adjacent players.
    double score=std::hypot(yawError,pitchError)+range*.000025-(enemy.id==target_?.18:0)+(enemy.immune?10:0);
    if(score<bestScore) {best=&enemy;bestScore=score;bestYaw=yaw;bestPitch=pitch;bestDistance=range;bestPoint=point;bestHead=head;}
  }
  if(!best) {
    firing_=false;
    // Only retain the last actually seen point, never follow live coordinates
    // through a wall. A confirmed death cancels the memory immediately.
    bool dead=false;for(const auto& e:p.opponents)if(e.id==target_&&!e.alive)dead=true;
    if(target_&&!dead&&now-lastSeenAt_<.8) {
      Vec3 d{lastSeenPoint_.x-p.eye.x,lastSeenPoint_.y-p.eye.y,lastSeenPoint_.z-p.eye.z};
      auto config=yaw_.config();config.radiansPerMouseCount=p.radiansPerMouseCount;yaw_.setConfig(config);
      config=pitch_.config();config.radiansPerMouseCount=p.radiansPerMouseCount;pitch_.setConfig(config);
      yaw_.setMeasuredYaw(p.yaw);pitch_.setMeasuredYaw(p.pitch);
      out.yawCounts=yaw_.update(std::atan2(d.y,d.x),dt);
      out.pitchCounts=pitch_.update(-std::atan2(d.z,std::hypot(d.x,d.y)),dt);
      out.engaged=true;out.target=target_;out.status="checking last seen angle";return out;
    }
    reset();
    const int lowAmmo=p.weapon.item==1?2:(automatic(p.weapon.item)?6:1);
    if(p.weapon.ammo<=lowAmmo&&now-lastThreatAt_>1.0&&now-lastReload_>3.2) {
      out.reload=true;lastReload_=now;out.status="tactical reload";
    }
    return out;
  }
  lastThreatAt_=now;
  if(target_!=best->id) {reset();target_=best->id;acquiredAt_=now;lastProgressAt_=now;lastAmmo_=p.weapon.ammo;}
  if(p.weapon.ammo!=lastAmmo_) {lastProgressAt_=now;lastAmmo_=p.weapon.ammo;}
  if((best->immune&&now-acquiredAt_>1.2)||now-lastProgressAt_>4.0) {
    skipTarget_=target_;skipUntil_=now+2;reset();out.status="repositioning after stalled engagement";return out;
  }
  lastSeenPoint_=bestPoint;lastSeenAt_=now;
  // Compensate the measured punch in the desired angle rather than integrating
  // mouse corrections indefinitely. Invalid/missing recoil never becomes input.
  bool punchOk=false;
  if(p.recoilValid && (p.shotsFired>0||havePunch_) && std::isfinite(p.recoil.x)&&std::isfinite(p.recoil.y)&&
      std::abs(p.recoil.x)<25*kPi/180&&std::abs(p.recoil.y)<25*kPi/180) {
    punchOk=true;
    havePunch_=true;
    bestYaw=wrapAngle(bestYaw-p.recoil.y*2);
    bestPitch=std::clamp(bestPitch-p.recoil.x*2,-89*kPi/180,89*kPi/180);
  }
  if(!punchOk)havePunch_=false;
  auto c=yaw_.config();c.radiansPerMouseCount=p.radiansPerMouseCount;yaw_.setConfig(c);
  c=pitch_.config();c.radiansPerMouseCount=p.radiansPerMouseCount;pitch_.setConfig(c);
  yaw_.setMeasuredYaw(p.yaw);pitch_.setMeasuredYaw(p.pitch);
  // Recoil goes through the same bounded controller as aiming. A separate
  // per-sample impulse bypasses its acceleration limit and saturates native
  // pitch input, causing alternating spikes when recoil samples arrive.
  // Match the visible target's angular velocity, including our own movement.
  // Error-only spring damping otherwise leaves a permanent lag on strafing players.
  const Vec3 d{bestPoint.x-p.eye.x,bestPoint.y-p.eye.y,bestPoint.z-p.eye.z};
  const Vec3 v{best->velocity.x-p.velocity.x,best->velocity.y-p.velocity.y,best->velocity.z-p.velocity.z};
  const double horizontal2=d.x*d.x+d.y*d.y;
  const double horizontal=std::sqrt(horizontal2);
  double yawRate=0,pitchRate=0;
  if(horizontal>24) {
    yawRate=(d.x*v.y-d.y*v.x)/horizontal2;
    const double horizontalRate=(d.x*v.x+d.y*v.y)/horizontal;
    pitchRate=-(horizontal*v.z-d.z*horizontalRate)/(horizontal2+d.z*d.z);
  }
  out.yawCounts=yaw_.update(bestYaw,dt,yawRate);
  out.pitchCounts=pitch_.update(bestPitch,dt,pitchRate);
  out.angularError=std::hypot(wrapAngle(bestYaw-p.yaw),bestPitch-p.pitch)*180/kPi;
  const double tolerance=std::clamp(std::atan2(bestHead?3.5:7.0,bestDistance)*180/kPi,.12,1.5);
  const double elapsed=now-acquiredAt_;
  const double reaction=.075+.125*(1-intensity_)+double(best->id%7)*.006;
  const bool aligned=out.angularError<tolerance && p.velocity.xy().length()<45 && elapsed>reaction;
  // Brake against measured momentum in camera coordinates; release the brake
  // after crossing the low-speed threshold, rather than reversing the run.
  if(p.onGround && p.velocity.xy().length()>45 && elapsed<.25) {
    const double along=p.velocity.x*std::cos(p.yaw)+p.velocity.y*std::sin(p.yaw);
    const double right=p.velocity.x*std::sin(p.yaw)-p.velocity.y*std::cos(p.yaw);
    out.backward=along>30;out.forward=along<-30;
    out.strafe=right>30?-1:(right<-30?1:0);
  }
  // Automatic weapons hold fire; semi-automatic weapons still need clicks.
  const bool precision=p.weapon.item==9||p.weapon.item==40;
  // Keep the stance between bursts instead of tapping Ctrl with each bullet.
  // At close range preserve mobility; avoid ducking behind low cover that
  // would obstruct the shot from the lowered eye position.
  Vec3 lowEye=p.eye;
  lowEye.z=std::min(p.eye.z,p.position.z+46.0);
  out.crouch=elapsed>.18 && bestDistance>300 && p.velocity.xy().length()<45 &&
      map.clearRay(lowEye,bestPoint);
  if(precision&&!p.weapon.scoped) {
    out.scope=now-lastScope_>1;
    if(out.scope)lastScope_=now;
  }
  double period=precision?1.15:.22;
  double on=.035;
  if(p.weapon.item==1) {period=bestDistance>800?.55:.36;on=.025;}
  const bool maintain=firing_&&automatic(p.weapon.item)&&out.angularError<std::max(2.5,tolerance*4)&&p.velocity.xy().length()<45;
  out.fire=!best->immune&&(aligned||maintain)&&p.weapon.ammo>0&&(!precision||p.weapon.scoped)&&
      (automatic(p.weapon.item)||std::fmod(std::max(0.0,elapsed),period)<on);
  // A selected
  // head shot gets an opening tap, then can transition to a controlled spray.
  const bool tap=automatic(p.weapon.item)&&bestHead&&elapsed<.7;
  if(tap) {
    // Bound the click even when ammo telemetry misses or delays a shot.
    if(pulseAmmo_>=0&&(p.weapon.ammo<pulseAmmo_||now-pulseStartedAt_>=.035)) {
      nextPulseAt_=now+.24;pulseAmmo_=-1;
    }
    if(now<nextPulseAt_)out.fire=false;
    if(out.fire&&pulseAmmo_<0){pulseAmmo_=p.weapon.ammo;pulseStartedAt_=now;}
  }
  firing_=out.fire;
  out.engaged=true;out.target=target_;out.headAim=bestHead;
  out.status=out.fire?(out.crouch?"firing crouched":"firing"):(out.backward||out.forward||out.strafe?"counter-strafe":"aiming");
  if(best->immune)out.status="watching spawn protection";
  return out;
}
}
