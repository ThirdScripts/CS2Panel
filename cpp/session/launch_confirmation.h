#pragma once
#include "state/client_state.h"
namespace cs2n::session {
inline bool confirmsLaunchedMatch(const ClientStateSnapshot& state, double previousPacket) {
  return state.gsi == GsiHealth::Fresh && state.state == ClientState::IN_MATCH &&
         state.receivedAt >= 0 && state.receivedAt > previousPacket;
}
}
