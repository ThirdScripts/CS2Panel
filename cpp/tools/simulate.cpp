// Navigation simulator: drives the engine over a mesh without Counter-Strike 2.
//
// This is what lets the whole stack be exercised end to end. A simulated world
// integrates the movement the engine commands, the "player" follows the same
// physics constraints a real one has, and the run reports how much of the map
// was covered and how often the navigator got stuck.
//
// Usage: cs2n_sim [--mesh path] [--seconds N] [--seed N] [--verbose]
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// The simulator paces itself in real time; without a 1 ms timer the default
// 15.6 ms scheduler granularity would stretch a 60 s run to three minutes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#include "app/engine.h"
#include "core/log.h"
#include "movement/input.h"
#include "navigation/navmesh_io.h"
#include "navigation/navigator.h"
#include "state/provider.h"
#include "tests/mesh_fixture.h"

using namespace cs2n;

namespace {

// A world with the same constraints as a real player: fixed top speed, limited
// acceleration, and walls it cannot walk through.
class SimulatedWorld final : public IPlayerStateProvider {
 public:
  SimulatedWorld(const NavMesh* mesh, Vec2 position) : mesh_(mesh), state_(position) {}

  void step(double dt, bool walk, int strafe, double aimYaw) {
    // Turn towards the aim the way a human would: instantly for the simulator,
    // because the look controller's own smoothness is measured separately.
    yaw_ = aimYaw;
    Vec2 direction{std::cos(yaw_), std::sin(yaw_)};
    Vec2 lateral{-direction.y, direction.x};
    Vec2 desired{0.0, 0.0};
    if (walk) desired += direction;
    if (strafe != 0) desired += lateral * static_cast<double>(strafe);
    if (desired.lengthSq() > 1e-9) desired = desired.normalized() * kTopSpeed;

    // Accelerate towards the desired velocity, but only into walkable space.
    const double acceleration = kAcceleration * dt;
    velocity_.x += clamp(desired.x - velocity_.x, -acceleration, acceleration);
    velocity_.y += clamp(desired.y - velocity_.y, -acceleration, acceleration);

    const Vec2 candidate = state_ + velocity_ * dt;
    if (mesh_ == nullptr || mesh_->walkable(candidate)) {
      state_ = candidate;
    } else {
      // Slide along the wall instead of stopping dead.
      const Vec2 slideX{state_.x + velocity_.x * dt, state_.y};
      const Vec2 slideY{state_.x, state_.y + velocity_.y * dt};
      const bool canX = mesh_->walkable(slideX);
      const bool canY = mesh_->walkable(slideY);
      if (canX) {
        state_ = slideX;
        velocity_.y = 0.0;
      } else if (canY) {
        state_ = slideY;
        velocity_.x = 0.0;
      } else {
        velocity_.x = velocity_.y = 0.0;
      }
    }
    time_ += dt;
  }

  std::optional<PlayerState> poll() override {
    PlayerState state;
    state.valid = true;
    state.alive = alive_;
    state.position = Vec3{state_.x, state_.y, 0.0};
    state.velocity = Vec3{velocity_.x, velocity_.y, 0.0};
    state.yaw = yaw_;
    state.mapName = "de_dust2";
    state.timestamp = time_;
    return state;
  }
  [[nodiscard]] std::string name() const override { return "simulated-world"; }

  [[nodiscard]] Vec2 position() const { return state_; }
  void kill() { alive_ = false; }
  void revive(const Vec2& position) {
    alive_ = true;
    state_ = position;
    velocity_ = {};
  }

 private:
  static constexpr double kTopSpeed = 250.0;       // units/s, roughly a running player
  static constexpr double kAcceleration = 2000.0;  // units/s^2

  const NavMesh* mesh_;
  Vec2 state_;
  Vec2 velocity_;
  double yaw_ = 0.0;
  double time_ = 0.0;
  bool alive_ = true;
};

// Captures the engine's intent instead of sending it to Windows.
class SimInput final : public IInputBackend {
 public:
  void setWalk(bool down) override { walk = down; }
  void setStrafe(int direction) override { strafe = direction; }
  void turn(double counts) override { turnCounts += counts; }

  void releaseAll() override {
    walk = false;
    strafe = 0;
  }
  [[nodiscard]] std::string name() const override { return "simulated-input"; }

  bool walk = false;
  int strafe = 0;
  double turnCounts = 0.0;
};

struct Options {
  std::string meshPath;
  double seconds = 60.0;
  std::uint32_t seed = 12345;
  bool verbose = false;
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  timeBeginPeriod(1);
#endif
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--mesh" && i + 1 < argc) {
      options.meshPath = argv[++i];
    } else if (argument == "--seconds" && i + 1 < argc) {
      options.seconds = std::atof(argv[++i]);
    } else if (argument == "--seed" && i + 1 < argc) {
      options.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (argument == "--verbose") {
      options.verbose = true;
    }
  }

  NavMesh mesh;
  std::string error;
  if (!options.meshPath.empty()) {
    if (!NavMeshIo::loadFile(options.meshPath, mesh, error)) {
      std::printf("cannot load mesh: %s\n", error.c_str());
      return 1;
    }
    std::printf("loaded mesh '%s': %zu areas, %zu connections\n", mesh.name().c_str(), mesh.areaCount(),
                mesh.connectionCount());
  } else {
    mesh = cs2n::test::makeRoomMesh(10, 10, 100.0, -1, 0);
    std::printf("using built-in 10x10 test room\n");
  }

  if (!mesh.valid()) {
    std::printf("mesh is not valid\n");
    return 1;
  }

  NavigatorConfig navigatorConfig;
  navigatorConfig.strategy = GoalStrategy::RandomReachable;
  navigatorConfig.seed = options.seed;
  navigatorConfig.minGoalDistance = 200.0;
  Navigator navigator(&mesh, navigatorConfig);

  SimInput input;
  // Spawn somewhere provably walkable: on the real Dust II mesh the corner of
  // the bounding box is usually a wall, and a player wedged into geometry from
  // the first tick would look like a navigation failure.
  Vec2 spawn{50, 50};
  {
    const NavArea* best = nullptr;
    double bestRadius = -1.0;
    for (const NavArea& area : mesh.areas()) {
      const double radius = area.radius();
      if (radius > bestRadius) {
        bestRadius = radius;
        best = &area;
      }
    }
    if (best != nullptr) spawn = best->centroid();
  }
  std::printf("spawn point          : (%.1f, %.1f)\n", spawn.x, spawn.y);

  auto world = std::make_unique<SimulatedWorld>(&mesh, spawn);
  SimulatedWorld* worldPointer = world.get();

  EngineConfig engineConfig;
  engineConfig.controlHz = 200.0;
  engineConfig.recoveryAttempts = 4;
  Engine engine(&input, std::move(world), navigator, LookConfig{}, MoveConfig{}, engineConfig);

  // Coverage: which grid cells the player actually stood in.
  const double bucket = 100.0;
  std::vector<std::uint8_t> visited;
  int visitedCount = 0;
  auto record = [&](const Vec2& position) {
    const int width = static_cast<int>((mesh.boundsMax().x - mesh.boundsMin().x) / bucket) + 1;
    const int height = static_cast<int>((mesh.boundsMax().y - mesh.boundsMin().y) / bucket) + 1;
    visited.resize(static_cast<std::size_t>(width * height), 0);
    const int x = static_cast<int>((position.x - mesh.boundsMin().x) / bucket);
    const int y = static_cast<int>((position.y - mesh.boundsMin().y) / bucket);
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    std::size_t index = static_cast<std::size_t>(y * width + x);
    if (!visited[index]) {
      visited[index] = 1;
      ++visitedCount;
    }
  };

  engine.start();

  const double dt = 1.0 / 200.0;
  double deaths = 0.0;
  bool killed = false;
  int steps = static_cast<int>(options.seconds / dt);
  for (int step = 0; step < steps; ++step) {
    const auto snapshot = engine.snapshot();
    const double aim = snapshot.havePlayer ? snapshot.player.yaw : 0.0;
    if (snapshot.havePlayer && snapshot.state == EngineState::Navigating) {
      // The look controller's job is to reduce the aim error; feed the world the
      // direction the engine is trying to face.
      const double target = aim + snapshot.aimErrorDeg * kPi / 180.0;
      worldPointer->step(dt, input.walk, input.strafe, target);
    } else {
      worldPointer->step(dt, false, 0, aim);
    }
    record(worldPointer->position());

    // A scripted death partway through, to check the respawn path in motion.
    if (!killed && step == steps / 3) {
      worldPointer->kill();
      ++deaths;
      killed = true;
    } else if (killed && step == steps / 3 + 200) {
      worldPointer->revive(spawn);
      killed = false;
    }

    if (options.verbose && step % 200 == 0) {
      const auto now = engine.snapshot();
      std::printf("t=%6.1f state=%-11s pos=(%6.1f,%6.1f) path=%zu stuck=%d rem=%7.1f walk=%d strafe=%d aimErr=%6.1f\n",
                  step * dt, engineStateName(now.state), worldPointer->position().x, worldPointer->position().y,
                  now.pathPoints, now.stuckCount, now.remainingDistance, input.walk ? 1 : 0, input.strafe,
                  now.havePlayer ? now.aimErrorDeg : 0.0);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(dt));
  }

  engine.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto final = engine.snapshot();
  std::printf("\n--- simulation summary ---\n");
  std::printf("final state        : %s\n", engineStateName(final.state));
  std::printf("final position     : (%.1f, %.1f)\n", worldPointer->position().x, worldPointer->position().y);
  std::printf("cells visited       : %d\n", visitedCount);
  std::printf("stuck recoveries    : %d\n", final.stuckCount);
  std::printf("deaths handled      : %.0f\n", deaths);
  std::printf("total mouse counts  : %.0f\n", input.turnCounts);
  std::printf("last status         : %s\n", final.status.c_str());
  if (!final.lastError.empty()) std::printf("last error          : %s\n", final.lastError.c_str());

  // A run that ends in ERROR or never left the spawn area is a failure.
  const int result = [&] {
    if (final.state == EngineState::Error) {
      std::printf("\nRESULT: FAIL (engine ended in ERROR)\n");
      return 1;
    }
    if (visitedCount < 4) {
      std::printf("\nRESULT: FAIL (the player barely moved)\n");
      return 1;
    }
    std::printf("\nRESULT: PASS\n");
    return 0;
  }();
#ifdef _WIN32
  timeEndPeriod(1);
#endif
  return result;
}
