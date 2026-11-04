// The navigation engine: a fixed-rate control loop driving an explicit state
// machine.
//
//   IDLE -> NAVIGATING -> BLOCKED -> RECOVERING -> NAVIGATING
//                        \-> ERROR (recovery budget exhausted)
//   any -> DEAD -> RESPAWNING -> NAVIGATING
//   any -> PAUSED (user, data loss) -> back to where it was
//
// The engine owns nothing platform specific: it talks to an IPlayerStateProvider
// for where the player is and an IInputBackend for acting on the world, so the
// whole machine can be driven by the simulator in the tests.
#pragma once
#include "state/observer_pause.h"
#include "perception/awareness.h"
#include "perception/radar_pursuit.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "movement/look_controller.h"
#include "movement/move_controller.h"
#include "movement/stuck_detector.h"
#include "navigation/navigator.h"
#include "state/provider.h"
#include "combat/controller.h"

namespace cs2n {

enum class EngineState {
  Idle,
  Navigating,
  Blocked,
  Recovering,
  Dead,
  Respawning,
  Paused,
  Error,
  WaitingForData,
};

const char* engineStateName(EngineState state);

struct EngineConfig {
  double controlHz = 250.0;         // steering loop rate
  double planIntervalSeconds = 0.5; // re-check the route periodically
  double dataLossTimeout = 8.0;     // give up after this long without any state
  int recoveryAttempts = 4;         // blocked cycles before giving up
  double strafeRecoverySeconds = 0.7;
  int respawnConfirmations = 4;     // fresh poses required before moving again
  StuckConfig stuck;                // when to declare the player wedged
};

struct EngineSnapshot {
  const NavMesh* activeMesh = nullptr;
  std::string detectedMap;
  EngineState state = EngineState::Idle;
  std::string status = "idle";
  PlayerState player;
  bool havePlayer = false;
  std::size_t pathPoints = 0;
  std::size_t targetIndex = 0;
  double remainingDistance = 0.0;
  double aimErrorDeg = 0.0;
  int stuckCount = 0;
  int recoveryAttempt = 0;
  double controlHz = 0.0;
  double providerHz = 0.0;
  std::uint64_t tick = 0;
  std::string lastError;
  bool combatEnabled=true,combatActive=false,firing=false;
  int combatTargets=0;
  std::uint32_t combatTarget=0;
  double combatError=0;
};

class IInputBackend;

class Engine {
 public:
  Engine(IInputBackend* input, PlayerStateProviderPtr provider, Navigator navigator,
         LookConfig lookConfig = {}, MoveConfig moveConfig = {}, EngineConfig config = {});
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void start();     // begins navigating from wherever the player is
  void stop();      // graceful stop, releases every key
  void pause();     // temporary hold that keeps the route
  void resume();
  void emergencyStop();
  void setCombatMesh(const VisibilityMesh* mesh);
  // Registered meshes remain immutable and outlive the engine and its UI.
  void registerMap(const std::string& name, const NavMesh* navigation, const VisibilityMesh* visibility);
  void setCombatEnabled(bool enabled);
  void setIntensity(double value);
  void setPerception(GameAudio* audio,RadarObserver* radar);


  [[nodiscard]] EngineSnapshot snapshot() const;
  [[nodiscard]] bool running() const { return running_.load(); }
  [[nodiscard]] EngineState state() const { return state_.load(); }

  // Path drawn by the UI; safe to call from any thread.
  [[nodiscard]] std::vector<PathPoint> currentPath() const;

  void setLookConfig(const LookConfig& config);
  void setMoveConfig(const MoveConfig& config);
  [[nodiscard]] LookConfig lookConfig() const;

  Navigator& navigator() { return navigator_; }
  [[nodiscard]] const Navigator& navigator() const { return navigator_; }

 private:
  bool selectMap(const std::string& name);
  struct MapEntry {std::string name; const NavMesh* navigation; const VisibilityMesh* visibility;};
  std::vector<MapEntry> maps_;
  std::string activeMap_;
  void loop();
  void applyEmergencyStop(const std::string& reason);
  void enterState(EngineState next, const std::string& reason);
  void releaseMovement();
  bool pollPlayer(PlayerState& out);
  bool handleCombatTick(double now,const PlayerState& player);
  void handleNavigationTick(double now, const PlayerState& player);
  void handleStuck(double now, const PlayerState& player, double speed, bool aimConverged, double aimErrorDeg);
  void enterRecovery(double now, const std::string& reason);
  void enterDead(const std::string& reason);
  void handleRespawnTick(double now, const PlayerState& player);
  // Publishes the snapshot for the UI and sleeps until the next tick.
  void publishAndSleep(double now, double tickInterval, double& nextDeadline);

  IInputBackend* input_;
  PlayerStateProviderPtr provider_;
  Navigator navigator_;
  MoveController move_;
  LookController look_;
  StuckDetector stuck_;
  EngineConfig config_;
  const VisibilityMesh* combatMesh_=nullptr;
  CombatController combat_;
  ObserverPause observerPause_;
  bool observerHeld_=false;
  bool combatEnabled_=true;
  GameAudio* audio_=nullptr;
  RadarObserver* radarObserver_=nullptr;
  Awareness awareness_;
  RadarPursuit radarPursuit_;
  std::optional<RadarGoal> radarGoal_;
  bool radarRoute_=false;
  Vec3 radarPlannedPosition_;
  double radarPlanAt_=-1000;
  double combatExitAt_=-1000,combatExitYaw_=0;

  std::thread thread_;
  std::atomic<bool> done_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> emergency_{false};
  std::atomic<EngineState> state_{EngineState::Idle};

  mutable std::recursive_mutex mutex_;
  EngineSnapshot snapshot_;
  Path path_;
  std::string status_ = "idle";
  std::string lastError_;
  double lastPlanAt_ = -1.0;
  double lastDataAt_ = -1.0;
  double recoveryStartedAt_ = -1.0;
  double retryRouteAt_=0,loadoutAt_=-1,lastSelectAt_=-1000;
  bool loadoutPending_=true;
  int loadoutAttempts_=0,joinAttempts_=0,joinObservedTeam_=-1;
  double joinAt_=-1000;
  int recoveryAttempt_ = 0;
  int respawnCountdown_ = 0;
  double providerTicks_ = 0.0;
  double providerWindowStart_ = -1.0;
  double providerHz_ = 0.0;
  double controlTicks_ = 0.0;
  double controlWindowStart_ = -1.0;
  double controlHz_ = 0.0;
  std::uint64_t tickCounter_ = 0;
  double lastKnownYaw_ = 0.0;
  double previousTick_=0, tickDt_=.004, progressAt_=0;
  double maxTickGapMs_=0,maxTickWorkMs_=0;
  unsigned lateTicks_=0;
  Vec2 recoveryReference_;
  bool havePositionReference_ = false;
  Vec2 positionReference_;
};

}  // namespace cs2n
