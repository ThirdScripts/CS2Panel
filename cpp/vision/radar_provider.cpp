#include "vision/radar_provider.h"

#include <chrono>

namespace cs2n {
namespace {

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

RadarPositionProvider::RadarPositionProvider(RadarProviderConfig config)
    : config_(std::move(config)), localizer_(config_.tuning) {}

std::optional<PlayerState> RadarPositionProvider::poll() {
  std::string error;
  auto frame = capture_.grab(config_.region, error);
  if (!frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastReason_ = error;
    return std::nullopt;
  }

  const DetectionResult detection = localizer_.locate(*frame);
  const double now = monotonicSeconds();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    lastFrame_ = *frame;
    lastReason_ = detection.reason;
    lastConfidence_ = detection.confidence;
    ++frameCount_;
  }

  if (!detection.ok) return std::nullopt;

  if (havePrevious_ &&
      distance(detection.worldPosition, previousPosition_) > config_.positionJumpLimit * config_.tuning.unitsPerPixel) {
    // A jump this large cannot be locomotion. Keep the new position but report
    // nothing, so the engine restarts its route instead of chasing a ghost.
    std::lock_guard<std::mutex> lock(mutex_);
    lastReason_ = "position jumped, treating as respawn or glitch";
    previousPosition_ = detection.worldPosition;
    return std::nullopt;
  }

  PlayerState state;
  state.valid = true;
  state.alive = true;
  state.position = Vec3{detection.worldPosition.x, detection.worldPosition.y, 0.0};
  state.yaw = detection.yaw;
  state.mapName = config_.expectedMap;
  state.timestamp = now;

  previousPosition_ = detection.worldPosition;
  havePrevious_ = true;
  return state;
}

Image RadarPositionProvider::lastFrame() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastFrame_;
}

std::string RadarPositionProvider::lastReason() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastReason_;
}

double RadarPositionProvider::lastConfidence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastConfidence_;
}

std::uint64_t RadarPositionProvider::frameCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return frameCount_;
}

void RadarPositionProvider::forgetHistory() {
  std::lock_guard<std::mutex> lock(mutex_);
  havePrevious_ = false;
}

}  // namespace cs2n
