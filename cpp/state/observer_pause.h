#pragma once
namespace cs2n {
class ObserverPause {
 public:
  bool update(bool valid,int count,double now) {
    if(valid&&count>0){held_=true;clearAt_=-1;}
    else if(held_) {
      if(!valid||count<0)clearAt_=-1;
      else if(clearAt_<0)clearAt_=now;
      else if(now-clearAt_>=1) {held_=false;clearAt_=-1;}
    }
    return held_;
  }
 private:
  bool held_=false;
  double clearAt_=-1;
};
}
