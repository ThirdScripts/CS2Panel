#include "app/engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/log.h"
#include "core/types.h"
#include "movement/input.h"

namespace cs2n {
namespace {

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

const char* engineStateName(EngineState state) {
  switch (state) {
    case EngineState::Idle: return "IDLE";
    case EngineState::Navigating: return "NAVIGATING";
    case EngineState::Blocked: return "BLOCKED";
    case EngineState::Recovering: return "RECOVERING";
    case EngineState::Dead: return "DEAD";
    case EngineState::Respawning: return "RESPAWNING";
    case EngineState::Paused: return "PAUSED";
    case EngineState::Error: return "ERROR";
    case EngineState::WaitingForData: return "WAITING_FOR_DATA";
  }
  return "IDLE";
}

Engine::Engine(IInputBackend* input, PlayerStateProviderPtr provider, Navigator navigator, LookConfig lookConfig,
               MoveConfig moveConfig, EngineConfig config)
    : input_(input),
      provider_(std::move(provider)),
      navigator_(std::move(navigator)),
      move_(moveConfig),
      look_(lookConfig),
      config_(config) {
  move_.setMesh(navigator_.mesh());
  if(input_&&input_->continuousCamera()){
    auto c=look_.config();c.quantizeCounts=false;look_.setConfig(c);
    combat_.setContinuousCamera(true);
  }
  stuck_.setConfig(config_.stuck);
}

Engine::~Engine() {
  done_ = true;
  if (thread_.joinable()) thread_.join();
  if (input_ != nullptr) input_->releaseAll();
}

void Engine::registerMap(const std::string& name, const NavMesh* navigation, const VisibilityMesh* visibility) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (running_ || name.empty() || !navigation || !navigation->valid()) return;
  for (const auto& entry : maps_) if (entry.name == name) return;
  maps_.push_back({name, navigation, visibility});
}

bool Engine::selectMap(const std::string& name) {
  const MapEntry* selected = nullptr;
  for (const auto& entry : maps_) if (entry.name == name) {selected = &entry; break;}
  if (activeMap_ != name || navigator_.mesh() != (selected ? selected->navigation : nullptr)) {
    releaseMovement();
    activeMap_ = name;
    navigator_.setMesh(selected ? selected->navigation : nullptr);
    move_.setMesh(navigator_.mesh());
    combatMesh_ = selected ? selected->visibility : nullptr;
    path_.clear(); look_.reset(); stuck_.resetAll(); combat_.reset(); awareness_.reset();
    radarPursuit_.reset(); radarGoal_.reset(); radarRoute_=false; radarPlanAt_=-1000;
    havePositionReference_=false; recoveryAttempt_=0; retryRouteAt_=0; lastPlanAt_=-1;
    combatExitAt_=-1000; lastSelectAt_=-1000;
    joinAttempts_=0; joinObservedTeam_=-1; joinAt_=-1000;
    loadoutPending_=true; loadoutAttempts_=0; loadoutAt_=-1;
    snapshot_.havePlayer=false; snapshot_.player={}; snapshot_.pathPoints=0;
    snapshot_.targetIndex=0; snapshot_.remainingDistance=0;
    snapshot_.combatActive=false; snapshot_.firing=false; snapshot_.combatTarget=0; snapshot_.combatTargets=0;
    snapshot_.activeMesh=navigator_.mesh(); snapshot_.detectedMap=name;
    lastError_.clear();
    enterState(EngineState::WaitingForData, selected ? "map selected: " + name :
      name.empty() ? "waiting for Deathmatch map" : "unsupported map: " + name);
    Logger::instance().info("Map: " + status_);
  }
  if (!selected && name.empty()) status_="waiting for Deathmatch map";
  return selected != nullptr;
}

void Engine::start() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (running_.load()) return;
  if (state_.load() == EngineState::Error) {
    Logger::instance().info("Engine: clearing previous error on start");
  }
  stuck_.resetAll();
  radarPursuit_.reset();radarGoal_.reset();radarRoute_=false;radarPlanAt_=-1000;
  path_.clear();navigator_.clearGoal();look_.reset();
  lastDataAt_=monotonicSeconds();lastPlanAt_=-1;havePositionReference_=false;
  enterState(EngineState::Idle,"starting");
  recoveryAttempt_ = 0;
  loadoutPending_=true;loadoutAt_=-1;loadoutAttempts_=0;joinAttempts_=0;joinObservedTeam_=-1;joinAt_=-1000;combat_.reset();
  emergency_ = false;
  paused_ = false;
  running_ = true;
  observerPause_=ObserverPause{};observerHeld_=false;
  if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
  Logger::instance().info("Engine: navigation requested");
}

void Engine::stop() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  running_ = false;
  paused_ = false;
  enterState(EngineState::Idle, "stopped by operator");
  if (input_ != nullptr) input_->releaseAll();
  Logger::instance().info("Engine: stopped");
}

void Engine::pause() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (state_.load() == EngineState::Idle) return;
  paused_ = true;
  enterState(EngineState::Paused, "paused by operator");
}

void Engine::resume() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!paused_.load()) return;
  paused_ = false;
  enterState(running_.load() ? EngineState::Navigating : EngineState::Idle, "resumed");
}

void Engine::emergencyStop() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  emergency_ = true;
  running_ = false;
  applyEmergencyStop("emergency stop");
  Logger::instance().warn("Engine: EMERGENCY STOP");
}



EngineSnapshot Engine::snapshot() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  EngineSnapshot copy = snapshot_;
  copy.state = state_.load();
  copy.stuckCount = stuck_.stuckCount();
  copy.activeMesh = navigator_.mesh();
  return copy;
}

std::vector<PathPoint> Engine::currentPath() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return path_.points;
}

void Engine::setLookConfig(const LookConfig& config) { std::lock_guard<std::recursive_mutex> lock(mutex_); look_.setConfig(config); }
void Engine::setMoveConfig(const MoveConfig& config) { std::lock_guard<std::recursive_mutex> lock(mutex_); move_.setConfig(config); }
LookConfig Engine::lookConfig() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return look_.config(); }

void Engine::releaseMovement() {
  if (input_ == nullptr) return;
  input_->setWalk(false);
  input_->setBackward(false);
  input_->setStrafe(0);
  input_->setFire(false);
  input_->setCrouch(false);
}

void Engine::setCombatMesh(const VisibilityMesh* mesh) {std::lock_guard<std::recursive_mutex> lock(mutex_);combatMesh_=mesh;}
void Engine::setIntensity(double value){std::lock_guard<std::recursive_mutex> lock(mutex_);combat_.setIntensity(value);}
void Engine::setPerception(GameAudio* audio,RadarObserver* radar){std::lock_guard<std::recursive_mutex> lock(mutex_);audio_=audio;radarObserver_=radar;}
void Engine::setCombatEnabled(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);combatEnabled_=enabled;
  if(!enabled) {combat_.reset();if(input_){input_->setFire(false);input_->setCrouch(false);}}
}
bool Engine::handleCombatTick(double now,const PlayerState& player) {
  const bool wasActive=snapshot_.combatActive;
  snapshot_.combatEnabled=combatEnabled_;snapshot_.combatActive=false;snapshot_.firing=false;
  if(!combatEnabled_||!combatMesh_) {input_->setFire(false);input_->setCrouch(false);combat_.reset();return false;}
  const auto result=combat_.update(player,*combatMesh_,now,tickDt_);
  snapshot_.combatTargets=result.visibleTargets;snapshot_.combatTarget=result.target;snapshot_.combatError=result.angularError;
  if(result.reload)input_->reload();
  if(!result.engaged) {
    input_->setFire(false);
    input_->setCrouch(false);
    if(wasActive) {combatExitAt_=now;combatExitYaw_=player.yaw;look_.reset();stuck_.resetRoute();status_="combat ended; continuing route";}
    return false;
  }
  input_->setWalk(result.forward);input_->setBackward(result.backward);input_->setStrafe(result.strafe);
  input_->setCrouch(result.crouch);
  if(result.scope)input_->scope();
  input_->turn(result.yawCounts);input_->pitch(result.pitchCounts);input_->setFire(result.fire);
  snapshot_.combatActive=true;snapshot_.firing=result.fire;
  status_=result.status+" target="+std::to_string(result.target);
  stuck_.resetRoute();look_.reset();recoveryStartedAt_=now;
  return true;
}

void Engine::enterState(EngineState next, const std::string& reason) {
  const EngineState previous = state_.exchange(next);
  if (previous != next) {
    Logger::instance().info(std::string("Engine: ") + engineStateName(previous) + " -> " + engineStateName(next) +
                            " (" + reason + ")");
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  status_ = reason;
}

void Engine::applyEmergencyStop(const std::string& reason) {
  releaseMovement();
  if (input_ != nullptr) input_->releaseAll();
  enterState(EngineState::Error, reason);
  running_ = false;
}

bool Engine::pollPlayer(PlayerState& out) {
  if (!provider_) return false;
  auto state = provider_->poll();
  if (!state) {lastError_=provider_->diagnostic();return false;}
  out = *state;
  lastError_.clear();
  return true;
}

void Engine::handleNavigationTick(double now, const PlayerState& player) {
  if (!navigator_.ready()) {
    applyEmergencyStop("navigation mesh is not loaded");
    lastError_ = "navigation mesh is not loaded";
    return;
  }

  const Vec2 position = player.position.xy();
  if(radarGoal_&&combatEnabled_) {
    if(!radarRoute_||path_.empty()||(now-radarPlanAt_>.6&&
       (distance(radarGoal_->position.xy(),radarPlannedPosition_.xy())>64||std::abs(radarGoal_->position.z-radarPlannedPosition_.z)>32))) {
      Path pursuit;std::string error;
      if(navigator_.planTo(position,player.position.z,radarGoal_->position.xy(),radarGoal_->position.z,pursuit,error)) {
        path_=std::move(pursuit);radarRoute_=true;radarPlanAt_=now;
        radarPlannedPosition_=radarGoal_->position;lastPlanAt_=now;
        Logger::instance().info("Radar pursuit: target="+std::to_string(radarGoal_->id));
      }else{radarPursuit_.reject(now);radarGoal_.reset();}
    }
    if(radarGoal_)status_="moving to radar contact "+std::to_string(radarGoal_->id);
  }
  if(radarRoute_&&(!radarGoal_||!combatEnabled_)) {
    path_.clear();navigator_.clearGoal();radarRoute_=false;
    status_="radar contact expired or reached; resuming patrol";
  }
  lastKnownYaw_ = player.yaw;
  look_.setMeasuredYaw(player.yaw);
  if(player.radiansPerMouseCount>0) {auto c=look_.config();c.radiansPerMouseCount=player.radiansPerMouseCount;look_.setConfig(c);}

  // A jump this large cannot be locomotion: treat it as a respawn.
  if (!path_.empty()) {
    if (!havePositionReference_) {
      positionReference_ = position;
      havePositionReference_ = true;
    } else if (distance(position, positionReference_) > 400.0) {
      positionReference_ = position;
      stuck_.resetRoute();
      path_.clear();
      navigator_.clearGoal();
      respawnCountdown_ = config_.respawnConfirmations;
      enterState(EngineState::Respawning, "position jumped; relocalising");
      return;
    } else if (distance(position, positionReference_) > 40.0) {
      positionReference_ = position;
    }
  }

  if (path_.empty() || now - lastPlanAt_ > config_.planIntervalSeconds) {
    std::string error;
    if (path_.empty()) {
      if (navigator_.planNewRoute(position, player.position.z, path_, error)) {
        lastPlanAt_ = now;
        stuck_.resetRoute();
        Logger::instance().info("Engine: planned a route with " + std::to_string(path_.size()) + " points");
      } else {
        lastError_ = error;
        releaseMovement();
        retryRouteAt_=now+2;
        enterState(EngineState::Blocked, "waiting to replan: " + error);
        return;
      }
    } else {
      // MoveController advances the route while preserving corner continuity.
      lastPlanAt_ = now;
    }
  }

  const MovementIntent intent = move_.evaluate(path_, position, player.yaw, player.position.z);
  // Resume from the actual post-fight view, not a stored pre-fight angle.
  double blend=std::clamp((now-combatExitAt_)/.65,0.0,1.0);
  blend=blend*blend*(3-2*blend);
  const double aimYaw = wrapAngle(combatExitYaw_+wrapAngle(intent.aimYaw-combatExitYaw_)*blend);
  const double counts = look_.update(aimYaw, tickDt_);
  input_->turn(counts);
  if(player.radiansPerMouseCount>0&&std::abs(player.pitch)>.15*kPi/180)
    input_->pitch(std::clamp(-player.pitch*(1-std::exp(-4*tickDt_)),
      -45*kPi/180*tickDt_,45*kPi/180*tickDt_)/player.radiansPerMouseCount);

  const double aimErrorDeg = look_.errorDeg();
  const bool aimConverged = intent.forward || intent.strafe!=0;
  input_->setWalk(intent.forward);
  input_->setBackward(false);
  input_->setStrafe(intent.strafe);

  const double remaining = path_.empty() ? 0.0 : path_.length();
  const double speed = player.velocity.length();
  if(recoveryAttempt_ && distance(position,recoveryReference_)>180) recoveryAttempt_=0;
  const StuckReport report = stuck_.update(now, remaining, speed, aimConverged, aimErrorDeg);

  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    snapshot_.player = player;
    snapshot_.havePlayer = true;
    snapshot_.pathPoints = path_.size();
    snapshot_.targetIndex = intent.targetIndex;
    snapshot_.remainingDistance = remaining;
    snapshot_.aimErrorDeg = aimErrorDeg;
    snapshot_.recoveryAttempt = recoveryAttempt_;
  }

  if (intent.arrived) {
    releaseMovement();
    path_.clear();
    navigator_.clearGoal();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    status_ = "destination reached, choosing a new goal";
    return;
  }

  if (report.recoveryDue) {
    handleStuck(now, player, speed, aimConverged, aimErrorDeg);
  }
}

void Engine::handleStuck(double now, const PlayerState& player, double speed, bool aimConverged,
                         double aimErrorDeg) {
  (void)speed;
  (void)aimConverged;
  (void)aimErrorDeg;
  (void)player;
  ++recoveryAttempt_;
  recoveryReference_=player.position2();
  if (recoveryAttempt_ > config_.recoveryAttempts) {
    releaseMovement();path_.clear();navigator_.clearGoal();stuck_.resetRoute();
    retryRouteAt_=now+3;recoveryAttempt_=0;
    enterState(EngineState::Blocked,"recovery cooldown; will choose another route");
    return;
  }
  enterRecovery(now, "stuck; running recovery manoeuvre " + std::to_string(recoveryAttempt_));
}

void Engine::enterRecovery(double now, const std::string& reason) {
  releaseMovement();
  recoveryStartedAt_ = now;
  enterState(EngineState::Recovering, reason);
}

void Engine::enterDead(const std::string& reason) {
  radarPursuit_.reset();radarGoal_.reset();radarRoute_=false;
  awareness_.reset();combatExitAt_=-1000;havePositionReference_=false;
  loadoutPending_=true;loadoutAt_=-1;loadoutAttempts_=0;combat_.reset();
  releaseMovement();
  if (input_ != nullptr) input_->releaseAll();
  path_.clear();
  navigator_.clearGoal();
  stuck_.resetRoute();
  enterState(EngineState::Dead, reason);
}

void Engine::handleRespawnTick(double now, const PlayerState& player) {
  look_.reset();
  look_.setMeasuredYaw(player.yaw);
  lastKnownYaw_ = player.yaw;
  if (respawnCountdown_ > 0) {
    --respawnCountdown_;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    status_ = "confirming position after respawn";
    return;
  }
  path_.clear();
  navigator_.clearGoal();
  recoveryAttempt_ = 0;
  lastPlanAt_ = now;
  enterState(EngineState::Navigating, "respawned; planning a new route");
}

void Engine::publishAndSleep(double now, double tickInterval, double& nextDeadline) {
  maxTickWorkMs_=std::max(maxTickWorkMs_,(monotonicSeconds()-now)*1000);
  std::unique_lock<std::recursive_mutex> guard(mutex_);
  {
    EngineSnapshot copy;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      copy = snapshot_;
    }
    copy.state = state_.load();
    copy.status = status_;
    copy.stuckCount = stuck_.stuckCount();
    copy.controlHz = controlHz_;
    copy.providerHz = providerHz_;
    copy.lastError = lastError_;
    copy.tick = tickCounter_++;
    if(running_ && now-progressAt_>=1) {
      progressAt_=now;
      Logger::instance().info(format("timing gap_max_ms=%.3f work_max_ms=%.3f gaps_over_16ms=%u",maxTickGapMs_,maxTickWorkMs_,lateTicks_));
      maxTickGapMs_=0;maxTickWorkMs_=0;lateTicks_=0;
      Logger::instance().info(format("pose state=%s xyz=%.1f,%.1f,%.1f yaw=%.1f speed=%.1f remaining=%.1f aim=%.1f hz=%.1f combat=%d target=%u error=%.2f weapon=%d ammo=%d",engineStateName(copy.state),copy.player.position.x,copy.player.position.y,copy.player.position.z,copy.player.yaw*180/kPi,copy.player.velocity.length(),copy.remainingDistance,copy.aimErrorDeg,copy.controlHz,copy.combatActive,copy.combatTarget,copy.combatError,copy.player.weapon.item,copy.player.weapon.ammo));
    }
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      snapshot_ = copy;
    }
  }

  guard.unlock();
  nextDeadline += tickInterval;
  const double wake = monotonicSeconds();  // caller already sampled the clock this tick
  if (nextDeadline < wake) nextDeadline = wake;  // fell badly behind; resync
  const double sleepSeconds = nextDeadline - wake;
  if (sleepSeconds > 0.0) {
    std::this_thread::sleep_for(std::chrono::duration<double>(sleepSeconds));
  }
}

void Engine::loop() {
  const double tickInterval = 1.0 / std::max(1.0, config_.controlHz);
  double next = monotonicSeconds();

  while (!done_.load()) {
    const double now = monotonicSeconds();
    std::unique_lock<std::recursive_mutex> tickLock(mutex_);
    if(running_ && previousTick_>0){
      const double gap=(now-previousTick_)*1000;
      maxTickGapMs_=std::max(maxTickGapMs_,gap);
      if(gap>16)++lateTicks_;
    }
    tickDt_=previousTick_>0?std::clamp(now-previousTick_,.001,.025):tickInterval;previousTick_=now;
    ++controlTicks_;
    if (controlWindowStart_ < 0.0) controlWindowStart_ = now;
    // The rate is published continuously, not only when a window closes: a run
    // shorter than a second must still show a believable frequency.
    {
      const double elapsed = now - controlWindowStart_;
      if (elapsed >= 1.0) {
        controlHz_ = controlTicks_ / elapsed;
        controlTicks_ = 0.0;
        controlWindowStart_ = now;
      } else if (elapsed > 0.2) {
        controlHz_ = controlTicks_ / elapsed;
      }
    }

    if (emergency_.load() || (running_.load() && input_ && input_->stopRequested())) {
      applyEmergencyStop("emergency stop engaged");
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }



    PlayerState player;
    const bool observedPlayer=running_&&pollPlayer(player);
    if(running_) {
      const bool held=observerPause_.update(observedPlayer&&player.observersValid,player.spectators,now);
      if(held) {
        if(input_)input_->releaseAll();
        combat_.reset();look_.reset();stuck_.resetRoute();
        observerHeld_=true;
        enterState(EngineState::Paused,"spectator watching; waiting for confirmed departure");
        tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
      }
      if(observerHeld_) {
        observerHeld_=false;path_.clear();navigator_.clearGoal();havePositionReference_=false;
        lastDataAt_=now;
        enterState(paused_?EngineState::Paused:EngineState::WaitingForData,
                   paused_?"paused by operator":"spectators left; resuming");
      }
    }



    if (!running_.load()) {
      releaseMovement();
      if (state_.load() != EngineState::Idle && state_.load() != EngineState::Error) {
        enterState(EngineState::Idle, "engine not running");
      }
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }

    if (paused_.load() && state_.load() != EngineState::Paused) {
      enterState(EngineState::Paused, "paused");
    }
    if (state_.load() == EngineState::Paused) {
      releaseMovement();
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }

    const auto setup=provider_->matchSetup();
    if (!maps_.empty() && !selectMap(setup.valid ? setup.mapName : "")) {
      releaseMovement();
      tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
    }
    // Loading/team-selection UI may ignore binds. A confirmed team transition
    // starts a fresh bounded attempt budget, including automatic CT assignment.
    if(setup.valid && setup.team!=joinObservedTeam_) {
      joinObservedTeam_=setup.team;joinAttempts_=0;joinAt_=-1000;
      loadoutPending_=true;loadoutAttempts_=0;loadoutAt_=-1;
    }
    if(setup.valid && setup.team!=2 && input_) {
      releaseMovement();
        if(!input_->loadoutCommandsAvailable())status_=input_->loadoutUnavailableReason();
      else if(joinAttempts_>=3)status_="T selection not confirmed; press F8 twice to retry";
      else if(now-joinAt_>=8) {
        input_->joinTerrorists();joinAt_=now;++joinAttempts_;
        status_="selecting Terrorists; waiting for confirmation";
        Logger::instance().info("Setup: requested T side");
      }
      tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
    }
    if(setup.valid && setup.team==2)joinAttempts_=0;
    if(input_ && !input_->ready()) {
        releaseMovement();look_.reset();stuck_.resetRoute();awareness_.reset();lastDataAt_=now;
      status_=input_->unavailableReason();
      tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
    }
    const bool havePlayer = observedPlayer && (maps_.empty() || player.mapName == activeMap_);
    if (havePlayer) {
      lastDataAt_ = now;
      ++providerTicks_;
      if (providerWindowStart_ < 0.0) providerWindowStart_ = now;
      const double elapsed = now - providerWindowStart_;
      if (elapsed >= 1.0) {
        providerHz_ = providerTicks_ / elapsed;
        providerTicks_ = 0.0;
        providerWindowStart_ = now;
      } else if (elapsed > 0.2) {
        providerHz_ = providerTicks_ / elapsed;
      }
    }

    const EngineState current = state_.load();
    const double sinceData = lastDataAt_ < 0.0 ? 1.0e9 : now - lastDataAt_;
    if(current==EngineState::WaitingForData) {
      releaseMovement();
      if(havePlayer) {
        path_.clear();navigator_.clearGoal();stuck_.resetRoute();look_.reset();combat_.reset();
        havePositionReference_=false;respawnCountdown_=config_.respawnConfirmations;
        enterState(player.alive?EngineState::Respawning:EngineState::Dead,"game data restored");
      }
      tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
    }

    if (current == EngineState::Dead || current == EngineState::Respawning) {
      if (havePlayer && player.alive) {
        if (current == EngineState::Dead) {
          respawnCountdown_ = config_.respawnConfirmations;
          enterState(EngineState::Respawning, "player is alive again");
        } else {
          handleRespawnTick(now, player);
        }
      } else if (!havePlayer && sinceData > config_.dataLossTimeout) {
        releaseMovement();enterState(EngineState::WaitingForData,"waiting for game data while dead");
        lastError_ = "no game data while the player was dead";
      } else {
        releaseMovement();
      }
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }

    if (!havePlayer) {
      releaseMovement();
      if (sinceData > config_.dataLossTimeout) {
        enterState(EngineState::WaitingForData,"waiting for trustworthy game data; automatic resume");
        lastError_ = provider_->diagnostic();
      } else {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        status_ = "waiting for a trustworthy player state";
        snapshot_.havePlayer = false;
      }
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }

    if (!player.alive) {
      enterDead("player died");
      tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
      continue;
    }

    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      snapshot_.player = player;
      snapshot_.havePlayer = true;
    }

    if(loadoutPending_ && player.team==2 && current==EngineState::Navigating && input_->loadoutCommandsAvailable()) {
      releaseMovement();
      if(player.weapon.valid && player.weapon.item==7) {
        // Still disable random DM weapons once per spawn through the loadout bind.
        if(loadoutAttempts_==0)input_->buyLoadout();
        loadoutPending_=false;status_="AK-47 confirmed";
        Logger::instance().info("Setup: AK-47 confirmed");
      } else if(loadoutAt_<0 || now-loadoutAt_>=1.5) {
        if(loadoutAttempts_>=3) {
          loadoutPending_=false;lastError_="AK-47 not confirmed: check loadout slot / buy zone / F7 config";
          Logger::instance().error(lastError_);
        } else {input_->buyLoadout();loadoutAt_=now;++loadoutAttempts_;status_="buying AK-47; waiting for weapon confirmation";}
      } else if(now-loadoutAt_>.35 && now-lastSelectAt_>.5){input_->selectPrimary();lastSelectAt_=now;}
      tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
    }
    if(player.weapon.valid && !CombatController::firearm(player.weapon.item) && now-lastSelectAt_>1) {
      input_->selectPrimary();lastSelectAt_=now;
    }
    if(current==EngineState::Navigating||current==EngineState::Recovering||current==EngineState::Blocked) {
      const bool engaged=handleCombatTick(now,player);
      const auto radar=radarObserver_?radarObserver_->latest():RadarHints{};
      radarGoal_=combatEnabled_?radarPursuit_.update(player,radar,now,engaged):std::nullopt;
      // A confirmed contact drives the route; do not repeatedly stop walking
      // for the old bearing-only glance at that same mark.
      const auto attention=awareness_.update(player,audio_?audio_->latest():SoundHint{},radarGoal_?RadarHints{}:radar,engaged,now,combatMesh_);
      if(engaged) {tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;}
      if(attention.active&&combatEnabled_) {
        releaseMovement();look_.setMeasuredYaw(player.yaw);
        auto c=look_.config();if(player.radiansPerMouseCount>0)c.radiansPerMouseCount=player.radiansPerMouseCount;look_.setConfig(c);
        input_->turn(look_.update(attention.yaw,tickDt_));status_=attention.reason;
        combatExitAt_=now;combatExitYaw_=player.yaw;stuck_.resetRoute();
        tickLock.unlock();publishAndSleep(now,tickInterval,next);continue;
      }
    }
    switch (current) {
      case EngineState::Idle:
        releaseMovement();
        enterState(EngineState::Navigating, "starting navigation");
        break;

      case EngineState::Navigating:
        handleNavigationTick(now, player);
        break;

      case EngineState::Recovering: {
        look_.setMeasuredYaw(player.yaw);
        const double elapsed = now - recoveryStartedAt_;
        // Strafe away from whatever we are wedged against, without walking.
        const int side = (recoveryAttempt_ % 2 == 0) ? -1 : 1;
        input_->setWalk(false);
        input_->setBackward(elapsed<.25);
        input_->setStrafe(elapsed < config_.strafeRecoverySeconds ? side : 0);
        if (elapsed >= config_.strafeRecoverySeconds) {
          releaseMovement();look_.reset();
            radarRoute_=false;
          std::string error;
          path_.clear();
          if (navigator_.planNewRoute(player.position.xy(), player.position.z, path_, error)) {
            stuck_.resetRoute();
            lastPlanAt_ = now;
            enterState(EngineState::Navigating, "recovered; new route planned");
          } else {
            lastError_ = error;
            retryRouteAt_=now+2;enterState(EngineState::Blocked,"recovery route unavailable: "+error);
          }
        }
        break;
      }

      case EngineState::Blocked:
        releaseMovement();
        if(now>=retryRouteAt_) {
          path_.clear();navigator_.clearGoal();stuck_.resetRoute();look_.reset();
          enterState(EngineState::Navigating,"retrying route after cooldown");
        }
        break;
      case EngineState::WaitingForData:
      case EngineState::Paused:
      case EngineState::Error:
        releaseMovement();
        break;
    }

    tickLock.unlock();
      publishAndSleep(now, tickInterval, next);
  }

  releaseMovement();
  if (input_ != nullptr) input_->releaseAll();
}

}  // namespace cs2n
