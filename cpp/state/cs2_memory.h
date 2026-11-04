#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include "core/json.h"
#include "state/provider.h"
#include "state/process_target.h"

namespace cs2n {
// Read-only player snapshots; local-only mode skips opponent/weapon scanning.
class Cs2MemoryProvider final : public IPlayerStateProvider {
 public:
  explicit Cs2MemoryProvider(const std::string& profilePath, bool includeCombat = true, ProcessTarget target = {});
  ~Cs2MemoryProvider() override;
  std::optional<PlayerState> poll() override;
  std::string name() const override { return "cs2-local-memory"; }
  std::string diagnostic() const override {return error_;}
  const std::string& error() const { return error_; }
  double sensitivity() const { return sensitivity_; }
  double mouseYawDegrees() const { return profile_["mouseYawDegrees"].asNumber(.022); }
  bool connected() const { return process_ != nullptr; }
  DWORD processId() const {return pid_;}
  ProcessTarget target() const {return target_;}
  std::uintptr_t clientBase() const {return client_;}
 private:
  bool attach();
  std::uintptr_t entity(std::uintptr_t list,std::uint32_t handle,std::size_t stride) const;
  void pollCombat(std::uintptr_t pawn,PlayerState& state);
  void pollObservers(std::uintptr_t pawn,PlayerState& state);
  void detach();
  bool read(std::uintptr_t address, void* out, std::size_t size) const;
  template<class T> bool read(std::uintptr_t address, T& out) const { return read(address, &out, sizeof(T)); }
  std::uintptr_t offset(const char* key) const { return static_cast<std::uintptr_t>(profile_[key].asInt()); }
  JsonValue profile_;
  bool profileValid_ = false;
  ProcessTarget target_;
  bool includeCombat_ = true;
  HANDLE process_ = nullptr;
  std::uintptr_t client_ = 0;
  DWORD pid_ = 0;
  double nextAttach_ = 0;
  double sensitivity_ = 0;
  double lastCombatPoll_ = -1;
  double lastObserversPoll_ = -1;
  int spectators_=0;
  bool observersValid_=false;
  std::size_t entityStride_ = 0;
  std::vector<Opponent> opponents_;
  std::string error_;
};
}
