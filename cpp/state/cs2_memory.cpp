#include "state/cs2_memory.h"
#include <TlHelp32.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cs2n {
namespace {
double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Float3 { float x,y,z; };
bool finite(Float3 v, double limit) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<limit&&std::abs(v.y)<limit&&std::abs(v.z)<limit; }
}
Cs2MemoryProvider::Cs2MemoryProvider(const std::string& path, bool includeCombat, ProcessTarget target) : target_(target), includeCombat_(includeCombat) {
  std::ifstream file(std::filesystem::u8path(path)); std::ostringstream text; text << file.rdbuf();
  if (!file || !JsonValue::parse(text.str(),profile_,error_)) {
    error_="Cannot load memory profile: "+path;
    return;
  }
  for (const char* key : {"pawn", "moduleTimestamp", "moduleSize", "clientVersion"}) {
    const auto& value = profile_[key];
    const double number = value.asNumber();
    if (!value.isNumber() || !std::isfinite(number) || number <= 0 ||
        (number > (std::string(key) == "clientVersion" ? 2147483647.0 : 4294967295.0)) || std::floor(number) != number) {
      error_ = std::string("Invalid memory profile field: ") + key;
      return;
    }
  }
  profileValid_ = true;
}
Cs2MemoryProvider::~Cs2MemoryProvider() { detach(); }
void Cs2MemoryProvider::detach() { if(process_) CloseHandle(process_); process_=nullptr; client_=0; pid_=0;entityStride_=0;opponents_.clear();observersValid_=false;lastObserversPoll_=-1; }
bool Cs2MemoryProvider::read(std::uintptr_t address, void* out, std::size_t size) const {
  SIZE_T done=0;
  return process_ && address>=0x10000 && address < 0x0000800000000000ull &&
    ReadProcessMemory(process_,reinterpret_cast<LPCVOID>(address),out,size,&done) && done==size;
}
bool Cs2MemoryProvider::attach() {
  if(!profileValid_) return false;
  HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(list==INVALID_HANDLE_VALUE) { error_="Cannot enumerate processes"; return false; }
  PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
  int count=0;
  if(Process32FirstW(list,&entry)) do { if(_wcsicmp(entry.szExeFile,L"cs2.exe")==0 && (!target_.pid||entry.th32ProcessID==target_.pid)) {pid_=entry.th32ProcessID;++count;} } while(Process32NextW(list,&entry));
  CloseHandle(list);
  if(count>1){pid_=0;error_="Multiple CS2 processes: explicit PID and creation time required";return false;}
  if(target_.pid&&!target_.created){pid_=0;error_="Explicit target requires creation time";return false;}
  if(!pid_) {error_="Waiting for cs2.exe";return false;}
  process_=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid_);
  if(!process_) {error_="OpenProcess failed: "+std::to_string(GetLastError());pid_=0;return false;}
  if(!target_.pid)target_={pid_,processCreation(process_)};
  if(!sameProcess(process_,target_)){error_="Target exited or PID reused; refusing another client";detach();return false;}
  wchar_t image[32768]{};DWORD length=32768;
  if(!QueryFullProcessImageNameW(process_,0,image,&length)||_wcsicmp(std::filesystem::path(image).filename().c_str(),L"cs2.exe")){
    error_="Target executable is not cs2.exe";detach();return false;
  }
  list=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid_);
  std::filesystem::path modulePath;
  if(list!=INVALID_HANDLE_VALUE) {
    MODULEENTRY32W module{}; module.dwSize=sizeof(module);
    if(Module32FirstW(list,&module)) do {if(_wcsicmp(module.szModule,L"client.dll")==0) {client_=reinterpret_cast<std::uintptr_t>(module.modBaseAddr);modulePath=module.szExePath;break;}} while(Module32NextW(list,&module));
    CloseHandle(list);
  }
  IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
  if(!client_ || !read(client_,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000||!read(client_+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE) {
    error_="Cannot read client.dll PE headers";detach();return false;
  }
  if(nt.FileHeader.TimeDateStamp!=offset("moduleTimestamp")||nt.OptionalHeader.SizeOfImage!=offset("moduleSize")) {
    error_="CS2 client.dll changed; rebuild and validate memory profile before use";detach();return false;
  }
  std::ifstream inf(modulePath.parent_path().parent_path().parent_path()/"steam.inf");
  std::string line; bool versionMatches=false;
  while(std::getline(inf,line)) if(line=="ClientVersion="+std::to_string(profile_["clientVersion"].asInt())) versionMatches=true;
  if(!versionMatches) { error_="Installed CS2 version differs from profile/navmesh";detach();return false; }
  error_.clear();return true;
}
std::optional<PlayerState> Cs2MemoryProvider::poll() {
  double now=nowSeconds();
  if(process_ && WaitForSingleObject(process_,0)!=WAIT_TIMEOUT) detach();
  if(!process_) { if(now<nextAttach_) return {};nextAttach_=now+1; if(!attach()) return {}; }
  std::uintptr_t pawn=0,verify=0,sens=0; int health=0; unsigned char life=0; Float3 pos{},vel{},angles{}; float sensitivity=0;
  if(!read(client_+offset("pawn"),pawn) || !pawn) {error_="Local pawn unavailable (menu/loading)";return {};}
  if(!read(pawn+offset("health"),health)||!read(pawn+offset("life"),life)||
     !read(pawn+offset("position"),pos)||!read(pawn+offset("velocity"),vel)||
     !read(client_+offset("angles"),angles)||!read(client_+offset("sensitivity"),sens)||
     !read(sens+offset("sensitivityValue"),sensitivity)||!read(client_+offset("pawn"),verify)||pawn!=verify) {
    error_="Incomplete local-player snapshot";return {};
  }
  if(health<0||health>500||life>3||!finite(pos,32768)||!finite(vel,10000)||!finite(angles,361)||
      std::abs(angles.x)>90.1||!std::isfinite(sensitivity)||sensitivity<.01||sensitivity>100) {
    error_="Memory values failed validation; profile may be incompatible";return {};
  }
  sensitivity_=sensitivity;
  PlayerState s; s.valid=true; s.alive=health>0&&life==0; s.position={pos.x,pos.y,pos.z};
  s.health=health;
  s.velocity={vel.x,vel.y,vel.z};s.yaw=angles.y*kPi/180;s.pitch=angles.x*kPi/180;
  s.radiansPerMouseCount=sensitivity_*mouseYawDegrees()*kPi/180;
  // Map identity comes from fresh GSI, never from a static memory profile.
  s.timestamp=now;error_.clear();
  if(profile_["team"].isNumber()) {unsigned char team=0;read(pawn+offset("team"),team);s.team=team;}
  if(profile_["flags"].isNumber()) {std::uint32_t flags=0;if(read(pawn+offset("flags"),flags))s.onGround=(flags&1)!=0;}
  if(profile_["aimPunchServices"].isNumber()) {
    std::uintptr_t punch=0;Float3 angle{};int shots=0;
    if(read(pawn+offset("aimPunchServices"),punch)&&read(punch+offset("punchAngle"),angle)&&finite(angle,25)&&
       read(pawn+offset("shotsFired"),shots)&&shots>=0&&shots<200) {
      s.recoil={angle.x*kPi/180,angle.y*kPi/180};s.recoilValid=true;s.shotsFired=shots;
    }
  }
  if(s.alive && includeCombat_) {pollCombat(pawn,s);pollObservers(pawn,s);}
  return s;
}

std::uintptr_t Cs2MemoryProvider::entity(std::uintptr_t list,std::uint32_t handle,std::size_t stride) const {
  if(handle==0xffffffff || !stride)return 0;
  std::uint32_t index=handle&0x7fff;std::uintptr_t page=0,result=0;
  if(!read(list+0x10+8*(index>>9),page)||!read(page+stride*(index&0x1ff),result))return 0;
  return result;
}



void Cs2MemoryProvider::pollObservers(std::uintptr_t pawn,PlayerState& s) {
  if(!entityStride_||!profile_["observerPawn"].isNumber())return;
  if(s.timestamp-lastObserversPoll_>=.1) {
    lastObserversPoll_=s.timestamp;spectators_=0;observersValid_=false;
    std::uintptr_t list=0,local=0;std::uint32_t localHandle=0xffffffff;
    if(!read(client_+offset("entityList"),list)||!read(client_+offset("localController"),local)||
       !read(local+offset("controllerPawn"),localHandle)||entity(list,localHandle,entityStride_)!=pawn)return;
    observersValid_=true;
    for(unsigned i=1;i<=64;++i) {
      auto controller=entity(list,i,entityStride_);if(!controller||controller==local)continue;
      std::uint64_t steamId=0;std::uint32_t handle=0xffffffff,active=0xffffffff;unsigned char hltv=0;
      if(!read(controller+offset("steamId"),steamId)||!steamId||
         !read(controller+offset("hltv"),hltv)||hltv||!read(controller+offset("observerPawn"),handle)||
         !read(controller+offset("activePawn"),active)||handle!=active)continue;
      auto observer=entity(list,handle,entityStride_);std::uintptr_t services=0;
      unsigned char mode=0;std::uint32_t target=0xffffffff;
      if(observer&&read(observer+offset("observerServices"),services)&&services&&
         read(services+offset("observerMode"),mode)&&read(services+offset("observerTarget"),target)&&
         (mode==4||mode==5)&&target==localHandle)++spectators_;
    }
  }
  s.observersValid=observersValid_;s.spectators=spectators_;
}

void Cs2MemoryProvider::pollCombat(std::uintptr_t pawn,PlayerState& s) {
  if(!profile_["entityList"].isNumber())return;
  Float3 view{};float fovScale=1;
  if(!read(pawn+offset("viewOffset"),view)||!finite(view,128)||view.z<15||view.z>85)return;
  s.eye={s.position.x+view.x,s.position.y+view.y,s.position.z+view.z};
  if(read(pawn+offset("fovSensitivity"),fovScale)&&std::isfinite(fovScale)&&fovScale>.01&&fovScale<=2)
    s.radiansPerMouseCount*=fovScale;
  std::uintptr_t list=0,local=0;
  if(!read(client_+offset("entityList"),list)||!read(client_+offset("localController"),local)||!list||!local)return;
  int localIndex=0;
  // Validate the slot layout against both known local pointers. Never choose a
  // stride just because unrelated memory happens to look like health/position.
  for(std::size_t candidate : {std::size_t(0x70),std::size_t(0x78)}) {
    if(entityStride_ && candidate!=entityStride_)continue;
    for(unsigned i=1;i<=64;++i)if(entity(list,i,candidate)==local) {
      std::uint32_t h=0xffffffff;
      if(read(local+offset("controllerPawn"),h)&&entity(list,h,candidate)==pawn) {localIndex=static_cast<int>(i);entityStride_=candidate;break;}
    }
    if(localIndex)break;
  }
  if(!localIndex){entityStride_=0;opponents_.clear();return;}
  std::uintptr_t services=0;std::uint32_t active=0xffffffff;
  if(read(pawn+offset("weaponServices"),services)&&read(services+offset("activeWeapon"),active)) {
    auto weapon=entity(list,active,entityStride_);unsigned short item=0;int ammo=-1;unsigned char scoped=0;
    if(weapon&&read(weapon+offset("itemId"),item)&&read(weapon+offset("clip"),ammo)&&ammo>=-1&&ammo<300) {
      read(pawn+offset("scoped"),scoped);s.weapon={item,ammo,scoped!=0,true};
      unsigned char reloading=0;
      if(profile_["reloading"].isNumber()&&read(weapon+offset("reloading"),reloading))s.weapon.reloading=reloading!=0;
    }
  }
  if(s.timestamp-lastCombatPoll_<.025) {s.opponents=opponents_;s.combatDataValid=true;return;}
  lastCombatPoll_=s.timestamp;opponents_.clear();
  for(unsigned i=1;i<=64;++i) {
    auto controller=entity(list,i,entityStride_);if(!controller||controller==local)continue;
    std::uint32_t handle=0xffffffff;
    if(!read(controller+offset("controllerPawn"),handle))continue;
    auto target=entity(list,handle,entityStride_);if(!target||target==pawn)continue;
    std::uint32_t owner=0xffffffff;
    if(!read(target+offset("pawnController"),owner)||entity(list,owner,entityStride_)!=controller)continue;
    int health=0;unsigned char life=0,immune=0,dormant=1;std::uintptr_t node=0;
    Float3 pos{},targetView{},velocity{};std::uint64_t spotted=0;
    if(!read(target+offset("health"),health)||health<=0||health>500||!read(target+offset("life"),life)||life!=0||
       !read(target+offset("immunity"),immune)||!read(target+offset("sceneNode"),node)||
       !read(node+offset("dormant"),dormant)||dormant||!read(node+offset("sceneOrigin"),pos)||!finite(pos,32768)||
       !read(target+offset("viewOffset"),targetView)||!finite(targetView,128)||targetView.z<15||targetView.z>85)continue;
    read(target+offset("velocity"),velocity);if(!finite(velocity,10000))velocity={};
    read(target+offset("spottedState")+offset("spottedMask"),spotted);
    Opponent opponent;opponent.id=handle;opponent.alive=true;opponent.immune=immune!=0;opponent.dormant=false;
    opponent.position={pos.x,pos.y,pos.z};opponent.aimPoint={pos.x,pos.y,pos.z+targetView.z*.76};
    opponent.headPoint={pos.x,pos.y,pos.z+targetView.z-1};opponent.headValid=true;
    opponent.velocity={velocity.x,velocity.y,velocity.z};opponent.timestamp=s.timestamp;
    opponent.spottedByLocal=(spotted&(std::uint64_t(1)<<(localIndex-1)))!=0;
    Float3 shotAngles{};int shots=0;
    if(profile_["enemyEyeAngles"].isNumber()&&read(target+offset("enemyEyeAngles"),shotAngles)&&finite(shotAngles,361)&&
       std::abs(shotAngles.x)<=90&&read(target+offset("shotsFired"),shots)&&shots>=0&&shots<200) {
      opponent.shotDataValid=true;opponent.shotsFired=shots;
      opponent.shotYaw=shotAngles.y*kPi/180;opponent.shotPitch=shotAngles.x*kPi/180;
    }
    opponents_.push_back(opponent);
  }
  s.opponents=opponents_;s.combatDataValid=true;
}
}
