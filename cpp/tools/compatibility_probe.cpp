// Manual, bounded local-match validation. Never run automatically by CTest.
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include "movement/memory_input.h"
#include "state/cs2_memory.h"
int main(int argc,char** argv) {
 using namespace cs2n;
 if(argc!=4||std::string(argv[1])!="--local-exercise")return 2;
 try {
  const auto pid=std::stoul(argv[2]);const auto created=std::stoull(argv[3]);
  if(!pid||pid>MAXDWORD||!created)return 2;
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
  std::filesystem::current_path(std::filesystem::path(exe).parent_path());
  ProcessTarget target{DWORD(pid),created};
  Cs2MemoryProvider memory("assets/memory.json",true,target);
  MemoryInput input(target);
  auto before=memory.poll();
  if(!before||!before->alive||!input.ready())throw std::runtime_error("No live compatible player: "+input.error());
  auto step=[&](int phase){
   for(int i=0;i<60;++i){
    auto p=memory.poll();
    if(!p||!p->alive||!input.ready()||input.stopRequested())throw std::runtime_error("Exercise interrupted; inconclusive");
    if(phase==0)input.setWalk(true);
    if(phase==1)input.setTurnAxis(.15);
    if(phase==2)input.setTurnAxis(.15,true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   input.releaseAll();std::this_thread::sleep_for(std::chrono::milliseconds(150));
  };
  step(0);auto walked=memory.poll(); if(!walked)throw std::runtime_error("No movement result");
  const double distance=(walked->position.xy()-before->position.xy()).length();
  step(1);auto turned=memory.poll();if(!turned)throw std::runtime_error("No yaw result");
  const double yaw=std::abs(wrapAngle(turned->yaw-walked->yaw))*180/kPi;
  step(2);auto pitched=memory.poll();if(!pitched)throw std::runtime_error("No pitch result");
  const double pitch=std::abs(pitched->pitch-turned->pitch)*180/kPi;
  std::cout<<"distance="<<distance<<" yaw_delta="<<yaw<<" pitch_delta="<<pitch<<std::endl;
  if(distance<2||yaw<1||pitch<.2)throw std::runtime_error("Movement/camera not confirmed");
  if(!input.loadoutCommandsAvailable())throw std::runtime_error(input.loadoutUnavailableReason());
  input.buyLoadout();
  for(int i=0;i<30;++i){auto p=memory.poll();if(p&&p->weapon.item==7){std::cout<<"AK47 confirmed; input released\n";return 0;}std::this_thread::sleep_for(std::chrono::milliseconds(100));}
  throw std::runtime_error("Purchase not confirmed");
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
