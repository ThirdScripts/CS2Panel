// Read-only lifecycle diagnostic. Never launches or controls CS2.
#include "state/gsi_provider.h"
#include "session/practice_launcher.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
int main(int argc, char** argv) {
  if(argc != 3) { std::cerr << "Usage: cs2n_deathmatch_launch_probe <existing GSI cfg> <PID>\n"; return 2; }
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

  auto provider=std::make_shared<cs2n::GsiProvider>(config);
  if(!provider->listening()){std::cerr<<"GSI listener unavailable\n";return 3;}
  unsigned long pid=0;try{pid=std::stoul(argv[2]);}catch(...){return 2;}
  cs2n::session::DeathmatchLauncher launcher({pid,0},provider);
  launcher.start();std::string last;
  for(int i=0;i<3300;++i){
    auto message=launcher.message();if(message!=last){std::cout<<message<<std::endl;last=message;}
    auto status=launcher.status();
    if(status==cs2n::session::LaunchStatus::Ready){std::cout<<"READY: GSI confirmed. Navigator engine was not started by this probe.\n";return 0;}
    if(status==cs2n::session::LaunchStatus::Failed||status==cs2n::session::LaunchStatus::Cancelled){std::cerr<<launcher.message()<<std::endl;return 4;}
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  launcher.cancel();return 5;
}
