// Smooth camera aiming.
//
// The Python prototype issued one clamped proportional mouse delta every ~100 ms,
// which is exactly why the camera stepped. Here the aim loop runs at a high,
// fixed rate and the commanded angular velocity follows a trapezoidal profile:
// it ramps up under an acceleration limit, is capped by a maximum angular
// speed, and is pre-braked so the turn decelerates into the target instead of
// overshooting. Fractional mouse counts accumulate internally, so no command is
// lost to rounding.
//
// The 0/360 degree seam is handled by always working with the shortest signed
// rotation, so a target that sits just across the wrap point still turns the
// short way round.
#pragma once

#include <string>

#include "core/types.h"

namespace cs2n {

enum class AimMode {
  Trapezoid,  // accel-limited trapezoid with pre-braking (default)
  Spring,     // critically damped second order response, soft arrival
};

struct LookConfig {
  bool quantizeCounts = true;
  AimMode mode = AimMode::Trapezoid;

  double maxAngularSpeedDeg = 210.0;   // hard cap on turn rate
  double accelerationDeg = 900.0;      // how fast the turn rate itself ramps
  double decelerationDeg = 1200.0;     // braking is stronger than acceleration
  double radiansPerMouseCount = 7.68e-4;  // yaw per mouse pixel; calibrate per sensitivity
  double deadbandDeg = 0.15;           // stop chasing sub-pixel error
  double snapDeg = 0.05;               // below this the command is exactly zero

  // Spring mode only.
  double springNaturalFreq = 12.0;  // rad/s
  double springDamping = 1.0;       // 1.0 == critically damped

  [[nodiscard]] bool validate(std::string& error) const;
};

class LookController {
 public:
  LookController() = default;
  explicit LookController(const LookConfig& config) : config_(config) {}

  void setConfig(const LookConfig& config) { config_ = config; }
  [[nodiscard]] const LookConfig& config() const { return config_; }

  // Tells the controller where the view currently points. Call whenever a fresh
  // measurement arrives; the controller resynchronises without jumping.
  void setMeasuredYaw(double yawRadians);

  [[nodiscard]] double measuredYaw() const { return measuredYaw_; }

  // Advances the controller by dt seconds towards targetYaw and returns the
  // mouse delta in counts (pixels) to send now. Positive turns right in the
  // radar frame, matching the mouse convention used by the input backend.
  double update(double targetYaw, double dt, double targetVelocityRad = 0);

  // Re-zeros internal state without moving the view.
  void reset();

  [[nodiscard]] bool settled(double toleranceDeg = 0.5) const;

  // Diagnostics for the UI and tests.
  [[nodiscard]] double commandDegPerSecond() const { return commandDegPerSecond_; }
  [[nodiscard]] double errorDeg() const { return wrapAngle(targetYaw_ - measuredYaw_) * 180.0 / kPi; }

 private:
  double trapezoidStep(double errorRad, double dt);
  double springStep(double errorRad, double dt, double targetVelocityRad);

  LookConfig config_;
  double measuredYaw_ = 0.0;
  double targetYaw_ = 0.0;
  double commandDegPerSecond_ = 0.0;   // smoothed angular velocity actually sent
  double accumulatedCounts_ = 0.0;     // fractional mouse counts not yet sent
};

}  // namespace cs2n
