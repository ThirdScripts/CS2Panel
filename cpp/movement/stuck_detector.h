// Stuck detection based on real progress along the route.
//
// Image-derived heuristics are not enough: the prototype only noticed trouble
// after a fixed wall-clock timeout regardless of what the player was doing.
// Here progress is measured as monotonic reduction of the remaining distance to
// the goal, combined with actual speed. That distinguishes "slow but moving"
// from "grinding into a wall", which is what recovery should key on.
#pragma once

#include "core/types.h"

namespace cs2n {

enum class StuckEvent {
  None = 0,
  NoProgress,   // moving too slowly relative to the remaining distance
  Stopped,      // velocity essentially zero for too long
  TurnTimeout,  // the view failed to converge on the aim target in time
};

struct StuckConfig {
  double noProgressSeconds = 3.0;    // time allowed without measurable progress
  double stoppedSeconds = 2.5;       // time allowed with zero velocity
  double minProgress = 16.0;         // metres of route progress that resets the timer
  double stoppedSpeed = 6.0;         // below this, treat the player as stopped
  double turnTimeoutSeconds = 6.0;   // allowed time to converge on a new heading
  double goalReachSlack = 48.0;      // progress measured to this margin around the goal
};

struct StuckReport {
  StuckEvent event = StuckEvent::None;
  double stalledFor = 0.0;      // seconds the current condition has held
  double progressSinceReset = 0.0;
  int stuckCount = 0;           // cumulative recoveries triggered in this run
  bool recoveryDue = false;     // rising edge: one recovery manoeuvre should start now
};

class StuckDetector {
 public:
  StuckDetector() = default;
  explicit StuckDetector(const StuckConfig& config) : config_(config) {}

  void setConfig(const StuckConfig& config) { config_ = config; }

  // Clears per-route state. Call on every replan or respawn.
  void resetRoute();

  // Clears everything, including the cumulative counter.
  void resetAll();

  // Feed one tick. `distanceToGoal` is the remaining route distance, `speed`
  // the measured velocity magnitude, `aimConverged` whether the view is roughly
  // on target and `aimErrorDeg` how far off it is.
  StuckReport update(double now, double distanceToGoal, double speed, bool aimConverged, double aimErrorDeg);

  [[nodiscard]] int stuckCount() const { return stuckCount_; }
  [[nodiscard]] double secondsStalled() const { return stalledSeconds_; }

 private:
  // Anything longer than this between two samples is a discontinuity (pause,
  // suspend, debugger), not a stall; it re-anchors the clocks instead.
  static constexpr double kMaxPlausibleTickSeconds = 1.0;

  StuckConfig config_;
  double lastDistance_ = 0.0;
  double referenceDistance_ = 0.0;
  double stalledSeconds_ = 0.0;
  double stoppedSeconds_ = 0.0;
  double aimStalledSeconds_ = 0.0;
  double lastUpdate_ = 0.0;
  int stuckCount_ = 0;
  bool inStuck_ = false;
  bool haveLastUpdate_ = false;
  bool haveReference_ = false;
};

}  // namespace cs2n
