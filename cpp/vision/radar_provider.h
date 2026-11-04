// Radar-based position provider.
//
// This is the production position source. It captures the radar rectangle,
// localises the own-player marker, and publishes the result through the same
// IPlayerStateProvider interface every other source uses, so the navigation
// engine never needs to know where its coordinates came from.
#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "state/provider.h"
#include "vision/capture.h"
#include "vision/image.h"
#include "vision/radar_localizer.h"

namespace cs2n {

struct RadarProviderConfig {
  CaptureRegion region;
  MarkerTuning tuning;
  std::string expectedMap;      // when set, refuse states from any other map
  double maxFrameAge = 0.35;    // frames older than this are not published
  double positionJumpLimit = 400.0;  // radar pixels; larger jumps are rejected
};

class RadarPositionProvider final : public IPlayerStateProvider {
 public:
  explicit RadarPositionProvider(RadarProviderConfig config);

  std::optional<PlayerState> poll() override;
  [[nodiscard]] std::string name() const override { return "radar-vision"; }

  // Diagnostics for the UI.
  [[nodiscard]] Image lastFrame() const;
  [[nodiscard]] std::string lastReason() const;
  [[nodiscard]] double lastConfidence() const;
  [[nodiscard]] std::uint64_t frameCount() const;

  // Rejects a jump that a real player could not have made (radar glitch, or a
  // respawn). The caller decides what to do about it.
  void forgetHistory();

 private:
  RadarProviderConfig config_;
  RadarLocalizer localizer_;
  RadarCapture capture_;

  mutable std::mutex mutex_;
  Image lastFrame_;
  std::string lastReason_ = "no frame captured yet";
  double lastConfidence_ = 0.0;
  std::uint64_t frameCount_ = 0;
  bool havePrevious_ = false;
  Vec2 previousPosition_;
};

}  // namespace cs2n
