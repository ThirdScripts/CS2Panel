// Diagnostic memory stand: a process that publishes its own position in memory.
//
// This executable exists solely so the MemoryProvider can be validated against
// a process we own. It opens no other process, injects nothing and knows nothing
// about Counter-Strike 2. It publishes its own process id and the address of a
// shared snapshot on stdout, then advances that snapshot along a predictable
// path so the reader can be checked for correctness and for staleness handling.
//
// Usage:
//   cs2n_mem_probe [--pid-only] [--seconds N]
//
// Output (one line on stdout, then a stream of updates on stderr):
//   PID=<pid> ADDR=0x<hex address> SIZE=<sizeof ProbeSnapshot>

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "state/memory_provider.h"

namespace {

using cs2n::ProbeSnapshot;
using cs2n::kProbeMagic;

volatile std::sig_atomic_t g_stop = 0;

void handleSignal(int) { g_stop = 1; }

}  // namespace

// Placed at global scope so its address is stable and its contents are plain
// data the reader can copy verbatim.
ProbeSnapshot g_snapshot{};

int main(int argc, char** argv) {
  int seconds = 30;
  bool pidOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--pid-only") {
      pidOnly = true;
    } else if (argument == "--seconds" && i + 1 < argc) {
      seconds = std::atoi(argv[++i]);
    }
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  std::memset(&g_snapshot, 0, sizeof(g_snapshot));
  g_snapshot.magic = kProbeMagic;
  g_snapshot.sequence = 0;
  g_snapshot.x = 0.0;
  g_snapshot.y = 0.0;
  g_snapshot.z = 0.0;
  g_snapshot.yaw = 0.0;
  g_snapshot.health = 100;
  std::snprintf(g_snapshot.mapName, sizeof(g_snapshot.mapName), "%s", "de_dust2");

  const auto processId = static_cast<unsigned long>(GetCurrentProcessId());
  const auto address = reinterpret_cast<unsigned long long>(&g_snapshot);

  if (pidOnly) {
    std::printf("%lu\n", processId);
    std::fflush(stdout);
  } else {
    std::printf("PID=%lu ADDR=0x%llX SIZE=%zu\n", processId, address, sizeof(ProbeSnapshot));
    std::fflush(stdout);
  }

  const auto started = std::chrono::steady_clock::now();
  unsigned sequence = 0;
  while (!g_stop) {
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (elapsed > static_cast<double>(seconds)) break;

    ++sequence;
    // Walks a circle so a reader can check both the magnitude and the direction
    // of change, and so "no movement" is unambiguous when it stops advancing.
    const double angle = elapsed * 0.7;
    g_snapshot.x = 512.0 + 128.0 * std::cos(angle);
    g_snapshot.y = 512.0 + 128.0 * std::sin(angle);
    g_snapshot.z = 0.0;
    g_snapshot.yaw = angle;
    g_snapshot.vx = -128.0 * 0.7 * std::sin(angle);
    g_snapshot.vy = 128.0 * 0.7 * std::cos(angle);
    g_snapshot.vz = 0.0;
    g_snapshot.health = 100;
    // The write to sequence is what the reader treats as the freshness signal,
    // so it must happen last.
    g_snapshot.sequence = sequence;

    std::fprintf(stderr, "seq=%u x=%.3f y=%.3f yaw=%.3f\n", sequence, g_snapshot.x, g_snapshot.y, g_snapshot.yaw);
    std::fflush(stderr);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return 0;
}
