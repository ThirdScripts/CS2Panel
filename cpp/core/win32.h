// Small Win32 helpers shared by capture and input: focus detection and DPI.
#pragma once

#include <cstdint>

namespace cs2n::win32 {

// Handle of the current foreground window, or 0.
void* foregroundWindow();

// Process id owning a window, or 0.
std::uint32_t processIdOf(void* window);

// Executable name (lower case) of a process, empty when unavailable.
const char* processName(std::uint32_t processId);

// True when cs2.exe owns the foreground window.
bool isCs2Foreground();

// True when the emergency stop key (F10) is currently held.
bool isEmergencyStopHeld();

// Per-monitor DPI awareness; call once at start-up.
void enableDpiAwareness();

}  // namespace cs2n::win32
