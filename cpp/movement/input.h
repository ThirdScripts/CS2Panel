// Abstraction over "how the intent reaches the game".
//
// The engine only ever talks to this interface, which is what makes the whole
// navigation stack testable without Counter-Strike 2 running: the simulator and
// the unit tests substitute their own implementation.
#pragma once

#include <string>

namespace cs2n {

class IInputBackend {
 public:
  virtual ~IInputBackend() = default;

  // Straight movement key.
  virtual void setWalk(bool down) = 0;
  virtual void setBackward(bool) {}
  virtual bool ready() const { return true; }
  virtual bool stopRequested() const { return false; }
  virtual bool loadoutCommandsAvailable() const { return true; }
  virtual std::string loadoutUnavailableReason() const {return "activate CS2 to select T / buy AK-47";}
  virtual std::string unavailableReason() const { return "waiting for CS2 focus"; }
  // Lateral movement: -1 left, 0 none, +1 right.
  virtual void setStrafe(int direction) = 0;
  // Relative mouse motion in counts. Sign convention: positive turns right.
  virtual void turn(double counts) = 0;
  virtual bool continuousCamera() const {return false;}
  virtual void pitch(double) {}
  virtual void setFire(bool) {}
  virtual void setCrouch(bool) {}
  virtual void reload() {}
  virtual void scope() {}
  virtual void buyLoadout() {}
  virtual void joinTerrorists() {}
  virtual void selectPrimary() {}
  virtual bool disconnectMatch() {return false;}


  // Releases every key this backend pressed. Safe to call repeatedly.
  virtual void releaseAll() = 0;

  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace cs2n
