// Snapshot of everything the navigation engine needs to know about the player.
#pragma once

#include <string>
#include <vector>
#include "combat/types.h"

#include "core/types.h"

namespace cs2n {

struct PlayerState {
  bool valid = false;        // provider produced a usable snapshot
  bool alive = false;        // health > 0 and the player is in the world
  Vec3 position;             // world coordinates
  Vec3 velocity;             // world units per second
  double yaw = 0.0;          // view yaw, radians (0 = +X, CCW)
  double pitch = 0.0;        // view pitch, radians, kept for completeness
  std::string mapName;       // e.g. "de_dust2"
  double timestamp = 0.0;    // provider clock, seconds, when the data was captured
  double radiansPerMouseCount = 0.0;
  Vec3 eye;
  WeaponState weapon;
  std::vector<Opponent> opponents;
  bool combatDataValid=false;
  bool observersValid=false;
  int spectators=0; // confirmed human first-/third-person observers of this pawn
  int team=0,shotsFired=0;
  int health=100;
  bool onGround=false,recoilValid=false;
  Vec2 recoil; // pitch/yaw in radians, before weapon recoil scale

  [[nodiscard]] Vec2 position2() const { return position.xy(); }
};

}  // namespace cs2n
