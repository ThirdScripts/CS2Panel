#include "state/provider.h"

#include "core/types.h"

namespace cs2n {

std::optional<PlayerState> CompositeProvider::poll() {
  PlayerState result;
  if (live_) {
    const auto live = live_->poll();
    if (!live || !live->valid) return std::nullopt;
    result = *live;
    if(!live->alive) return result;
  }
  if (position_) {
    const auto position = position_->poll();
    if (!position || !position->valid) return std::nullopt;
    if (result.valid && position->timestamp > 0.0 && result.timestamp > 0.0 &&
        result.timestamp - position->timestamp > maxPositionAge_) {
      return std::nullopt;
    }
    result.alive = result.valid ? result.alive && position->alive : position->alive;
    result.position = position->position;
    result.velocity = position->velocity;
    result.radiansPerMouseCount=position->radiansPerMouseCount;
    result.yaw = position->yaw;
    result.pitch = position->pitch;
    result.eye=position->eye;
    result.weapon=position->weapon;
    result.opponents=position->opponents;
    result.combatDataValid=position->combatDataValid;
    result.observersValid=position->observersValid;result.spectators=position->spectators;
    result.team=position->team;result.onGround=position->onGround;
    result.health=position->health;
    result.recoil=position->recoil;result.recoilValid=position->recoilValid;result.shotsFired=position->shotsFired;
    result.timestamp = position->timestamp;
    result.valid = true;
    if (!finiteNumber(result.position.x) || !finiteNumber(result.position.y) ||
        !finiteNumber(result.position.z) || !finiteNumber(result.yaw)) {
      return std::nullopt;
    }
  }
  return result;
}

std::string CompositeProvider::name() const {
  std::string out;
  if (live_) out += live_->name();
  if (position_) {
    if (!out.empty()) out += "+";
    out += position_->name();
  }
  return out.empty() ? "none" : out;
}

}  // namespace cs2n
