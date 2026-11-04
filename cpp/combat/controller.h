#pragma once
#include <string>
#include "combat/geometry.h"
#include "movement/look_controller.h"
#include "state/player_state.h"
namespace cs2n {
struct CombatDecision {
  bool engaged=false,fire=false,reload=false,scope=false,crouch=false;
  bool forward=false,backward=false,headAim=false;
  int strafe=0;
  double yawCounts=0,pitchCounts=0,angularError=0;
  std::uint32_t target=0;
  int visibleTargets=0;
  std::string status="patrol";
};
class CombatController {
 public:
  CombatController();
  CombatDecision update(const PlayerState& player,const VisibilityMesh& geometry,double now,double dt);
  void reset();
  static bool firearm(int item);
  static bool automatic(int item);
  void setIntensity(double value);
  void setContinuousCamera(bool enabled){continuous_=enabled;setIntensity(requestedIntensity_);}
  void setHeadChance(double value) {headChance_=value;}
 private:
  LookController yaw_,pitch_;
  std::uint32_t target_=0;
  double acquiredAt_=0,lastReload_=-1000,lastScope_=-1000;
  Vec3 lastSeenPoint_;
  double lastSeenAt_=-1000,lastThreatAt_=-1000,lastProgressAt_=0,skipUntil_=0;
  std::uint32_t skipTarget_=0;
  int lastAmmo_=-1,lastWeapon_=0;
  bool firing_=false;
  double intensity_=0.5,headChance_=0.6;
  bool continuous_=false;
  double requestedIntensity_=.5;
  bool havePunch_=false;
  int pulseAmmo_=-1;
  double nextPulseAt_=0,pulseStartedAt_=0;
};
}
