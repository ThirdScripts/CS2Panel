#include "movement/memory_input.h"
#include <TlHelp32.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "core/log.h"
#include "movement/native_turn.h"
namespace cs2n {
namespace {
double seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
JsonValue load(const char* path){std::ifstream f(path);std::ostringstream s;s<<f.rdbuf();JsonValue j;std::string e;JsonValue::parse(s.str(),j,e);return j;}
}
MemoryInput::MemoryInput(ProcessTarget target):memory_("assets/memory.json",false,target),bound_(target.pid!=0){
  const auto profile=load("assets/memory.json"),buttons=load("assets/input-buttons.json");
  if(buttons["clientSha256"].asString().empty()||buttons["clientSha256"].asString()!=profile["clientSha256"].asString()){
    error_="Input profile missing or incompatible";return;}
  auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W e{};e.dwSize=sizeof(e);int count=0;
  if(snap==INVALID_HANDLE_VALUE){error_="Cannot enumerate game processes";return;}
  if(Process32FirstW(snap,&e))do{if(_wcsicmp(e.szExeFile,L"cs2.exe")==0)++count;}while(Process32NextW(snap,&e));CloseHandle(snap);
  if(!bound_&&count!=1){error_="Multiple CS2 processes require explicit target";return;}
  auto p=memory_.poll();
  // The module can be validated in the lobby before a local pawn exists.
  if(!memory_.connected()||!memory_.clientBase()){error_=memory_.error();return;}
  if(p)player_=*p;
  base_=memory_.clientBase();angles_=base_+profile["angles"].asInt();
  handle_=OpenProcess(PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,memory_.processId());
  if(!handle_){error_="Cannot open game for experimental input";return;}
  struct ConstructionHandleGuard {
    HANDLE& handle;
    bool transferred = false;
    ~ConstructionHandleGuard(){if(!transferred && handle){CloseHandle(handle);handle=nullptr;}}
  } handleGuard{handle_};
  if(!sameProcess(handle_,memory_.target())){CloseHandle(handle_);handle_=nullptr;error_="Input target identity changed";return;}
  for(const char* name:{"forward","back","left","right","attack","attack2","reload","duck","turnleft","turnright","turnup","turndown"}){
    const auto rva=buttons["buttons"][name]["rva"].asInt();
    if(rva<0x30||rva+4>profile["moduleSize"].asInt()){error_="Invalid input address";break;}
    std::uintptr_t namePointer=0;char actual[32]{};SIZE_T read=0;
    if(!ReadProcessMemory(handle_,reinterpret_cast<void*>(base_+rva-0x28),&namePointer,8,&read)||read!=8||
       !ReadProcessMemory(handle_,reinterpret_cast<void*>(namePointer),actual,sizeof(actual),&read)||read!=sizeof(actual)||
       std::string(actual,strnlen_s(actual,sizeof(actual)))!=name){error_="Button layout validation failed";break;}
    buttons_[name]=base_+rva;
  }
  if(!error_.empty()){CloseHandle(handle_);handle_=nullptr;return;}
  commands_=std::make_unique<GameCommands>(memory_.processId(),memory_.target().created);
  if(!commands_->available())Logger::instance().error("Background commands: "+commands_->error());
  Logger::instance().info("EXPERIMENTAL memory input: button names and client version validated");
  watchdog_=std::thread([this]{while(!done_){std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::recursive_mutex> lock(mutex_);const auto now=seconds();
    if(emergencyStopHeld()||now-lease_>.25){releaseAll();continue;}
    for(auto& [key,expiry]:pulses_)if(expiry>0&&now>=expiry){button(key.c_str(),false);expiry=0;}
  }});
  handleGuard.transferred=true;
}
MemoryInput::~MemoryInput(){done_=true;if(watchdog_.joinable())watchdog_.join();releaseAll();if(handle_)CloseHandle(handle_);}
bool MemoryInput::ready() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!handle_||!error_.empty())return false;
  if(WaitForSingleObject(handle_,0)!=WAIT_TIMEOUT){error_="CS2 closed or restarted; restart Navigator";return false;}
  const double now=seconds();if(now-checkedAt_>.01){checkedAt_=now;auto p=memory_.poll();
    if(!p){player_.valid=false;return false;}player_=*p;
    if(memory_.clientBase()!=base_){error_="Game process changed; restart Navigator";return false;}}
  return player_.valid;
}
bool MemoryInput::stopRequested() const{return emergencyStopHeld() || (cs2ForegroundActive() && (GetAsyncKeyState(VK_ESCAPE)&0x8000));}
bool MemoryInput::write(std::uintptr_t addr,const void* data,std::size_t size){SIZE_T n=0;
  if(!handle_||!WriteProcessMemory(handle_,reinterpret_cast<void*>(addr),data,size,&n)||n!=size){error_="Game input write failed";return false;}return true;}
void MemoryInput::button(const char* name,bool down){std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it=buttons_.find(name);if(it==buttons_.end())return;
  if(down&&(!ready()||!player_.alive||stopRequested()))down=false;
  if(!down&&!held_[name])return;
  // In this pinned client, movement samples the float at KeyButton+0x18.
  // The edge flags at +0x30 alone change readback but produce no movement.
  // Confirmed against ordinary W down/up and the installed client's reader.
  const float value=down?1.f:0.f;
  const std::uint32_t state=down?65537u:16777472u;
  // Track ownership before either write so a partial failure is released too.
  if(down)held_[name]=true;
  const bool valueWritten=write(it->second-0x18,&value,sizeof(value));
  const bool stateWritten=write(it->second,&state,sizeof(state));
  if(valueWritten&&stateWritten)held_[name]=down;
  if(down)lease_=seconds();
}
void MemoryInput::pulse(const char* name){std::lock_guard<std::recursive_mutex> lock(mutex_);button(name,true);if(held_[name])pulses_[name]=seconds()+.05;}
void MemoryInput::setTurnAxis(double value,bool pitch){
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!std::isfinite(value)||!ready()||!player_.alive||stopRequested())value=0;
  value=std::clamp(value,-1.0,1.0);
  const char* active=pitch?(value>0?"turndown":"turnup"):(value>0?"turnleft":"turnright");
  const char* inactive=pitch?(value>0?"turnup":"turndown"):(value>0?"turnright":"turnleft");
  button(inactive,false);
  if(value==0){button(active,false);return;}
  auto it=buttons_.find(active);if(it==buttons_.end())return;
  held_[active]=true;float magnitude=static_cast<float>(std::abs(value));
  const std::uint32_t state=65537u;
  if(!write(it->second-0x18,&magnitude,sizeof(magnitude))||!write(it->second,&state,sizeof(state))){releaseAll();return;}
  lease_=seconds();pulses_[active]=lease_+.05;
}
void MemoryInput::turn(double counts){
  nativeAngle(counts,false);
}
void MemoryInput::nativeAngle(double counts,bool pitch){
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto& last=pitch?lastPitchAt_:lastTurnAt_;
  if(!ready()||!player_.alive||stopRequested()){setTurnAxis(0,pitch);last=-1;return;}
  const double now=seconds();
  const double dt=last<0||now-last>.05?1.0/144.0:std::clamp(now-last,.001,.025);
  last=now;
  setTurnAxis(nativeTurnAxis(counts,player_.radiansPerMouseCount,dt,pitch?60:400),pitch);
}
void MemoryInput::angle(double counts,bool pitch){std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!std::isfinite(counts)||!ready()||!player_.alive||stopRequested())return;
  float a[3]{};SIZE_T n=0;if(!ReadProcessMemory(handle_,reinterpret_cast<void*>(angles_),a,sizeof(a),&n)||n!=sizeof(a))return;
  if(!std::isfinite(a[0])||!std::isfinite(a[1])||std::abs(a[0])>90||std::abs(a[1])>360)return;
  const double delta=std::clamp(counts*player_.radiansPerMouseCount*180/kPi,-5.,5.);
  const int axis=pitch?0:1;
  a[axis]=static_cast<float>(pitch?std::clamp(a[0]+delta,-89.,89.):wrapAngle((a[1]+delta)*kPi/180)*180/kPi);
  write(angles_+axis*sizeof(float),&a[axis],sizeof(float));lease_=seconds();
}
void MemoryInput::releaseAll(){std::lock_guard<std::recursive_mutex> lock(mutex_);
  for(auto& [key,held]:held_)if(held)button(key.c_str(),false);
  pulses_.clear();lastTurnAt_=-1;lastPitchAt_=-1;fallback_.releaseAll();
}
void MemoryInput::command(const std::string& text){
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  releaseAll();
  if(stopRequested()||!commands_)return;
  if(!commands_->send(text))Logger::instance().error("Background command failed: "+commands_->error());
}
bool MemoryInput::disconnectMatch(){
  std::lock_guard<std::recursive_mutex> lock(mutex_);releaseAll();
  return !stopRequested()&&commands_&&commands_->send("disconnect");
}
}
