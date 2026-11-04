#pragma once
#include "perception/audio.h"
#include "perception/radar.h"
#include "state/player_state.h"
#include "combat/geometry.h"
#include <unordered_map>
namespace cs2n {
struct Attention {bool active=false;double yaw=0;const char* reason="";};
class Awareness {
 public:
  Attention update(const PlayerState& p,const SoundHint& sound,const RadarHints& radar,bool engaged,double now,const VisibilityMesh* geometry=nullptr) {
    if(!p.valid||!p.alive){reset();return {};}
    const bool hurt=health_>=0&&p.health<health_;health_=p.health;
    bool nearShot=false;double shotBearing=0,closest=100;
    for(const auto& enemy:p.opponents) {
      if(!enemy.shotDataValid||!enemy.alive||enemy.dormant||now-enemy.timestamp>.12)continue;
      auto previous=shots_.find(enemy.id);
      bool fired=previous!=shots_.end()&&enemy.shotsFired>previous->second;
      shots_[enemy.id]=enemy.shotsFired;
      if(!fired)continue;
      const Vec3 origin=enemy.headPoint;
      Vec3 d{p.eye.x-origin.x,p.eye.y-origin.y,p.eye.z-20-origin.z};
      Vec3 direction{std::cos(enemy.shotYaw)*std::cos(enemy.shotPitch),std::sin(enemy.shotYaw)*std::cos(enemy.shotPitch),-std::sin(enemy.shotPitch)};
      double along=d.x*direction.x+d.y*direction.y+d.z*direction.z;
      double miss=std::sqrt(std::max(0.0,d.length()*d.length()-along*along));
      if(along>0&&along<3500&&miss<closest&&geometry&&geometry->clearRay(origin,p.eye)) {
        nearShot=true;closest=miss;shotBearing=std::atan2(origin.y-p.eye.y,origin.x-p.eye.x);
      }
    }
    if(shots_.size()>128)shots_.clear();
    if(engaged){until_=0;lastOwnFire_=now;return {};}
    bool newSound=sound.timestamp>lastSound_&&now-sound.timestamp<.3;
    lastSound_=sound.timestamp;
    const bool soundThreat=newSound&&now-lastOwnFire_>.5&&
      (sound.sharpImpulse||(std::abs(sound.side)>.18&&(p.velocity.xy().length()<30||sound.strength>.04)));
    if(now>=cooldown_&&(hurt||nearShot||soundThreat)) {
      double offset=newSound&&std::abs(sound.side)>.18?(sound.side>0?-1:1)*kPi/2:kPi;
      yaw_=nearShot?shotBearing:wrapAngle(p.yaw+offset);until_=now+.85;cooldown_=now+1.6;
      reason_=nearShot?"checking nearby shot":hurt?"checking incoming damage":"checking sound direction";
    } else if(now>=cooldown_&&now-radar.timestamp<.3&&!radar.bearings.empty()) {
      double best=10;
      for(double bearing:radar.bearings){double d=std::abs(wrapAngle(bearing-p.yaw));if(d<best){best=d;yaw_=bearing;}}
      if(best>20*kPi/180){until_=now+.8;cooldown_=now+2;reason_="checking radar contact";}
    }
    if(now<until_)return {true,yaw_,reason_};
    return {};
  }
  void reset(){health_=-1;until_=0;cooldown_=0;lastSound_=-1000;shots_.clear();}
 private:
  int health_=-1;double until_=0,cooldown_=0,yaw_=0,lastSound_=-1000,lastOwnFire_=-1000;const char* reason_="";
  std::unordered_map<std::uint32_t,int> shots_;
};
}
