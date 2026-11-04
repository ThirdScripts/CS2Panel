#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include "movement/input.h"
#include "movement/windows_input.h"
#include "movement/game_commands.h"
#include <memory>
#include "state/cs2_memory.h"
namespace cs2n {
// Experimental, version-pinned backend. Writes button states and view angles.
class MemoryInput final:public IInputBackend {
 public:
  explicit MemoryInput(ProcessTarget target = {});~MemoryInput() override;
  bool ready() const override;
  bool stopRequested() const override;
  bool loadoutCommandsAvailable() const override {std::lock_guard<std::recursive_mutex> lock(mutex_);return commands_&&commands_->available();}
  std::string loadoutUnavailableReason() const override {std::lock_guard<std::recursive_mutex> lock(mutex_);return commands_?commands_->error():"background command channel unavailable";}
  std::string unavailableReason() const override {auto e=error();return e.empty()?"waiting for valid game memory":e;}
  void setWalk(bool down) override {button("forward",down);}
  void setBackward(bool down) override {button("back",down);}
  void setStrafe(int side) override {button("left",side<0);button("right",side>0);}
  void turn(double counts) override;
  void diagnosticDirectTurn(double counts) {angle(counts,false);}
  void pitch(double counts) override {nativeAngle(counts,true);}
  bool continuousCamera() const override {return true;}
  // Diagnostic native turn axis, [-1,1]. Released by the same stop watchdog.
  void setTurnAxis(double value,bool pitch=false);
  void setFire(bool down) override {button("attack",down);}
  void setCrouch(bool down) override {button("duck",down);}
  void reload() override {pulse("reload");}
  void scope() override {pulse("attack2");}
  void buyLoadout() override {command("cl_dm_buyrandomweapons 0; buy ak47; buy deagle; slot1");}
  void joinTerrorists() override {command("jointeam 2 1");}
  void selectPrimary() override {command("slot1");}
  bool disconnectMatch() override;
  void releaseAll() override;
  std::string name() const override {return "experimental-memory-input";}
  std::string error() const {std::lock_guard<std::recursive_mutex> lock(mutex_);return error_;}
 private:
  void button(const char*,bool);void pulse(const char*);void angle(double,bool);
  void nativeAngle(double counts,bool pitch);
  bool write(std::uintptr_t,const void*,std::size_t);
  void command(const std::string&);
  std::unique_ptr<GameCommands> commands_;
  mutable std::recursive_mutex mutex_;
  mutable Cs2MemoryProvider memory_{"assets/memory.json",false};
  bool bound_=false;
  mutable PlayerState player_;
  mutable double checkedAt_=-1000;
  HANDLE handle_=nullptr;std::uintptr_t base_=0,angles_=0;
  std::map<std::string,std::uintptr_t> buttons_;
  std::map<std::string,bool> held_;
  std::map<std::string,double> pulses_;
  mutable std::string error_;
  double lease_=0;
  double lastTurnAt_=-1,lastPitchAt_=-1;
  std::atomic<bool> done_{false};std::thread watchdog_;
  WindowsInput fallback_;
};
}
