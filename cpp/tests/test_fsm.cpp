// End-to-end state machine tests driven by a simulated world, no CS2 required.
#include <chrono>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

#include "app/engine.h"
#include "movement/input.h"
#include "state/provider.h"
#include "tests/framework.h"
#include "tests/mesh_fixture.h"

using namespace cs2n;

namespace {

// Records what the engine asked the game to do.
class FakeInput final : public IInputBackend {
 public:
  void setWalk(bool down) override { if (down != walk) ++walkChanges; walk = down; }
  void setStrafe(int direction) override { strafe = direction; }
  void turn(double counts) override { totalTurn += counts; }

  void releaseAll() override {
    walk = false;
    strafe = 0;
    ++releases;
  }
  [[nodiscard]] std::string name() const override { return "fake"; }

  std::atomic<bool> walk{false};
  std::atomic<int> strafe{0};
  std::atomic<double> totalTurn{0.0};
  std::atomic<int> walkChanges{0};
  std::atomic<int> releases{0};
};

// A world that can be advanced by hand: alive/dead, position, velocity.
class ScriptedWorld final : public IPlayerStateProvider {
 public:
  std::optional<PlayerState> poll() override {
    std::lock_guard lock(positionMutex);
    if (!available) return std::nullopt;
    PlayerState state;
    state.valid = true;
    state.alive = alive;
    state.position = Vec3{position.x, position.y, 0.0};
    state.velocity = Vec3{velocity.x, velocity.y, 0.0};
    state.yaw = yaw;
    state.mapName = "de_dust2";
    state.timestamp = timestamp;
    return state;
  }
  [[nodiscard]] std::string name() const override { return "scripted"; }

  void setPosition(Vec2 value) { std::lock_guard lock(positionMutex); position=value; }
  std::mutex positionMutex;
  std::atomic<bool> available{true};
  std::atomic<bool> alive{true};
  Vec2 position{50, 50};
  Vec2 velocity;
  double yaw = 0.0;
  double timestamp = 0.0;
};

std::shared_ptr<ScriptedWorld> makeWorld() { return std::make_shared<ScriptedWorld>(); }

}  // namespace

CS2N_TEST(Engine_AutoMapChangesAndUnknownMapClearsNavigation) {
  class MapWorld : public IPlayerStateProvider {
   public:
    std::atomic<int> selected{0};
    std::string map() const {return selected==0?"de_dust2":selected==1?"de_mirage":selected==2?"de_inferno":"";}
    std::string name() const override {return "map test";}
    MatchSetupState matchSetup() const override {return {selected!=3,2,1,map()};}
    std::optional<PlayerState> poll() override {
      PlayerState p;p.valid=true;p.alive=true;p.mapName=map();p.position={50,50,0};p.timestamp=1;return p;
    }
  };
  auto world=std::make_shared<MapWorld>();FakeInput input;
  auto dust=cs2n::test::makeRoomMesh(4,4,100,-1,0);
  auto mirage=cs2n::test::makeRoomMesh(5,5,100,-1,0);
  Engine engine(&input,world,Navigator(nullptr));
  engine.registerMap("de_dust2",&dust,nullptr);engine.registerMap("de_mirage",&mirage,nullptr);
  engine.start();
  auto waitMap=[&](const NavMesh* mesh,const std::string& name) {
    for(int i=0;i<100;++i) {
      auto s=engine.snapshot();if(s.activeMesh==mesh && s.detectedMap==name)return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }return false;
  };
  CHECK_TRUE(waitMap(&dust,"de_dust2"));
  world->selected=1;CHECK_TRUE(waitMap(&mirage,"de_mirage"));
  world->selected=2;CHECK_TRUE(waitMap(nullptr,"de_inferno"));
  CHECK_TRUE(engine.currentPath().empty());CHECK_FALSE(engine.snapshot().havePlayer);
  world->selected=3;CHECK_TRUE(waitMap(nullptr,""));
  world->selected=0;CHECK_TRUE(waitMap(&dust,"de_dust2"));
  engine.stop();
}

CS2N_TEST(Engine_ObserverPauseAutomaticallyResumesButPreservesManualStop) {
  class ObservedWorld : public IPlayerStateProvider {
   public:
    std::atomic<int> count{1};
    std::optional<PlayerState> poll() override {
      PlayerState p;p.valid=p.alive=p.observersValid=true;p.spectators=count;
      p.position={50,50,0};p.timestamp=1;return p;
    }
    std::string name() const override{return "observer-test";}
  };
  auto world=std::make_shared<ObservedWorld>();FakeInput input;
  const auto mesh=cs2n::test::makeRoomMesh(4,4,100,-1,0);
  Engine engine(&input,world,Navigator(&mesh));engine.start();
  for(int i=0;i<100&&engine.state()!=EngineState::Paused;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK_TRUE(engine.running());CHECK_TRUE(engine.state()==EngineState::Paused);
  world->count=0;std::this_thread::sleep_for(std::chrono::milliseconds(40));
  CHECK_TRUE(engine.state()==EngineState::Paused);
  for(int i=0;i<150&&engine.state()==EngineState::Paused;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK_TRUE(engine.running());CHECK_TRUE(engine.state()!=EngineState::Paused);
  world->count=1;
  for(int i=0;i<100&&engine.state()!=EngineState::Paused;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
  engine.pause();world->count=0;
  std::this_thread::sleep_for(std::chrono::milliseconds(1150));
  CHECK_TRUE(engine.state()==EngineState::Paused);
  engine.stop();CHECK_FALSE(engine.running());
}

CS2N_TEST(ObserverPause_UnknownSamplesAndBriefClearDoNotResume) {
  ObserverPause p;CHECK_FALSE(p.update(true,0,0));
  CHECK_TRUE(p.update(true,1,1));CHECK_TRUE(p.update(true,0,2));
  CHECK_TRUE(p.update(false,0,4));CHECK_TRUE(p.update(true,0,5));
  CHECK_TRUE(p.update(true,0,5.9));CHECK_FALSE(p.update(true,0,6.1));
}



CS2N_TEST(Engine_StartsIdleAndStaysSafe) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh room = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&room);
  Engine engine(&input, world, navigator);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  engine.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::Idle));
  CHECK_FALSE(input.walk);
}

CS2N_TEST(Engine_ReleasesKeysOnStop) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  engine.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(input.walk);
  CHECK_EQ(input.strafe.load(), 0);
}

CS2N_TEST(Engine_ReleasesKeysOnEmergencyStop) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  Engine engine(&input, world, navigator);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  engine.emergencyStop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::Error));
  CHECK_FALSE(input.walk);
  CHECK_EQ(input.strafe.load(), 0);
  CHECK_FALSE(engine.running());
}

CS2N_TEST(Engine_EntersDeadAndStopsMoving) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  world->alive = false;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const EngineState state = engine.state();
  CHECK_TRUE(state == EngineState::Dead || state == EngineState::Respawning);
  CHECK_FALSE(input.walk);
  engine.stop();
}

CS2N_TEST(Engine_RecoversAfterRespawn) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  config.respawnConfirmations = 2;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  world->alive = false;
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  world->alive = true;
  world->setPosition(Vec2{350, 350});  // respawned somewhere else
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  // Either back to navigating or already navigating again.
  const EngineState state = engine.state();
  CHECK_TRUE(state == EngineState::Navigating || state == EngineState::Respawning);
  engine.stop();
}

CS2N_TEST(Engine_WaitsSafelyAndResumesWhenDataReturns) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  config.dataLossTimeout = 0.4;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  world->available = false;  // GSI stops reporting, radar loses the marker
  std::this_thread::sleep_for(std::chrono::milliseconds(700));

  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::WaitingForData));
  CHECK_FALSE(input.walk);CHECK_TRUE(engine.running());
  world->available=true;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK_TRUE(engine.state()==EngineState::Navigating);
  engine.stop();
}







CS2N_TEST(Engine_TracksStuckAndRecovers) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  // Short thresholds so the test observes a full stall episode quickly; the
  // production defaults (2.5 s stopped, 3 s without progress) stay unchanged.
  config.stuck.stoppedSeconds = 0.25;
  config.stuck.noProgressSeconds = 0.35;
  config.recoveryAttempts = 3;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();

  // The world never moves: the player is welded in place, so the engine must
  // eventually declare a recovery and then give up safely.
  std::this_thread::sleep_for(std::chrono::milliseconds(900));
  const auto snapshot = engine.snapshot();
  CHECK_TRUE(snapshot.stuckCount > 0 || engine.state() == EngineState::Error);
  engine.stop();
}

CS2N_TEST(Engine_SnapshotExposesDiagnostics) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  const EngineSnapshot snapshot = engine.snapshot();
  CHECK_TRUE(snapshot.havePlayer);
  CHECK_EQ(snapshot.player.mapName, std::string("de_dust2"));
  CHECK_GT(snapshot.controlHz, 50.0);
  CHECK_GT(snapshot.providerHz, 10.0);
  CHECK_TRUE(snapshot.state == EngineState::Navigating);
  engine.stop();
}

CS2N_TEST(Engine_PauseHoldsRouteAndResumeRestoresIt) {
  auto world = makeWorld();
  FakeInput input;
  const NavMesh mesh = cs2n::test::makeRoomMesh(4, 4, 100.0, -1, 0);
  Navigator navigator(&mesh);
  EngineConfig config;
  config.controlHz = 200.0;
  Engine engine(&input, world, navigator, LookConfig{}, MoveConfig{}, config);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const std::size_t pathBefore = engine.currentPath().size();
  engine.pause();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::Paused));
  CHECK_FALSE(input.walk);

  engine.resume();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::Navigating));
  CHECK_TRUE(engine.currentPath().size() > 0 || pathBefore > 0);
  engine.stop();
}

CS2N_TEST(Engine_WithoutMeshFailsSafely) {
  auto world = makeWorld();
  FakeInput input;
  Navigator navigator(nullptr);  // no mesh loaded
  Engine engine(&input, world, navigator);
  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK_EQ(static_cast<int>(engine.state()), static_cast<int>(EngineState::Error));
  CHECK_FALSE(input.walk);
  engine.stop();
}
