// Windows input backend: plain SendInput, no injection, no hooks.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "movement/input.h"

namespace cs2n {

struct WindowsInputConfig {
  double leaseSeconds = 0.45;  // walk key auto-releases if not renewed in time
  bool watchdogEnabled = true;
  unsigned short crouchScan=0x39; // This user's +duck is Space.
};

class WindowsInput final : public IInputBackend {
 public:
  explicit WindowsInput(WindowsInputConfig config = {});
  ~WindowsInput() override;

  WindowsInput(const WindowsInput&) = delete;
  WindowsInput& operator=(const WindowsInput&) = delete;

  void setWalk(bool down) override;
  void setBackward(bool down) override;
  bool ready() const override;
  bool stopRequested() const override;
  void setStrafe(int direction) override;
  void turn(double counts) override;
  void pitch(double counts) override;
  void setFire(bool down) override;
  void setCrouch(bool down) override;
  void reload() override;
  void scope() override;
  void buyLoadout() override;
  void joinTerrorists() override;
  bool loadoutCommandsAvailable() const override {return ready();}
  void selectPrimary() override;
  void releaseAll() override;
  [[nodiscard]] std::string name() const override { return "windows-sendinput"; }

  [[nodiscard]] bool emergencyStopRequested() const;

 private:
  void watchdogLoop();
  bool ensureSafeToInject();
  bool sendKeyScan(unsigned short scan, bool up);
  bool sendMouse(long dx, long dy);

  WindowsInputConfig config_;
  std::mutex mutex_;
  std::atomic<bool> done_{false};
  std::atomic<bool> walkHeld_{false};
  std::atomic<bool> backHeld_{false};
  std::atomic<bool> fireHeld_{false};
  std::atomic<bool> crouchHeld_{false};
  std::atomic<double> lastCrouchLease_{0};
  std::atomic<double> lastFireLease_{0};
  std::atomic<bool> strafeLeft_{false};
  std::atomic<bool> strafeRight_{false};
  std::atomic<double> lastWalkLease_{0.0};
  std::thread watchdog_;
};

// True when CS2 owns the foreground window.
bool cs2ForegroundActive();
// True while the emergency stop key (F10) is physically held.
bool emergencyStopHeld();
// Enables per-monitor DPI awareness; call once before any capture or window work.
void enableProcessDpiAwareness();

}  // namespace cs2n
