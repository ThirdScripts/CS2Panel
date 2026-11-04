#include "movement/stuck_detector.h"
#include "tests/framework.h"

using namespace cs2n;

CS2N_TEST(StuckDetector_SilenceWhileProgressing) {
  StuckConfig config;
  config.noProgressSeconds = 2.0;
  config.minProgress = 10.0;
  StuckDetector detector(config);

  // Remaining distance shrinks steadily: nothing should fire.
  StuckReport report;
  double distance = 1000.0;
  for (int i = 0; i < 100; ++i) {
    distance -= 20.0;
    report = detector.update(i * 0.1, distance, 250.0, true, 0.0);
  }
  CHECK_EQ(static_cast<int>(report.event), static_cast<int>(StuckEvent::None));
  CHECK_EQ(report.stuckCount, 0);
}

CS2N_TEST(StuckDetector_FiresWhenProgressStalls) {
  StuckConfig config;
  config.noProgressSeconds = 2.0;
  config.minProgress = 10.0;
  StuckDetector detector(config);

  StuckReport report;
  for (int i = 0; i < 40; ++i) {
    // The player keeps reporting movement, but the remaining distance never
    // shrinks: grinding into a wall.
    report = detector.update(i * 0.1, 500.0, 250.0, true, 0.0);
  }
  CHECK_EQ(static_cast<int>(report.event), static_cast<int>(StuckEvent::NoProgress));
  CHECK_GT(report.stalledFor, 2.0);
  CHECK_EQ(report.stuckCount, 1);
}

CS2N_TEST(StuckDetector_FiresWhenStopped) {
  StuckConfig config;
  config.stoppedSeconds = 1.5;
  config.stoppedSpeed = 5.0;
  StuckDetector detector(config);

  StuckReport report;
  for (int i = 0; i < 30; ++i) report = detector.update(i * 0.1, 500.0, 0.0, true, 0.0);
  CHECK_EQ(static_cast<int>(report.event), static_cast<int>(StuckEvent::Stopped));
  CHECK_EQ(report.stuckCount, 1);
}

CS2N_TEST(StuckDetector_CountsRecoveriesOncePerEpisode) {
  StuckConfig config;
  config.stoppedSeconds = 1.0;
  StuckDetector detector(config);

  int episodes = 0;
  double now = 0.0;
  double distance = 500.0;
  for (int episode = 0; episode < 3; ++episode) {
    // Stall, then move again to clear the condition. Progress is measured as
    // shrinking distance to the goal, so each episode resumes where the
    // previous one advanced to; a jump back to a longer route is a replan,
    // which the engine reports through resetRoute().
    for (int i = 0; i < 15; ++i, now += 0.1) {
      const StuckReport report = detector.update(now, distance, 0.0, true, 0.0);
      // The event itself stays reported while the stall lasts; recovery is
      // budgeted on the rising edge only.
      if (report.recoveryDue) ++episodes;
    }
    for (int i = 0; i < 15; ++i, now += 0.1) {
      distance -= 8.0;
      detector.update(now, distance, 250.0, true, 0.0);
    }
  }
  // Three stalls, but the counter tracks recoveries, which is what the engine
  // budgets against.
  CHECK_EQ(episodes, 3);
  CHECK_EQ(detector.stuckCount(), 3);
}

CS2N_TEST(StuckDetector_ResetRouteClearsStallButKeepsCounter) {
  StuckConfig config;
  config.stoppedSeconds = 1.0;
  StuckDetector detector(config);

  for (int i = 0; i < 15; ++i) detector.update(i * 0.1, 500.0, 0.0, true, 0.0);
  CHECK_EQ(detector.stuckCount(), 1);

  detector.resetRoute();
  CHECK_EQ(detector.stuckCount(), 1);       // history kept
  CHECK_NEAR(detector.secondsStalled(), 0.0, 1e-9);  // stall clock cleared

  detector.resetAll();
  CHECK_EQ(detector.stuckCount(), 0);
}

CS2N_TEST(StuckDetector_AimTimeoutFires) {
  StuckConfig config;
  config.turnTimeoutSeconds = 2.0;
  StuckDetector detector(config);

  StuckReport report;
  for (int i = 0; i < 30; ++i) {
    // Moving fine, but the view never converges on the target.
    report = detector.update(i * 0.1, 400.0 - i * 5.0, 250.0, false, 80.0);
  }
  CHECK_EQ(static_cast<int>(report.event), static_cast<int>(StuckEvent::TurnTimeout));
}

CS2N_TEST(StuckDetector_LongGapIsNotAStall) {
  StuckConfig config;
  config.stoppedSeconds = 1.0;
  StuckDetector detector(config);

  detector.update(0.0, 500.0, 0.0, true, 0.0);
  // A five second gap (paused game, suspended process) must not count as stalled.
  const StuckReport report = detector.update(5.0, 500.0, 0.0, true, 0.0);
  CHECK_EQ(static_cast<int>(report.event), static_cast<int>(StuckEvent::None));
}
