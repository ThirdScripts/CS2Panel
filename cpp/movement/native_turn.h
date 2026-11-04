#pragma once
#include <algorithm>
#include <cmath>
#include "core/types.h"
namespace cs2n {
// Calibrated against version 2000933: analog 0.05 ~= 20 deg/s,
// analog 0.25 ~= 100 deg/s. Closed-loop yaw corrects gain variation.
inline double nativeTurnAxis(double counts,double radiansPerCount,double dt,double degreesPerSecond=400) {
  if(!std::isfinite(counts)||!std::isfinite(radiansPerCount)||!std::isfinite(dt)||
     !std::isfinite(degreesPerSecond)||radiansPerCount<=0||dt<=0||degreesPerSecond<=0)return 0;
  return std::clamp(counts*radiansPerCount*180/kPi/dt/degreesPerSecond,-1.0,1.0);
}
}
