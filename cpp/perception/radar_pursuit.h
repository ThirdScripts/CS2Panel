#pragma once
#include <optional>
#include "perception/radar.h"
#include "state/player_state.h"
namespace cs2n {
struct RadarGoal {std::uint32_t id=0;Vec3 position;double seenAt=0;};
// Pixel bearings provide the evidence; entity data resolves distance and floor.
// Never track an unconfirmed entity through a wall after its mark disappears.
class RadarPursuit {
 public:
  std::optional<RadarGoal> update(const PlayerState& p,const RadarHints& radar,double now,bool fighting=false) {
    if(!p.valid||!p.alive){reset();return {};}
    if(goal_&&now-goal_->seenAt>2.5)goal_.reset();
    if(radar.timestamp>lastFrame_&&radar.timestamp<=now+.05&&now-radar.timestamp<.3) {
      lastFrame_=radar.timestamp;
      const Opponent* choice=nullptr;double score=1e9;
      for(double bearing:radar.bearings) {
        if(!std::isfinite(bearing))continue;
        const Opponent* match=nullptr;int matches=0;
        for(const auto& e:p.opponents) {
          if(!e.alive||e.dormant||e.immune||now-e.timestamp>.12||e.timestamp>now+.05||
             (e.id==ignored_&&now<ignoreUntil_))continue;
          const Vec2 delta=e.position.xy()-p.position2();
          if(delta.length()<40||delta.length()>4500)continue;
          if(std::abs(wrapAngle(std::atan2(delta.y,delta.x)-bearing))>4*kPi/180)continue;
          match=&e;++matches;
        }
        if(matches!=1)continue;
        double cost=distance(match->position.xy(),p.position2())-(goal_&&goal_->id==match->id?400:0);
        if(cost<score){score=cost;choice=match;}
      }
      if(choice) {
        if(candidate_==choice->id&&now-candidateAt_<.4)++confirmations_;
        else{candidate_=choice->id;confirmations_=1;}
        candidateAt_=now;
        if(confirmations_>=2) {
          if(!goal_||goal_->id!=choice->id){progressAt_=now;progressPosition_=p.position2();}
          goal_=RadarGoal{choice->id,choice->position,radar.timestamp};
        }
      }else{candidate_=0;confirmations_=0;}
    }
    if(goal_) {
      if(fighting||distance(p.position2(),progressPosition_)>48){progressAt_=now;progressPosition_=p.position2();}
      if(!fighting&&(distance(p.position2(),goal_->position.xy())<80||now-progressAt_>5))reject(now);
    }
    return goal_;
  }
  void reject(double now){if(goal_){ignored_=goal_->id;ignoreUntil_=now+3;}goal_.reset();candidate_=0;confirmations_=0;}
  void reset(){goal_.reset();candidate_=0;confirmations_=0;lastFrame_=-1000;ignored_=0;ignoreUntil_=0;}
 private:
  std::optional<RadarGoal> goal_;
  std::uint32_t candidate_=0,ignored_=0;int confirmations_=0;
  double candidateAt_=0,lastFrame_=-1000,ignoreUntil_=0,progressAt_=0;
  Vec2 progressPosition_;
};
}
