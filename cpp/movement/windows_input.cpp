#include "movement/windows_input.h"

#include <Windows.h>

#include <chrono>
#include <cmath>

#include "core/log.h"
#include "core/types.h"
#include "core/win32.h"

namespace cs2n {
namespace {

constexpr unsigned short kScanW = 0x11;
constexpr unsigned short kScanA = 0x1E;
constexpr unsigned short kScanD = 0x20;

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct InputEvent {
  INPUT native{};

  static InputEvent key(unsigned short scan, bool up) {
    InputEvent event;
    event.native.type = INPUT_KEYBOARD;
    event.native.ki.wScan = scan;
    event.native.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return event;
  }



  static InputEvent mouse(long dx, long dy) {
    InputEvent event;
    event.native.type = INPUT_MOUSE;
    event.native.mi.dx = dx;
    event.native.mi.dy = dy;
    event.native.mi.dwFlags = MOUSEEVENTF_MOVE;
    return event;
  }
};

bool sendOne(const INPUT& event) {
  return SendInput(1, const_cast<INPUT*>(&event), sizeof(INPUT)) == 1;
}

}  // namespace

bool cs2ForegroundActive() { return win32::isCs2Foreground(); }
bool emergencyStopHeld() { return win32::isEmergencyStopHeld(); }
void enableProcessDpiAwareness() { win32::enableDpiAwareness(); }

WindowsInput::WindowsInput(WindowsInputConfig config) : config_(config) {
  if (config_.watchdogEnabled) watchdog_ = std::thread([this] { watchdogLoop(); });
}

WindowsInput::~WindowsInput() {
  done_ = true;
  if (watchdog_.joinable()) watchdog_.join();
  releaseAll();
}

bool WindowsInput::ensureSafeToInject() {
  if (emergencyStopHeld()) return false;
  return win32::isCs2Foreground();
}

bool WindowsInput::sendKeyScan(unsigned short scan, bool up) {
  return sendOne(InputEvent::key(scan, up).native);
}

bool WindowsInput::sendMouse(long dx, long dy) {
  if (dx == 0 && dy == 0) return true;
  return sendOne(InputEvent::mouse(dx, dy).native);
}

void WindowsInput::setWalk(bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(down && !ensureSafeToInject()) down=false;
  if (down == walkHeld_.load()) {
    if (down) lastWalkLease_.store(monotonicSeconds());
    return;
  }
  if (down && !ensureSafeToInject()) return;
  if (sendKeyScan(kScanW, !down)) {
    walkHeld_.store(down);
    if (down) lastWalkLease_.store(monotonicSeconds());
  }
}

bool WindowsInput::ready() const { return win32::isCs2Foreground(); }
bool WindowsInput::stopRequested() const { return emergencyStopHeld() || (ready() && (GetAsyncKeyState(VK_ESCAPE)&0x8000)); }
void WindowsInput::setBackward(bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(down && !ensureSafeToInject()) down=false;
  if(down!=backHeld_.load() && sendKeyScan(0x1F,!down)) backHeld_=down;
  if(down) lastWalkLease_=monotonicSeconds();
}

void WindowsInput::setStrafe(int direction) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(direction && !ensureSafeToInject()) direction=0;
  if(direction) lastWalkLease_=monotonicSeconds();
  const bool wantLeft = direction < 0;
  const bool wantRight = direction > 0;
  if (wantLeft != strafeLeft_.load()) {
    if (wantLeft && !ensureSafeToInject()) return;
    if (sendKeyScan(kScanA, !wantLeft)) strafeLeft_.store(wantLeft);
  }
  if (wantRight != strafeRight_.load()) {
    if (wantRight && !ensureSafeToInject()) return;
    if (sendKeyScan(kScanD, !wantRight)) strafeRight_.store(wantRight);
  }
}

void WindowsInput::turn(double counts) {
  if (!std::isfinite(counts)) return;
  const long value = static_cast<long>(std::lround(counts));
  if (value == 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ensureSafeToInject()) return;
  sendMouse(-value, 0); // Positive world yaw is counterclockwise.
}



void WindowsInput::releaseAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(crouchHeld_.exchange(false)) sendKeyScan(config_.crouchScan,true);
  if (walkHeld_.exchange(false)) sendKeyScan(kScanW, true);
  if(backHeld_.exchange(false)) sendKeyScan(0x1F,true);
  if (strafeLeft_.exchange(false)) sendKeyScan(kScanA, true);
  if (strafeRight_.exchange(false)) sendKeyScan(kScanD, true);
  if(fireHeld_.exchange(false)) {INPUT event{};event.type=INPUT_MOUSE;event.mi.dwFlags=MOUSEEVENTF_LEFTUP;sendOne(event);}
}

bool WindowsInput::emergencyStopRequested() const {
  return emergencyStopHeld() || !win32::isCs2Foreground();
}

void WindowsInput::watchdogLoop() {
  while (!done_.load()) {
    Sleep(40);
    const bool mustRelease = emergencyStopHeld() || !win32::isCs2Foreground();
    const double lease = lastWalkLease_.load();
    const bool leaseExpired = (walkHeld_.load() || backHeld_.load() || strafeLeft_.load() || strafeRight_.load()) && (monotonicSeconds() - lease > config_.leaseSeconds);
    if (mustRelease || leaseExpired) releaseAll();
    if(fireHeld_ && monotonicSeconds()-lastFireLease_>.15) setFire(false);
    if(crouchHeld_ && monotonicSeconds()-lastCrouchLease_>.15) setCrouch(false);
  }
}

void WindowsInput::pitch(double counts) {
  if(!std::isfinite(counts)||std::abs(counts)>2000)return;
  std::lock_guard<std::mutex> lock(mutex_);
  if(ensureSafeToInject())sendMouse(0,static_cast<long>(std::lround(counts)));
}
void WindowsInput::setFire(bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(down&&!ensureSafeToInject())down=false;
  if(down)lastFireLease_=monotonicSeconds();
  if(down==fireHeld_)return;
  INPUT event{};event.type=INPUT_MOUSE;event.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
  if(sendOne(event))fireHeld_=down;
}
void WindowsInput::reload() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(ensureSafeToInject()) {sendKeyScan(0x13,false);sendKeyScan(0x13,true);}
}
void WindowsInput::setCrouch(bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(down&&!ensureSafeToInject())down=false;
  if(down)lastCrouchLease_=monotonicSeconds();
  if(down!=crouchHeld_.load() && sendKeyScan(config_.crouchScan,!down))crouchHeld_=down;
}
void WindowsInput::buyLoadout() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(ensureSafeToInject()){sendKeyScan(0x41,false);sendKeyScan(0x41,true);} // F7
}
void WindowsInput::joinTerrorists() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(ensureSafeToInject()){sendKeyScan(0x43,false);sendKeyScan(0x43,true);} // F9
}
void WindowsInput::selectPrimary() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(ensureSafeToInject()){sendKeyScan(0x02,false);sendKeyScan(0x02,true);}
}
void WindowsInput::scope() {
  std::lock_guard<std::mutex> lock(mutex_);
  if(!ensureSafeToInject())return;
  INPUT events[2]{};events[0].type=events[1].type=INPUT_MOUSE;
  events[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN;events[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
  SendInput(2,events,sizeof(INPUT));
}

}  // namespace cs2n
