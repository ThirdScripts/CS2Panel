// Read-only lifecycle diagnostic. Never launches or controls CS2.
#include "state/gsi_provider.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
int main(int argc, char** argv) {
  if(argc < 2 || argc > 4) { std::cerr << "Usage: cs2n_gsi_state_probe <existing GSI cfg> [seconds:1..600] [expectedSteamId]\n"; return 2; }
  unsigned long seconds=20;
  if(argc>=3) {
    try {
      std::size_t used=0; seconds=std::stoul(argv[2],&used);
      if(used!=std::string(argv[2]).size() || seconds<1 || seconds>600) return 2;
    } catch(...) { return 2; }
  }
  std::ifstream file(argv[1]);
  if(!file) { std::cerr << "Cannot read GSI configuration\n"; return 2; }
  std::ostringstream data; data << file.rdbuf(); const auto text=data.str();
  std::smatch uri,token;
  if(!std::regex_search(text,uri,std::regex("\"uri\"\\s*\"http://127\\.0\\.0\\.1:([0-9]+)/?\"")) ||
     !std::regex_search(text,token,std::regex("\"token\"\\s*\"([^\"]+)\""))) {
    std::cerr << "Expected loopback URI and nonempty token\n"; return 2;
  }
  unsigned long port=0;
  try { port=std::stoul(uri[1]); } catch(...) { return 2; }
  if(port==0 || port>65535) return 2;
  cs2n::GsiConfig config; config.port=static_cast<std::uint16_t>(port);
  config.token=token[1]; config.requiredMap.clear();
  if(argc==4) config.expectedSteamId=argv[3];
  cs2n::GsiProvider provider(config);
  if(!provider.listening()) { std::cerr << "GSI listener unavailable; existing listeners are not stopped\n"; return 3; }
  bool received=false;
  for(unsigned long i=0;i<seconds*10;++i) {
    auto state=provider.clientState();
    const auto player=provider.poll();
    received |= state.receivedAt>=0;
    const auto utcMs=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    std::cout << "utc_unix_ms=" << utcMs << " sample=" << i << " state=" << cs2n::clientStateName(state.state)
              << " gsi=" << static_cast<int>(state.gsi) << " received_at=" << state.receivedAt
              << " account_bound=" << !config.expectedSteamId.empty()
              << " player_valid=" << bool(player);
    if(player) std::cout << " health=" << player->health << " alive=" << player->alive << " map=" << player->mapName;
    std::cout << " reason=" << state.reason << " player_reason=" << provider.lastRejection() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  // No payloads, tokens, account IDs, or chat content are logged.
  return received?0:4;
}
