// Smoothness of the camera controller is checked in closed loop: the controller
// drives a plant whose yaw is integrated from the mouse deltas it is handed, so
// these tests measure what a player would actually see, not just the raw output.
#include <cmath>
#include <vector>

#include "movement/look_controller.h"
#include "movement/native_turn.h"
#include <limits>
#include "tests/framework.h"

using namespace cs2n;
CS2N_TEST(NativeTurn_TracksRateAcrossTickSizesAndBoundsInvalidInput) {
  const double scale=.022*2.15*kPi/180;
  for(double dt:{.004,.007,.016,.025}) {
    const double counts=30*kPi/180*dt/scale;
    CHECK_NEAR(nativeTurnAxis(counts,scale,dt),.075,1e-9);
    CHECK_NEAR(nativeTurnAxis(-counts,scale,dt),-.075,1e-9);
    CHECK_NEAR(nativeTurnAxis(counts,scale,dt,60),.5,1e-9);
  }
  CHECK_NEAR(nativeTurnAxis(100000,scale,.007),1,1e-9);
  CHECK_NEAR(nativeTurnAxis(1,scale,0),0,1e-9);
  CHECK_NEAR(nativeTurnAxis(std::numeric_limits<double>::quiet_NaN(),scale,.007),0,1e-9);
}
CS2N_TEST(LookController_ContinuousOutputPreservesSubPixelMotion) {
  LookConfig c;c.mode=AimMode::Spring;c.quantizeCounts=false;
  LookController controller(c);controller.setMeasuredYaw(0);
  const double output=controller.update(.05*kPi/180,.007);
  CHECK_TRUE(output>0);CHECK_TRUE(output<1);
}

namespace {

struct Plant {
  double yaw = 0.0;
  double velocity = 0.0;  // rad/s, for reporting

  void apply(double counts, double radiansPerCount, double dt) {
    const double delta = counts * radiansPerCount;
    yaw = wrapAngle(yaw + delta);
    velocity = dt > 0.0 ? delta / dt : 0.0;
  }
};

struct Trace {
  double peakSpeedDeg = 0.0;
  double peakAccelerationDeg = 0.0;
  double overshootDeg = 0.0;
  double settleTime = -1.0;
  int signChanges = 0;
  bool monotoneAfterPeak = true;
};

// Runs the controller against the plant and measures what matters.
Trace runClosedLoop(LookController& controller, Plant& plant, double target, double seconds, double dt) {
  Trace trace;
  double previousSpeed = 0.0;
  double previousError = wrapAngle(target - plant.yaw);
  double peakSpeedTime = -1.0;
  int sign = previousError > 0 ? 1 : (previousError < 0 ? -1 : 0);
  const int initialSign = sign;
  bool crossedTarget = false;

  for (double now = 0.0; now < seconds; now += dt) {
    controller.setMeasuredYaw(plant.yaw);
    const double counts = controller.update(target, dt);
    plant.apply(counts, controller.config().radiansPerMouseCount, dt);

    const double speedDeg = std::fabs(controller.commandDegPerSecond());
    const double acceleration = std::fabs(controller.commandDegPerSecond() - previousSpeed) / dt;
    previousSpeed = controller.commandDegPerSecond();

    trace.peakSpeedDeg = std::max(trace.peakSpeedDeg, speedDeg);
    trace.peakAccelerationDeg = std::max(trace.peakAccelerationDeg, acceleration);

    const double errorDeg = std::fabs(wrapAngle(target - plant.yaw) * 180.0 / kPi);

    // Direction of travel through the error, to count ringing.
    const double currentError = wrapAngle(target - plant.yaw);
    const int currentSign = currentError > 0 ? 1 : (currentError < 0 ? -1 : 0);
    if (sign != 0 && currentSign != 0 && currentSign != sign) ++trace.signChanges;
    if (currentSign != 0 && currentSign != initialSign) crossedTarget = true;
    if (currentSign != 0) sign = currentSign;

    // Overshoot only means something once the target has been crossed: before
    // that the "error" is simply the distance still to travel.
    if (crossedTarget && errorDeg > trace.overshootDeg) trace.overshootDeg = errorDeg;

    if (peakSpeedTime < 0.0 && speedDeg < trace.peakSpeedDeg * 0.5) peakSpeedTime = now;
    if (peakSpeedTime >= 0.0 && errorDeg > std::fabs(previousError * 180.0 / kPi) + 1e-9) {
      trace.monotoneAfterPeak = false;
    }
    previousError = currentError;

    if (trace.settleTime < 0.0 && errorDeg < 0.5 && speedDeg < 5.0) trace.settleTime = now;
  }
  return trace;
}

}  // namespace

CS2N_TEST(LookController_ReachesTargetWithoutOvershoot) {
  LookConfig config;
  config.maxAngularSpeedDeg = 180.0;
  config.accelerationDeg = 800.0;
  config.decelerationDeg = 1000.0;
  LookController controller(config);
  Plant plant;
  plant.yaw = 0.0;

  const double target = 90.0 * kPi / 180.0;
  const Trace trace = runClosedLoop(controller, plant, target, 4.0, 1.0 / 250.0);

  CHECK_TRUE(trace.settleTime > 0.0);
  // A trapezoid with pre-braking must not swing past the target.
  CHECK_LT(trace.overshootDeg, 1.5);
  CHECK_LT(trace.signChanges, 3);
}

CS2N_TEST(LookController_ObeysAngularSpeedLimit) {
  LookConfig config;
  config.maxAngularSpeedDeg = 120.0;
  config.accelerationDeg = 5000.0;  // effectively instant ramp, so the cap binds
  config.decelerationDeg = 5000.0;
  LookController controller(config);
  Plant plant;

  const double target = 170.0 * kPi / 180.0;
  const Trace trace = runClosedLoop(controller, plant, target, 4.0, 1.0 / 250.0);

  CHECK_TRUE(trace.peakSpeedDeg <= config.maxAngularSpeedDeg + 5.0);
}

CS2N_TEST(LookController_AccelerationIsBounded) {
  LookConfig config;
  config.accelerationDeg = 700.0;
  config.decelerationDeg = 900.0;
  LookController controller(config);
  Plant plant;

  const double dt = 1.0 / 250.0;
  double previousSpeed = 0.0;
  double peakAcceleration = 0.0;
  for (int i = 0; i < 1000; ++i) {
    controller.setMeasuredYaw(plant.yaw);
    plant.apply(controller.update(120.0 * kPi / 180.0, dt), config.radiansPerMouseCount, dt);
    const double speed = controller.commandDegPerSecond();
    peakAcceleration = std::max(peakAcceleration, std::fabs(speed - previousSpeed) / dt);
    previousSpeed = speed;
  }

  // The measured slew must stay near the configured limit. A step command would
  // produce thousands of degrees per second squared, which is the "jerky" bug.
  CHECK_LT(peakAcceleration, config.accelerationDeg * 2.5);
}

CS2N_TEST(LookController_HandlesWrapAround) {
  LookConfig config;
  LookController controller(config);
  Plant plant;
  // Sitting just below +pi, asked to aim just above -pi: the short way round is
  // a small clockwise turn, not a full lap.
  plant.yaw = 179.0 * kPi / 180.0;
  const double target = -179.0 * kPi / 180.0;

  double travelled = 0.0;
  for (int i = 0; i < 250; ++i) {
    controller.setMeasuredYaw(plant.yaw);
    const double counts = controller.update(target, 1.0 / 250.0);
    travelled += counts * controller.config().radiansPerMouseCount;
    plant.apply(counts, controller.config().radiansPerMouseCount, 1.0 / 250.0);
  }

  CHECK_NEAR(wrapAngle(plant.yaw - target), 0.0, 0.02);
  // Total motion should be about 2 degrees, never close to a full revolution.
  CHECK_LT(std::fabs(travelled), 10.0 * kPi / 180.0);
}

CS2N_TEST(LookController_SmallErrorsSettleQuietly) {
  LookConfig config;
  LookController controller(config);
  Plant plant;
  plant.yaw = 0.0;
  const double target = 1.0 * kPi / 180.0;

  for (int i = 0; i < 400; ++i) {
    controller.setMeasuredYaw(plant.yaw);
    plant.apply(controller.update(target, 1.0 / 250.0), config.radiansPerMouseCount, 1.0 / 250.0);
  }
  CHECK_NEAR(wrapAngle(plant.yaw - target), 0.0, 0.3);
  CHECK_TRUE(controller.settled(0.5));
}

CS2N_TEST(LookController_BrokenTimestepIsIgnored) {
  LookController controller(LookConfig{});
  controller.setMeasuredYaw(0.0);
  // A huge dt must not translate into a huge mouse jump.
  const double counts = controller.update(90.0 * kPi / 180.0, 5.0);
  CHECK_NEAR(counts, 0.0, 1e-9);
}

CS2N_TEST(LookController_TracksMovingAngleWithoutSteadyLag) {
  LookConfig config;config.mode=AimMode::Spring;config.quantizeCounts=false;
  config.springNaturalFreq=28;config.maxAngularSpeedDeg=400;config.accelerationDeg=7000;
  LookController controller(config);double yaw=0;const double dt=1./144;
  for(int i=0;i<288;++i) {
    const double target=wrapAngle(i*dt*.6);
    controller.setMeasuredYaw(yaw);
    if(i>144)CHECK_LT(std::abs(wrapAngle(target-yaw)),.002);
    yaw=wrapAngle(yaw+controller.update(target,dt,.6)*config.radiansPerMouseCount);
  }
  for(int i=0;i<144;++i) {
    controller.setMeasuredYaw(yaw);
    yaw=wrapAngle(yaw+controller.update(2*.6,dt,0)*config.radiansPerMouseCount);
  }
  CHECK_NEAR(yaw,1.2,.002);
}

CS2N_TEST(LookController_SpringModeAlsoConverges) {
  LookConfig config;
  config.mode = AimMode::Spring;
  config.springNaturalFreq = 10.0;
  config.springDamping = 1.0;
  LookController controller(config);
  Plant plant;
  const double target = 60.0 * kPi / 180.0;
  const Trace trace = runClosedLoop(controller, plant, target, 5.0, 1.0 / 250.0);
  CHECK_TRUE(trace.settleTime > 0.0);
  CHECK_NEAR(wrapAngle(plant.yaw - target), 0.0, 0.05);
}
