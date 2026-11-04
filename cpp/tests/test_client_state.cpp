#include "state/client_state.h"
#include "tests/framework.h"
#include <limits>
#include <stdexcept>
using namespace cs2n;
namespace {
JsonValue payload(const std::string& phase = "live") {
  JsonValue value; std::string error;
  CHECK_TRUE(JsonValue::parse(R"({"provider":{"appid":730,"steamid":"owner"},"map":{"name":"de_inferno"},"round":{"phase":"over"},"player":{"activity":"playing"}})", value, error));
  JsonValue map = JsonValue::makeObject();
  map.set("name", JsonValue::makeString("de_inferno"));
  map.set("phase", JsonValue::makeString(phase));
  value.set("map", map); return value;
}
ClientStateSnapshot state(ClientStateModel& model, double now) {
  return model.snapshot(now, ProcessPresence::Running);
}
}
CS2N_TEST(ClientState_NoDataIsUnknownNotMenu) {
  ClientStateModel model;
  CHECK_TRUE(state(model, 0).state == ClientState::UNKNOWN);
  CHECK_TRUE(state(model, 10000).gsi == GsiHealth::NoData);
}
CS2N_TEST(ClientState_TransitionsThroughMatchAndExpiry) {
  ClientStateModel model(2);
  for(const auto* phase : {"warmup", "live", "intermission"}) {
    CHECK_TRUE(model.accept(payload(phase), 10));
    CHECK_TRUE(state(model, 10).state == ClientState::IN_MATCH);
  }
  CHECK_TRUE(model.accept(payload("gameover"), 11));
  CHECK_TRUE(state(model, 11).state == ClientState::MATCH_ENDED);
  CHECK_TRUE(state(model, 13).state == ClientState::UNKNOWN);
  CHECK_TRUE(state(model, 13).gsi == GsiHealth::Stale);
  CHECK_TRUE(model.accept(payload(), 14));
  CHECK_TRUE(state(model, 14).state == ClientState::IN_MATCH);
}
CS2N_TEST(ClientState_TimeoutBoundaryAndHeartbeatRecovery) {
  ClientStateModel model(2);
  CHECK_TRUE(model.accept(payload(), 0));
  CHECK_TRUE(state(model, 1.999).gsi == GsiHealth::Fresh);
  CHECK_TRUE(state(model, 2).gsi == GsiHealth::Stale);
  CHECK_TRUE(model.accept(payload(), 2.1));
  CHECK_TRUE(state(model, 4).gsi == GsiHealth::Fresh);
}
CS2N_TEST(ClientState_MissingMapClearsOldMatch) {
  ClientStateModel model; model.accept(payload(), 1);
  auto p = payload(); p.set("map", JsonValue::makeNull());
  CHECK_TRUE(model.accept(p, 2));
  CHECK_TRUE(state(model, 2).state == ClientState::UNKNOWN);
  CHECK_TRUE(state(model, 2).gsi == GsiHealth::Fresh);
  CHECK_TRUE(state(model, 2).reason.find("map.name absent/empty") != std::string::npos);
}
CS2N_TEST(ClientState_UnsupportedPhasesNeverInventStates) {
  ClientStateModel model;
  for(const auto* phase : {"menu", "searching", "connecting", "loading", "disconnected", "error", "", "future_phase"}) {
    CHECK_TRUE(model.accept(payload(phase), 1));
    CHECK_TRUE(state(model, 1).state == ClientState::UNKNOWN);
  }
}
CS2N_TEST(ClientState_MenuActivityAndRoundOverAreNotMatchEnd) {
  ClientStateModel model; auto p = payload();
  auto player = JsonValue::makeObject(); player.set("activity", JsonValue::makeString("menu"));
  p.set("player", player); model.accept(p, 1);
  CHECK_TRUE(state(model, 1).state == ClientState::IN_MATCH);
  p.set("map", JsonValue::makeNull()); model.accept(p, 2);
  CHECK_TRUE(state(model, 2).state == ClientState::UNKNOWN);
}
CS2N_TEST(ClientState_ProcessExitClearsSnapshotBeforeRestart) {
  ClientStateModel model; model.accept(payload(), 1);
  CHECK_TRUE(model.snapshot(1, ProcessPresence::Absent).state == ClientState::GAME_NOT_RUNNING);
  CHECK_TRUE(state(model, 1).state == ClientState::UNKNOWN);
  model.accept(payload(), 2);
  CHECK_TRUE(state(model, 2).state == ClientState::IN_MATCH);
}
CS2N_TEST(ClientState_ListenerFailureNeedsFreshDataToRecover) {
  ClientStateModel model; model.accept(payload(), 1);
  auto failed = model.snapshot(1, ProcessPresence::Running, false);
  CHECK_TRUE(failed.state == ClientState::UNKNOWN);
  CHECK_TRUE(failed.gsi == GsiHealth::Unavailable);
  CHECK_TRUE(state(model, 1).state == ClientState::UNKNOWN);
  model.accept(payload(), 2);
  CHECK_TRUE(state(model, 2).state == ClientState::IN_MATCH);
}
CS2N_TEST(ClientState_WrongIdentityCannotExtendFreshness) {
  ClientStateModel model(2, "owner"); model.accept(payload(), 1);
  auto p = payload(); auto provider = JsonValue::makeObject();
  provider.set("appid", JsonValue::makeNumber(730));
  provider.set("steamid", JsonValue::makeString("other")); p.set("provider", provider);
  CHECK_FALSE(model.accept(p, 2));
  CHECK_TRUE(state(model, 3).gsi == GsiHealth::Stale);
  provider.set("steamid", JsonValue::makeString("owner"));
  provider.set("appid", JsonValue::makeNumber(730.5)); p.set("provider", provider);
  CHECK_FALSE(model.accept(p, 3));
  CHECK_FALSE(model.accept(JsonValue::makeNull(), 4));
}
CS2N_TEST(ClientState_RejectsBadClockAndConfiguration) {
  ClientStateModel model; model.accept(payload(), 2);
  CHECK_FALSE(model.accept(payload(), 1));
  CHECK_FALSE(model.accept(payload(), std::numeric_limits<double>::quiet_NaN()));
  CHECK_TRUE(state(model, 1).state == ClientState::UNKNOWN);
  CHECK_TRUE(state(model, std::numeric_limits<double>::infinity()).state == ClientState::UNKNOWN);
  for(double timeout : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
    bool rejected = false;
    try { ClientStateModel bad(timeout); } catch(const std::invalid_argument&) { rejected = true; }
    CHECK_TRUE(rejected);
  }
}
CS2N_TEST(ClientState_ProcessEnumerationFailureIsNotAbsence) {
  ClientStateModel model;
  CHECK_TRUE(model.snapshot(0, ProcessPresence::Unknown).state == ClientState::UNKNOWN);
  model.accept(payload(), 1);
  CHECK_TRUE(model.snapshot(1, ProcessPresence::Unknown).state == ClientState::IN_MATCH);
  model.reset(); CHECK_TRUE(state(model, 1).gsi == GsiHealth::NoData);
}
CS2N_TEST(ClientState_AllStateNamesAreStable) {
  const ClientState values[] = {ClientState::GAME_NOT_RUNNING,ClientState::MAIN_MENU,ClientState::MATCH_SEARCH,
    ClientState::CONNECTING,ClientState::LOADING,ClientState::IN_MATCH,ClientState::MATCH_ENDED,
    ClientState::DISCONNECTED,ClientState::Error,ClientState::UNKNOWN};
  const char* names[] = {"GAME_NOT_RUNNING","MAIN_MENU","MATCH_SEARCH","CONNECTING","LOADING","IN_MATCH",
    "MATCH_ENDED","DISCONNECTED","ERROR","UNKNOWN"};
  for(int i=0;i<10;++i) CHECK_EQ(std::string(clientStateName(values[i])), std::string(names[i]));
}

CS2N_TEST(ClientState_RunningProcessWithoutGsiNeverMeansDisconnected) {
  ClientStateModel model;
  for(double now : {0.0, 1.0, 60.0, 86400.0}) {
    const auto result=model.snapshot(now, ProcessPresence::Running);
    CHECK_TRUE(result.state==ClientState::UNKNOWN);
    CHECK_TRUE(result.gsi==GsiHealth::NoData);
  }
  model.accept(payload(), 86401);
  CHECK_TRUE(state(model,86401).state==ClientState::IN_MATCH);
  CHECK_TRUE(state(model,86403).state==ClientState::UNKNOWN);
}

CS2N_TEST(ClientState_VisualMenuDoesNotMakeMissingMapAnAutomaticMenuSignal) {
  // Synthetic regression based on the observed missing-map condition, not a raw captured packet.
  JsonValue p; std::string error;
  CHECK_TRUE(JsonValue::parse(R"({"provider":{"appid":730,"steamid":"owner"},"player":{"activity":"menu"}})",p,error));
  ClientStateModel model; CHECK_TRUE(model.accept(p,1));
  const auto result=state(model,1);
  CHECK_TRUE(result.state==ClientState::UNKNOWN);
  CHECK_TRUE(result.gsi==GsiHealth::Fresh);
  CHECK_TRUE(result.reason.find("map.name absent/empty")!=std::string::npos);
}

CS2N_TEST(ClientState_MenuToMatchGapStaysUnknownThenRecovers) {
  ClientStateModel model(1.5);
  auto menu=payload(); menu.set("map",JsonValue::makeNull());
  CHECK_TRUE(model.accept(menu,0));
  CHECK_TRUE(state(model,1).gsi==GsiHealth::Fresh);
  CHECK_TRUE(state(model,1).state==ClientState::UNKNOWN);
  CHECK_TRUE(state(model,2).gsi==GsiHealth::Stale);
  CHECK_TRUE(state(model,2).state==ClientState::UNKNOWN);
  CHECK_TRUE(model.accept(payload(),9));
  CHECK_TRUE(state(model,9).state==ClientState::IN_MATCH);
  CHECK_TRUE(state(model,9).gsi==GsiHealth::Fresh);
}

CS2N_TEST(ClientState_MatchEndThenMenuDropsEndStateWithoutDisconnectClaim) {
  ClientStateModel model;
  CHECK_TRUE(model.accept(payload("gameover"),1));
  CHECK_TRUE(state(model,1).state==ClientState::MATCH_ENDED);
  auto menu=payload(); menu.set("map",JsonValue::makeNull());
  CHECK_TRUE(model.accept(menu,2));
  CHECK_TRUE(state(model,2).state==ClientState::UNKNOWN);
  CHECK_TRUE(state(model,2).gsi==GsiHealth::Fresh);
  CHECK_TRUE(state(model,4).state==ClientState::UNKNOWN);
  CHECK_TRUE(state(model,4).gsi==GsiHealth::Stale);
  CHECK_TRUE(model.accept(payload(),5));
  CHECK_TRUE(state(model,5).state==ClientState::IN_MATCH);
}
