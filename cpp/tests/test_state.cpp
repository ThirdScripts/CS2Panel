// GSI evaluation and provider composition.
//
// This provider consumes local-player liveness and identity, not coordinates.
// Spectator-only data is not a substitute for the active player position source.
// These tests pin the current provider contract down.
#include "state/gsi_provider.h"
#include "state/provider.h"
#include "tests/framework.h"
#include <limits>

using namespace cs2n;

namespace {

std::string livePayload(const std::string& map = "de_dust2", const std::string& phase = "live",
                        double health = 100.0, const std::string& activity = "playing") {
  return std::string(R"({"provider":{"appid":730,"steamid":"76561198000000000"},")") +
         R"(map":{"name":")" + map + R"(","phase":")" + phase + R"("},)" +
         R"("player":{"activity":")" + activity + R"(","steamid":"76561198000000000",)" +
         R"("state":{"health":)" + std::to_string(health) + "}}}";
}

}  // namespace

CS2N_TEST(Gsi_AcceptsHealthyPlayerOnDust2) {
  GsiConfig config;
  config.requiredMap = "de_dust2";
  const auto evaluation = GsiProvider::evaluatePayload(livePayload(), config, 42.0);
  CHECK_TRUE(evaluation.accepted);
  CHECK_TRUE(evaluation.state.valid);
  CHECK_TRUE(evaluation.state.alive);
  CHECK_EQ(evaluation.state.mapName, std::string("de_dust2"));
  CHECK_NEAR(evaluation.state.timestamp, 42.0, 1e-9);
}

CS2N_TEST(Gsi_RejectsOtherMaps) {
  GsiConfig config;
  const auto evaluation = GsiProvider::evaluatePayload(livePayload("de_inferno"), config, 0.0);
  CHECK_FALSE(evaluation.accepted);
  CHECK_FALSE(evaluation.rejection.empty());
}

CS2N_TEST(Gsi_AutoMapReportsMirageAndRejectsMissingMap) {
  GsiConfig config; config.requiredMap.clear();
  auto live=GsiProvider::evaluatePayload(livePayload("de_mirage"),config,10);
  CHECK_TRUE(live.accepted);CHECK_EQ(live.state.mapName,std::string("de_mirage"));
  CHECK_FALSE(GsiProvider::evaluatePayload(livePayload(""),config,10).accepted);
  auto setup=GsiProvider::evaluateMatchSetup(R"({"provider":{"appid":730,"steamid":"local"},"map":{"name":"de_mirage","mode":"deathmatch","phase":"live"}})",config,10);
  CHECK_TRUE(setup.valid);CHECK_EQ(setup.mapName,std::string("de_mirage"));CHECK_EQ(setup.team,0);
}

CS2N_TEST(Gsi_ReportsDeadPlayer) {
  GsiConfig config;
  const auto evaluation = GsiProvider::evaluatePayload(livePayload("de_dust2", "live", 0.0), config, 0.0);
  CHECK_TRUE(evaluation.accepted);
  CHECK_FALSE(evaluation.state.alive);
}

CS2N_TEST(Gsi_RejectsNonPlayingActivity) {
  GsiConfig config;
  const auto evaluation = GsiProvider::evaluatePayload(livePayload("de_dust2", "live", 100.0, "menu"), config, 0.0);
  CHECK_FALSE(evaluation.accepted);
}

CS2N_TEST(Gsi_SetupAcceptsTeamSelectionWithoutPawnButNotLobby) {
  GsiConfig c;
  const std::string prefix=R"({"provider":{"appid":730,"steamid":"local"},)";
  const std::string map=R"("map":{"name":"de_dust2","mode":"deathmatch","phase":"live"})";
  auto selection=GsiProvider::evaluateMatchSetup(prefix+map+"}",c,10);
  CHECK_TRUE(selection.valid);CHECK_EQ(selection.team,0);
  CHECK_FALSE(GsiProvider::evaluateMatchSetup(prefix+R"("player":{"activity":"menu"}})",c,10).valid);
  auto t=GsiProvider::evaluateMatchSetup(prefix+map+R"(,"player":{"steamid":"local","team":"T"}})",c,10);
  CHECK_TRUE(t.valid);CHECK_EQ(t.team,2);
  auto ct=GsiProvider::evaluateMatchSetup(prefix+map+R"(,"player":{"steamid":"local","team":"CT"}})",c,10);
  CHECK_TRUE(ct.valid);CHECK_EQ(ct.team,3);
  CHECK_FALSE(GsiProvider::evaluateMatchSetup(prefix+map+R"(,"player":{"steamid":"other","team":"CT"}})",c,10).valid);
}

CS2N_TEST(Gsi_SetupRejectsOtherModesMapsAndEndOfMatch) {
  GsiConfig c;
  for(const auto& map:{std::string(R"({"name":"de_mirage","mode":"deathmatch","phase":"live"})"),
      std::string(R"({"name":"de_dust2","mode":"competitive","phase":"live"})"),
      std::string(R"({"name":"de_dust2","mode":"deathmatch","phase":"intermission"})")}) {
    CHECK_FALSE(GsiProvider::evaluateMatchSetup(std::string(R"({"provider":{"appid":730,"steamid":"local"},"map":)")+map+"}",c,10).valid);
  }
}

CS2N_TEST(Gsi_RejectsInactiveMatchPhase) {
  GsiConfig config;
  const auto evaluation = GsiProvider::evaluatePayload(livePayload("de_dust2", "intermission"), config, 0.0);
  CHECK_FALSE(evaluation.accepted);
}

CS2N_TEST(Gsi_RejectsMalformedPayload) {
  GsiConfig config;
  CHECK_FALSE(GsiProvider::evaluatePayload("{not json", config, 0.0).accepted);
  CHECK_FALSE(GsiProvider::evaluatePayload("{}", config, 0.0).accepted);
}

CS2N_TEST(Gsi_NeverSuppliesCoordinates) {
  // This implementation does not obtain coordinates from the local-player payload.
  // Do not treat default zero coordinates as a measured position.
  GsiConfig config;
  const auto evaluation = GsiProvider::evaluatePayload(livePayload(), config, 0.0);
  CHECK_TRUE(evaluation.accepted);
  CHECK_NEAR(evaluation.state.position.x, 0.0, 1e-12);
  CHECK_NEAR(evaluation.state.position.y, 0.0, 1e-12);
}

CS2N_TEST(Composite_RequiresBothSources) {
  PlayerState live;
  live.valid = true;
  live.alive = true;
  live.mapName = "de_dust2";
  live.timestamp = 10.0;

  PlayerState positioned;
  positioned.valid = true;
  positioned.alive = true;
  positioned.position = Vec3{12.0, 34.0, 0.0};
  positioned.yaw = 0.5;
  positioned.timestamp = 10.0;

  CompositeProvider provider(std::make_shared<StaticProvider>(live, "live"),
                             std::make_shared<StaticProvider>(positioned, "position"));
  const auto state = provider.poll();
  CHECK_TRUE(state.has_value());
  CHECK_NEAR(state->position.x, 12.0, 1e-9);
  CHECK_NEAR(state->position.y, 34.0, 1e-9);
  CHECK_NEAR(state->yaw, 0.5, 1e-9);
  CHECK_EQ(state->mapName, std::string("de_dust2"));
}

CS2N_TEST(Composite_PreservesDeathWithoutPosition) {
  PlayerState live;
  live.valid = true;
  live.alive = false;  // GSI reports zero health

  PlayerState positioned;
  positioned.valid = true;
  positioned.position = Vec3{1.0, 2.0, 0.0};
  positioned.timestamp = 5.0;

  CompositeProvider provider(std::make_shared<StaticProvider>(live),
                             std::make_shared<StaticProvider>(positioned));
  CHECK_TRUE(provider.poll().has_value());
  CHECK_FALSE(provider.poll()->alive);
}

CS2N_TEST(Composite_FailsWhenPositionIsMissing) {
  PlayerState live;
  live.valid = true;
  live.alive = true;

  CompositeProvider provider(std::make_shared<StaticProvider>(live), std::make_shared<StaticProvider>(PlayerState{}));
  CHECK_FALSE(provider.poll().has_value());
}

CS2N_TEST(Composite_RejectsStalePosition) {
  PlayerState live;
  live.valid = true;
  live.alive = true;
  live.timestamp = 10.0;

  PlayerState positioned;
  positioned.valid = true;
  positioned.position = Vec3{1.0, 2.0, 0.0};
  positioned.timestamp = 9.0;  // one second old

  CompositeProvider provider(std::make_shared<StaticProvider>(live),
                             std::make_shared<StaticProvider>(positioned));
  provider.setMaxPositionAge(0.35);
  CHECK_FALSE(provider.poll().has_value());
}

CS2N_TEST(Composite_RejectsNonFiniteCoordinates) {
  PlayerState live;
  live.valid = true;
  live.alive = true;
  live.timestamp = 1.0;

  PlayerState positioned;
  positioned.valid = true;
  positioned.position = Vec3{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
  positioned.timestamp = 1.0;

  CompositeProvider provider(std::make_shared<StaticProvider>(live),
                             std::make_shared<StaticProvider>(positioned));
  CHECK_FALSE(provider.poll().has_value());
}



CS2N_TEST(Gsi_PreservesReportedHealthIncludingDeath) {
  GsiConfig config;
  for (int health : {0, 1, 37, 100, 500}) {
    auto result = GsiProvider::evaluatePayload(livePayload("de_dust2", "live", health), config, 1);
    CHECK_TRUE(result.accepted);
    CHECK_EQ(result.state.health, health);
    CHECK_EQ(result.state.alive, health > 0);
  }
  CHECK_FALSE(GsiProvider::evaluatePayload(livePayload("de_dust2", "live", 1.5), config, 1).accepted);
}

CS2N_TEST(Gsi_EvaluatorsRespectEndpointIdentityAndClock) {
  GsiConfig config; config.expectedSteamId = "other";
  CHECK_FALSE(GsiProvider::evaluatePayload(livePayload(), config, 1).accepted);
  const std::string setup = R"({"provider":{"appid":730,"steamid":"local"},"map":{"name":"de_dust2","mode":"deathmatch","phase":"live"}})";
  CHECK_FALSE(GsiProvider::evaluateMatchSetup(setup, config, 1).valid);
  config.expectedSteamId.clear();
  CHECK_TRUE(GsiProvider::evaluateMatchSetup(setup, config, 1).valid);
  for (double now : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(GsiProvider::evaluatePayload(livePayload(), config, now).accepted);
    CHECK_FALSE(GsiProvider::evaluateMatchSetup(setup, config, now).valid);
  }
  auto missing = livePayload();
  const std::string id = "76561198000000000";
  for (auto pos = missing.find(id); pos != std::string::npos; pos = missing.find(id)) missing.erase(pos, id.size());
  CHECK_FALSE(GsiProvider::evaluatePayload(missing, config, 1).accepted);
}
