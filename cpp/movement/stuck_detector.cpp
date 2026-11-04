#include "movement/stuck_detector.h"

#include <algorithm>
#include <cmath>

namespace cs2n {

void StuckDetector::resetRoute() {
  haveLastUpdate_ = false;
  haveReference_ = false;
  lastDistance_ = 0.0;
  referenceDistance_ = 0.0;
  stalledSeconds_ = 0.0;
  stoppedSeconds_ = 0.0;
  aimStalledSeconds_ = 0.0;
  inStuck_ = false;
}

void StuckDetector::resetAll() {
  resetRoute();
  stuckCount_ = 0;
  lastUpdate_ = 0.0;
}

StuckReport StuckDetector::update(double now, double distanceToGoal, double speed, bool aimConverged,
                                  double aimErrorDeg) {
  StuckReport report;
  (void)aimErrorDeg;  // reserved for a finer-grained "spinning in place" check

  // The first sample only establishes the time base.
  if (!haveLastUpdate_) {
    haveLastUpdate_ = true;
    lastUpdate_ = now;
    lastDistance_ = distanceToGoal;
    referenceDistance_ = distanceToGoal;
    haveReference_ = true;
    return report;
  }

  double dt = now - lastUpdate_;
  lastUpdate_ = now;
  if (dt <= 0.0) return report;

  // A long gap (paused game, suspended process, debugger break) is a
  // discontinuity rather than evidence of a stall: re-anchor every clock and
  // stay silent, so nothing accumulates against the recovery budget.
  if (dt > kMaxPlausibleTickSeconds) {
    referenceDistance_ = distanceToGoal;
    haveReference_ = true;
    stalledSeconds_ = 0.0;
    stoppedSeconds_ = 0.0;
    aimStalledSeconds_ = 0.0;
    inStuck_ = false;
    lastDistance_ = distanceToGoal;
    report.stuckCount = stuckCount_;
    return report;
  }

  // Progress: has the remaining distance shrunk by a meaningful amount since we
  // last re-referenced? Getting closer to the goal resets the stall clock.
  if (!haveReference_) {
    referenceDistance_ = distanceToGoal;
    haveReference_ = true;
  }
  const double progress = referenceDistance_ - distanceToGoal - config_.goalReachSlack;
  if (progress >= config_.minProgress) {
    referenceDistance_ = distanceToGoal;
    stalledSeconds_ = 0.0;
  } else {
    stalledSeconds_ += dt;
  }

  if (std::isfinite(speed) && speed >= 0.0 && speed < config_.stoppedSpeed) {
    stoppedSeconds_ += dt;
  } else {
    stoppedSeconds_ = 0.0;
  }

  if (!aimConverged) {
    aimStalledSeconds_ += dt;
  } else {
    aimStalledSeconds_ = 0.0;
  }

  if (stoppedSeconds_ >= config_.stoppedSeconds) {
    report.event = StuckEvent::Stopped;
    report.stalledFor = stoppedSeconds_;
  } else if (stalledSeconds_ >= config_.noProgressSeconds) {
    report.event = StuckEvent::NoProgress;
    report.stalledFor = stalledSeconds_;
  } else if (aimStalledSeconds_ >= config_.turnTimeoutSeconds) {
    report.event = StuckEvent::TurnTimeout;
    report.stalledFor = aimStalledSeconds_;
  }

  // The event stays reported for as long as the condition holds, which is what
  // the operator wants to see. Recovery, though, is budgeted per episode: the
  // edge flag tells the engine to start one recovery manoeuvre, not a new one
  // on every tick of the same stall.
  const bool risingEdge = report.event != StuckEvent::None && !inStuck_;
  report.recoveryDue = risingEdge;
  if (risingEdge) {
    inStuck_ = true;
    ++stuckCount_;
  } else if (report.event == StuckEvent::None) {
    inStuck_ = false;
  }

  lastDistance_ = distanceToGoal;
  report.progressSinceReset = haveReference_ ? std::max(0.0, referenceDistance_ - distanceToGoal) : 0.0;
  report.stuckCount = stuckCount_;
  return report;
}

}  // namespace cs2n
