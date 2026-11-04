#include "state/memory_provider.h"

#include <Windows.h>

#include <chrono>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/types.h"

namespace cs2n {
namespace {

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

MemoryProvider::MemoryProvider(MemoryProviderConfig config) : config_(config) {
  if (config_.processId == 0 || config_.address == 0) {
    lastError_ = "process id and address must both be set";
    Logger::instance().warn("Memory stand: " + lastError_);
    return;
  }
  // PROCESS_VM_READ only: no write, no rights that would permit injection.
  HANDLE handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, config_.processId);
  if (handle == nullptr) {
    lastError_ = "OpenProcess failed, error " + std::to_string(GetLastError());
    Logger::instance().warn("Memory stand: " + lastError_);
    return;
  }
  process_ = handle;
  lastError_.clear();
  Logger::instance().info(format("Memory stand: attached to pid %u at 0x%llX",
                                 config_.processId,
                                 static_cast<unsigned long long>(config_.address)));
}

MemoryProvider::~MemoryProvider() {
  if (process_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(process_));
    process_ = nullptr;
  }
}

std::optional<PlayerState> MemoryProvider::validate(const ProbeSnapshot& snapshot, double now,
                                                    double staleAfterSeconds, std::uint32_t maxSequenceJump,
                                                    const std::string& expectedMap, std::string& error) {
  if (snapshot.magic != kProbeMagic) {
    error = "probe magic mismatch";
    return std::nullopt;
  }
  if (!finiteNumber(snapshot.x) || !finiteNumber(snapshot.y) || !finiteNumber(snapshot.z) ||
      !finiteNumber(snapshot.yaw) || !finiteNumber(snapshot.vx) || !finiteNumber(snapshot.vy) ||
      !finiteNumber(snapshot.vz)) {
    error = "probe reported non-finite values";
    return std::nullopt;
  }
  if (snapshot.health > 100) {
    error = "probe health out of range";
    return std::nullopt;
  }
  if (!expectedMap.empty()) {
    char mapName[sizeof(ProbeSnapshot::mapName) + 1] = {};
    std::memcpy(mapName, snapshot.mapName, sizeof(snapshot.mapName));
    if (expectedMap != mapName) {
      error = "probe map is '" + std::string(mapName) + "', expected '" + expectedMap + "'";
      return std::nullopt;
    }
  }
  PlayerState state;
  state.valid = true;
  state.alive = snapshot.health > 0;
  state.position = Vec3{snapshot.x, snapshot.y, snapshot.z};
  state.velocity = Vec3{snapshot.vx, snapshot.vy, snapshot.vz};
  state.yaw = snapshot.yaw;
  state.timestamp = now;
  char mapName[sizeof(ProbeSnapshot::mapName) + 1] = {};
  std::memcpy(mapName, snapshot.mapName, sizeof(snapshot.mapName));
  state.mapName = mapName;
  (void)staleAfterSeconds;
  (void)maxSequenceJump;
  error.clear();
  return state;
}

std::optional<PlayerState> MemoryProvider::poll() {
  if (process_ == nullptr) return std::nullopt;

  ProbeSnapshot snapshot{};
  SIZE_T read = 0;
  if (!ReadProcessMemory(static_cast<HANDLE>(process_), reinterpret_cast<LPCVOID>(config_.address), &snapshot,
                         sizeof(snapshot), &read) ||
      read != sizeof(snapshot)) {
    lastError_ = "ReadProcessMemory failed, error " + std::to_string(GetLastError());
    return std::nullopt;
  }

  const double now = monotonicSeconds();
  if (snapshot.sequence != lastSequence_) {
    if (config_.maxSequenceJump != 0 && lastSequence_ != 0) {
      const std::uint32_t jump = snapshot.sequence - lastSequence_;
      if (jump > config_.maxSequenceJump) {
        lastError_ = "probe sequence jump too large";
        return std::nullopt;
      }
    }
    lastSequence_ = snapshot.sequence;
    lastAdvance_ = now;
  } else if (now - lastAdvance_ > config_.staleAfterSeconds) {
    lastError_ = "probe stopped advancing";
    return std::nullopt;
  }

  std::string error;
  auto state = validate(snapshot, now, config_.staleAfterSeconds, config_.maxSequenceJump,
                        config_.expectedMap, error);
  if (!state) lastError_ = error;
  return state;
}

}  // namespace cs2n
