#pragma once
#include <cstdint>
#include "core/types.h"
namespace cs2n {
struct Opponent {
  std::uint32_t id=0;
  Vec3 position;
  Vec3 aimPoint;
  Vec3 headPoint;
  bool headValid=false;
  Vec3 velocity;
  bool alive=false;
  bool immune=false;
  bool dormant=true;
  bool spottedByLocal=false;
  double timestamp=0;
  bool shotDataValid=false;
  int shotsFired=0;
  double shotYaw=0,shotPitch=0;
};
struct WeaponState {
  int item=0;
  int ammo=-1;
  bool scoped=false;
  bool valid=false;
  bool reloading=false;
};
}
