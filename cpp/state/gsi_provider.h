// Game State Integration listener. The existing player provider is separate
// from the conservative client lifecycle model; neither supplies world positions.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "state/player_state.h"
#include "state/client_state.h"
#include "state/provider.h"

namespace cs2n {

struct GsiConfig {
  std::uint16_t port = 31987;
  std::string token;              // required; requests without it are rejected
  std::string requiredMap = "de_dust2";
  std::string expectedSteamId;  // optional endpoint identity restriction
  double staleAfterSeconds = 1.5; // fail closed when CS2 stops sending updates
};

class GsiProvider final : public IPlayerStateProvider {
 public:
  explicit GsiProvider(GsiConfig config);
  ~GsiProvider() override;

  GsiProvider(const GsiProvider&) = delete;
  GsiProvider& operator=(const GsiProvider&) = delete;

  [[nodiscard]] bool listening() const { return listening_; }
  [[nodiscard]] std::uint16_t port() const { return config_.port; }
  [[nodiscard]] std::string lastRejection() const;
  std::string diagnostic() const override {return lastRejection();}

  std::optional<PlayerState> poll() override;
  MatchSetupState matchSetup() const override;
  ClientStateSnapshot clientState();
  // Explicit process evidence for a bound observer or deterministic tests.
  ClientStateSnapshot clientState(ProcessPresence process);
  static MatchSetupState evaluateMatchSetup(const std::string& body,const GsiConfig& config,double now);
  [[nodiscard]] std::string name() const override { return "gsi"; }

  // Pure payload -> state evaluation, exposed so the rules can be unit tested
  // without standing up the HTTP listener.
  struct Evaluation {
    bool accepted = false;
    std::string rejection;
    PlayerState state;
  };
  static Evaluation evaluatePayload(const std::string& body, const GsiConfig& config, double now);

 private:
  void serveLoop();
  bool handleRequest(const std::string& method, const std::string& path, const std::string& body,
                     int& status, std::string& reason);
  void applyPayload(const std::string& body);

  GsiConfig config_;
  ClientStateModel clientState_;
  std::atomic<bool> running_{false};
  std::atomic<bool> listening_{false};
  std::thread thread_;
  bool winsockStarted_ = false;
  std::uintptr_t listenSocket_ = ~std::uintptr_t(0);

  mutable std::mutex mutex_;
  PlayerState state_;
  MatchSetupState setup_;
  std::string lastRejection_ = "no data yet";
  std::uint64_t sequence_ = 0;
};

}  // namespace cs2n
