// Diagnostic memory stand.
//
// This provider exists ONLY to validate the IPlayerStateProvider contract and the
// Windows read path against a process we own (tools/mem_probe). The caller supplies
// the exact process id and the exact address; nothing is scanned, no offsets are
// discovered, nothing is injected and no game process is ever opened by this code.
// Production positioning uses the radar vision provider instead.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "state/player_state.h"
#include "state/provider.h"

namespace cs2n {

// Mirrors the layout published by tools/mem_probe. Packed so the on-wire layout
// matches what the probe writes, independent of compiler defaults.
#pragma pack(push, 1)
struct ProbeSnapshot {
  std::uint32_t magic;      // kProbeMagic
  std::uint32_t sequence;   // incremented by the probe on every update
  double x;
  double y;
  double z;
  double yaw;
  double vx;
  double vy;
  double vz;
  std::uint32_t health;     // 0 == dead
  char mapName[32];         // NUL terminated ASCII
};
#pragma pack(pop)

inline constexpr std::uint32_t kProbeMagic = 0x4353324E;  // "CS2N"

struct MemoryProviderConfig {
  std::uint32_t processId = 0;
  std::uintptr_t address = 0;        // address of the ProbeSnapshot in the target
  std::string expectedMap;           // optional guard, e.g. "de_dust2"
  double staleAfterSeconds = 0.5;    // fail closed if the probe stops advancing
  std::uint32_t maxSequenceJump = 0; // 0 disables the sanity check
};

class MemoryProvider final : public IPlayerStateProvider {
 public:
  explicit MemoryProvider(MemoryProviderConfig config);
  ~MemoryProvider() override;

  MemoryProvider(const MemoryProvider&) = delete;
  MemoryProvider& operator=(const MemoryProvider&) = delete;

  [[nodiscard]] bool attached() const { return process_ != nullptr; }
  [[nodiscard]] std::string lastError() const { return lastError_; }

  std::optional<PlayerState> poll() override;
  [[nodiscard]] std::string name() const override { return "memory-probe"; }

  // Exposed for unit tests so the read/validate path can be exercised without
  // spawning a process.
  static std::optional<PlayerState> validate(const ProbeSnapshot& snapshot, double now,
                                             double staleAfterSeconds, std::uint32_t maxSequenceJump,
                                             const std::string& expectedMap, std::string& error);

 private:
  MemoryProviderConfig config_;
  void* process_ = nullptr;  // HANDLE, kept opaque
  std::uint32_t lastSequence_ = 0;
  double lastAdvance_ = 0.0;
  std::string lastError_ = "not attached";
};

}  // namespace cs2n
