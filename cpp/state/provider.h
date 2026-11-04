// The navigation engine only ever talks to this interface.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "state/player_state.h"

namespace cs2n {

struct MatchSetupState {
  bool valid = false;
  int team = 0;
  double timestamp = 0;
  std::string mapName;
};

class IPlayerStateProvider {
 public:
  virtual ~IPlayerStateProvider() = default;

  // Returns the freshest state, or nullopt when no trustworthy data is available.
  // Implementations must fail closed: stale or implausible data yields nullopt.
  virtual std::optional<PlayerState> poll() = 0;

  // Human readable name for diagnostics, e.g. "gsi+radar".
  [[nodiscard]] virtual std::string name() const = 0;
  virtual std::string diagnostic() const {return name();}
  virtual MatchSetupState matchSetup() const {return {};}
};

using PlayerStateProviderPtr = std::shared_ptr<IPlayerStateProvider>;

// Combines providers: a "live" source supplies map/health, a "position" source
// supplies coordinates and yaw. Returns nullopt unless both agree the player is
// alive and the position is fresh.
class CompositeProvider final : public IPlayerStateProvider {
 public:
  CompositeProvider(PlayerStateProviderPtr live, PlayerStateProviderPtr position)
      : live_(std::move(live)), position_(std::move(position)) {}

  std::optional<PlayerState> poll() override;
  [[nodiscard]] std::string name() const override;
  std::string diagnostic() const override {return (live_?live_->diagnostic():"")+"; "+(position_?position_->diagnostic():"");}
  MatchSetupState matchSetup() const override {return live_?live_->matchSetup():MatchSetupState{};}

  // Staleness budget applied to the position source.
  void setMaxPositionAge(double seconds) { maxPositionAge_ = seconds; }

 private:
  PlayerStateProviderPtr live_;
  PlayerStateProviderPtr position_;
  double maxPositionAge_ = 0.35;
};

// Test/simulation helper: returns a caller-supplied state.
class StaticProvider final : public IPlayerStateProvider {
 public:
  explicit StaticProvider(PlayerState state, std::string label = "static")
      : state_(std::move(state)), label_(std::move(label)) {}

  void setState(const PlayerState& state) { state_ = state; }
  std::optional<PlayerState> poll() override {
    if (!state_.valid) return std::nullopt;
    return state_;
  }
  [[nodiscard]] std::string name() const override { return label_; }

 private:
  PlayerState state_;
  std::string label_;
};

}  // namespace cs2n
