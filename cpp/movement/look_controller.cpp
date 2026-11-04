#include "movement/look_controller.h"

#include <algorithm>
#include <cmath>

namespace cs2n {
namespace {

inline double radians(double deg) { return deg * kPi / 180.0; }
inline double degrees(double rad) { return rad * 180.0 / kPi; }

}  // namespace

bool LookConfig::validate(std::string& error) const {
  if (maxAngularSpeedDeg <= 0.0) { error = "maxAngularSpeedDeg must be positive"; return false; }
  if (accelerationDeg <= 0.0) { error = "accelerationDeg must be positive"; return false; }
  if (decelerationDeg <= 0.0) { error = "decelerationDeg must be positive"; return false; }
  if (radiansPerMouseCount <= 0.0) { error = "radiansPerMouseCount must be positive"; return false; }
  if (deadbandDeg < 0.0 || snapDeg < 0.0) { error = "deadband and snap must not be negative"; return false; }
  if (springNaturalFreq <= 0.0) { error = "springNaturalFreq must be positive"; return false; }
  if (springDamping <= 0.0) { error = "springDamping must be positive"; return false; }
  return true;
}

void LookController::setMeasuredYaw(double yawRadians) { measuredYaw_ = wrapAngle(yawRadians); }

void LookController::reset() {
  commandDegPerSecond_ = 0.0;
  accumulatedCounts_ = 0.0;
}

bool LookController::settled(double toleranceDeg) const {
  return std::fabs(errorDeg()) <= toleranceDeg && std::fabs(commandDegPerSecond_) < 1.0;
}

double LookController::update(double targetYaw, double dt, double targetVelocityRad) {
  targetYaw_ = wrapAngle(targetYaw);
  if (dt <= 0.0 || dt > 0.5) {
    // A broken time step must not produce a huge jump; drop the command instead.
    commandDegPerSecond_ = 0.0;
    accumulatedCounts_ = 0.0;
    return 0.0;
  }

  const double errorRad = wrapAngle(targetYaw_ - measuredYaw_);
  if(!std::isfinite(targetVelocityRad)) targetVelocityRad=0;
  targetVelocityRad=clamp(targetVelocityRad,-radians(config_.maxAngularSpeedDeg),radians(config_.maxAngularSpeedDeg));
  const double command = config_.mode == AimMode::Spring ? springStep(errorRad, dt, targetVelocityRad) : trapezoidStep(errorRad, dt);
  commandDegPerSecond_ = command;
  if(!config_.quantizeCounts){
    accumulatedCounts_=0;
    return command*kPi/180.0*dt/config_.radiansPerMouseCount;
  }

  // Convert angular velocity to mouse counts, keeping the fractional remainder.
  accumulatedCounts_ += command * kPi / 180.0 * dt / config_.radiansPerMouseCount;
  const double counts = std::trunc(accumulatedCounts_);
  accumulatedCounts_ -= counts;
  return counts;
}

double LookController::trapezoidStep(double errorRad, double dt) {
  const double errorDegValue = degrees(errorRad);
  const double magnitude = std::fabs(errorDegValue);

  // Inside the deadband the target speed is zero, but it is reached by braking,
  // never by snapping: an instantaneous zero would be an acceleration spike,
  // which is exactly the "jerky camera" defect this controller exists to fix.
  double desired = 0.0;
  double direction = 0.0;
  if (magnitude > config_.deadbandDeg) {
    direction = errorDegValue > 0.0 ? 1.0 : -1.0;
    // Pre-braking: the speed from which we could still stop within the distance
    // that remains outside the deadband.
    const double stoppingDistance = std::max(0.0, magnitude - config_.snapDeg);
    const double brakeSpeed = std::sqrt(2.0 * config_.decelerationDeg * stoppingDistance);
    desired = std::min(config_.maxAngularSpeedDeg, brakeSpeed);
  }

  const double limit = desired > std::fabs(commandDegPerSecond_) ? config_.accelerationDeg : config_.decelerationDeg;
  const double maximumChange = limit * dt;
  const double delta = clamp(desired * direction - commandDegPerSecond_, -maximumChange, maximumChange);
  return commandDegPerSecond_ + delta;
}

double LookController::springStep(double errorRad, double dt, double targetVelocityRad) {
  // Critically damped second order response: acceleration = w^2*e - 2*z*w*v.
  // Both the error and the velocity are integrated in radians; mixing degrees
  // into the damping term multiplies it by 57 and stalls the response.
  const double omega = config_.springNaturalFreq;
  const double zeta = config_.springDamping;
  const double velocityRad = commandDegPerSecond_ * kPi / 180.0;
  const double desiredAcceleration = omega * omega * errorRad - 2.0 * zeta * omega * (velocityRad-targetVelocityRad);

  double limit=radians(config_.accelerationDeg);
  double nextVelocity = velocityRad + clamp(desiredAcceleration,-limit,limit)*dt;
  const double cap = radians(config_.maxAngularSpeedDeg);
  nextVelocity = clamp(nextVelocity, -cap, cap);
  return degrees(nextVelocity);
}

}  // namespace cs2n
