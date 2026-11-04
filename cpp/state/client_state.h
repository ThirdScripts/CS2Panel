#pragma once
#include <string>
#include "core/json.h"

namespace cs2n {
enum class ClientState {
  GAME_NOT_RUNNING, MAIN_MENU, MATCH_SEARCH, CONNECTING, LOADING,
  IN_MATCH, MATCH_ENDED, DISCONNECTED, Error, UNKNOWN
};
enum class ProcessPresence { Unknown, Running, Absent };
enum class GsiHealth { NoData, Fresh, Stale, Unavailable };
struct ClientStateSnapshot {
  ClientState state = ClientState::UNKNOWN;
  GsiHealth gsi = GsiHealth::NoData;
  std::string reason;
  double receivedAt = -1;
};
const char* clientStateName(ClientState state);
// Only process enumeration, no handles for VM access and no launching CS2.
ProcessPresence detectCs2Presence();

// Deterministic, single-threaded model. Owner must serialize access.
// accept() takes an already authenticated GSI payload, never a partial delta.
class ClientStateModel {
 public:
  explicit ClientStateModel(double staleAfterSeconds = 1.5, std::string expectedSteamId = {});
  bool accept(const JsonValue& payload, double receivedAt);
  ClientStateSnapshot snapshot(double now, ProcessPresence process, bool listenerAvailable = true);
  void reset(); // mandatory when reusing an endpoint for another client/session
 private:
  double timeout_, receivedAt_ = -1;
  bool haveData_ = false;
  std::string expectedSteamId_;
  ClientState state_ = ClientState::UNKNOWN;
  std::string reason_;
};
}
