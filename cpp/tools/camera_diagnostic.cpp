// Bounded diagnostic: read-only by default; --turn performs a four-second
// camera-only sweep. No movement, aiming, firing, team or purchase commands.
#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include "state/cs2_memory.h"
#include "movement/memory_input.h"
using Clock=std::chrono::steady_clock;
double seconds(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
int main(int argc,char** argv) {
  using namespace cs2n;
  const bool pitch=argc>=2&&std::string(argv[1])=="--native-pitch";
  const bool native=pitch||(argc>=2&&std::string(argv[1])=="--native-turn");
  const double axis=native&&argc==3?std::strtod(argv[2],nullptr):.25;
  if(!std::isfinite(axis)||axis<=0||axis>1||argc>3)return 2;
  const bool turn=native||(argc==2&&std::string(argv[1])=="--turn");
  if(argc>1&&!turn)return 2;
  HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\CS2NavigatorCpp");
  if(GetLastError()==ERROR_ALREADY_EXISTS){std::cerr<<"Close Navigator before the isolated diagnostic\n";CloseHandle(singleton);return 2;}
  wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  std::filesystem::current_path(std::filesystem::path(path).parent_path());
  Cs2MemoryProvider local("assets/memory.json",false),full("assets/memory.json");
  auto first=local.poll();
  if(!first||!first->alive){std::cerr<<"No live player: "<<local.error()<<'\n';CloseHandle(singleton);return 3;}
  timeBeginPeriod(1);
  for(auto* provider:{&full,&local}) {
    std::vector<double> times;
    for(int i=0;i<120;++i){double start=seconds();provider->poll();times.push_back((seconds()-start)*1000);std::this_thread::sleep_for(std::chrono::milliseconds(7));}
    std::sort(times.begin(),times.end());
    std::cout<<(provider==&full?"full":"local")<<" poll_ms median="<<times[60]<<" p95="<<times[114]<<" max="<<times.back()<<'\n';
  }
  int result=0;
  if(turn){
    MemoryInput input;
    if(!input.error().empty()){std::cerr<<input.error()<<'\n';result=4;}
    else {
      std::ofstream out("camera-diagnostic.csv");
      out<<"elapsed,dt,yaw,command_deg,previous_command_residual_deg\n";
      double start=seconds(),previous=start,previousYaw=0,previousCommand=0;bool have=false;
      auto deadline=Clock::now();
      for(int i=0;i<576;++i){
        double now=seconds(),dt=now-previous;previous=now;
        auto p=local.poll();
        if(input.stopRequested()||!p||!p->alive||!input.ready()){std::cerr<<"Sweep interrupted; inconclusive\n";result=5;break;}
        const double yaw=(pitch?p->pitch:p->yaw)*180/kPi;
        const double residual=have?wrapAngle((yaw-previousYaw-previousCommand)*kPi/180)*180/kPi:0;
        const double command=(now-start<2?30.0:-30.0)*std::clamp(dt,0.0,.025);
        if(native)input.setTurnAxis(now-start<2?axis:-axis,pitch);
        else input.diagnosticDirectTurn(command*kPi/180/p->radiansPerMouseCount);
        out<<now-start<<','<<dt<<','<<yaw<<','<<command<<','<<residual<<'\n';
        previousYaw=yaw;previousCommand=command;have=true;
        deadline+=std::chrono::microseconds(6944);std::this_thread::sleep_until(deadline);
        if(now-start>=4)break;
      }
      input.releaseAll();std::cout<<"Camera trace: camera-diagnostic.csv\n";
    }
  }
  timeEndPeriod(1);CloseHandle(singleton);return result;
}
