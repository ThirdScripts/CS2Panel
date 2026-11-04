#include "state/client_state.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace cs2n {
const char* clientStateName(ClientState state) {
  switch(state) {
#define NAME(value) case ClientState::value: return #value
    NAME(GAME_NOT_RUNNING); NAME(MAIN_MENU); NAME(MATCH_SEARCH); NAME(CONNECTING);
    NAME(LOADING); NAME(IN_MATCH); NAME(MATCH_ENDED); NAME(DISCONNECTED); case ClientState::Error: return "ERROR"; NAME(UNKNOWN);
#undef NAME
  }
  return "UNKNOWN";
}
ProcessPresence detectCs2Presence() {
  const HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if(handle == INVALID_HANDLE_VALUE) return ProcessPresence::Unknown;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if(!Process32FirstW(handle, &entry)) {
    const DWORD error = GetLastError(); CloseHandle(handle);
    return error == ERROR_NO_MORE_FILES ? ProcessPresence::Absent : ProcessPresence::Unknown;
  }
  do {
    if(_wcsicmp(entry.szExeFile, L"cs2.exe") == 0) {
      CloseHandle(handle); return ProcessPresence::Running;
    }
  } while(Process32NextW(handle, &entry));
  const DWORD error = GetLastError(); CloseHandle(handle);
  return error == ERROR_NO_MORE_FILES ? ProcessPresence::Absent : ProcessPresence::Unknown;
}
ClientStateModel::ClientStateModel(double timeout, std::string expectedSteamId)
    : timeout_(timeout), expectedSteamId_(std::move(expectedSteamId)) {
  if(!std::isfinite(timeout_) || timeout_ <= 0) throw std::invalid_argument("Invalid GSI state timeout");
}
void ClientStateModel::reset() {
  haveData_ = false; receivedAt_ = -1; state_ = ClientState::UNKNOWN; reason_.clear();
}
bool ClientStateModel::accept(const JsonValue& payload, double receivedAt) {
  if(!std::isfinite(receivedAt) || receivedAt < 0 || (haveData_ && receivedAt < receivedAt_)) return false;
  const auto& provider = payload["provider"];
  if(provider["appid"].asNumber(-1) != 730 || provider["steamid"].asString().empty()) return false;
  if(!expectedSteamId_.empty() && provider["steamid"].asString() != expectedSteamId_) return false;
  ClientState state = ClientState::UNKNOWN;
  std::string reason = "GSI map.name absent/empty; menu/search/loading not distinguishable";
  const auto& map = payload["map"];
  const auto phase = map["phase"].asString();
  if(!map["name"].asString().empty()) {
    reason = "GSI map.phase missing or unsupported";
    if(phase == "gameover") {
      state = ClientState::MATCH_ENDED; reason = "GSI map.phase=gameover";
    } else if(phase == "live" || phase == "warmup" || phase == "intermission") {
      state = ClientState::IN_MATCH; reason = "GSI match context: " + phase;
    }
  }
  // Commit only validated source identity. Missing/unknown fields replace old state.
  state_ = state; reason_ = std::move(reason); receivedAt_ = receivedAt; haveData_ = true;
  return true;
}
ClientStateSnapshot ClientStateModel::snapshot(double now, ProcessPresence process, bool listenerAvailable) {
  if(process == ProcessPresence::Absent) {
    reset(); return {ClientState::GAME_NOT_RUNNING, GsiHealth::NoData, "OS process enumeration: no cs2.exe", -1};
  }
  if(!listenerAvailable) {
    reset(); return {ClientState::UNKNOWN, GsiHealth::Unavailable, "GSI listener unavailable; game error not established", -1};
  }
  if(!haveData_) return {ClientState::UNKNOWN, GsiHealth::NoData, "No authenticated GSI snapshot", -1};
  if(!std::isfinite(now) || now < receivedAt_ || now - receivedAt_ >= timeout_) {
    // Do not turn a transport timeout into a claim that the game disconnected.
    return {ClientState::UNKNOWN, GsiHealth::Stale, "GSI timeout or invalid monotonic clock", receivedAt_};
  }
  return {state_, GsiHealth::Fresh, reason_, receivedAt_};
}
}
