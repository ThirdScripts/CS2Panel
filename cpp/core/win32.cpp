#include "core/win32.h"

#include <Windows.h>

#include <cstring>

namespace cs2n::win32 {

void* foregroundWindow() { return reinterpret_cast<void*>(GetForegroundWindow()); }

std::uint32_t processIdOf(void* window) {
  DWORD pid = 0;
  if (window == nullptr) return 0;
  GetWindowThreadProcessId(static_cast<HWND>(window), &pid);
  return pid;
}

const char* processName(std::uint32_t processId) {
  static thread_local char name[MAX_PATH] = {};
  name[0] = '\0';
  if (processId == 0) return name;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr) return name;
  wchar_t wide[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  const BOOL ok = QueryFullProcessImageNameW(process, 0, wide, &size);
  CloseHandle(process);
  if (!ok) return name;
  const int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, MAX_PATH, nullptr, nullptr);
  if (written <= 0) {
    name[0] = '\0';
    return name;
  }
  for (char* c = name; *c; ++c) {
    if (*c >= 'A' && *c <= 'Z') *c = static_cast<char>(*c - 'A' + 'a');
  }
  return name;
}

bool isCs2Foreground() {
  void* window = foregroundWindow();
  if (window == nullptr) return false;
  const std::uint32_t pid = processIdOf(window);
  if (pid == 0) return false;
  const char* path = processName(pid);
  const char* slash = std::strrchr(path, '\\');
  return std::strcmp(slash ? slash + 1 : path, "cs2.exe") == 0;
}

bool isEmergencyStopHeld() { return (GetAsyncKeyState(VK_F10) & 0x8000) != 0; }

void enableDpiAwareness() {
  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
  using SetContextFn = BOOL(WINAPI*)(HANDLE);
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    if (auto setContext = reinterpret_cast<SetContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
      if (setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;
    }
  }
  SetProcessDPIAware();
}

}  // namespace cs2n::win32
