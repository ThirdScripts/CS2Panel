#include "session/practice_launcher.h"
#include "session/background_click.h"
#include "session/practice_vision.h"
#include "session/launch_confirmation.h"
#include "vision/image.h"
#include "tools/wgc_probe.h"
#include <TlHelp32.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
namespace cs2n::session {
namespace {
struct Close{HANDLE h=nullptr;~Close(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Search{DWORD pid;HWND window=nullptr;unsigned count=0;};
BOOL CALLBACK findWindow(HWND w,LPARAM arg){auto&s=*reinterpret_cast<Search*>(arg);DWORD pid=0;GetWindowThreadProcessId(w,&pid);
 if(pid==s.pid&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){s.window=w;++s.count;}return TRUE;}
bool patch(const Image& frame,const char* name,int x,int y){
 Image ref;std::string error;
 if(!Image::loadPnm(std::string("assets/network-menu/")+name+".ppm",ref,error))throw std::runtime_error("Missing Deathmatch menu calibration");
 return matchesPracticePatch(frame,ref,x,y);
}
}
void DeathmatchLauncher::report(LaunchStatus s,std::string text){std::lock_guard lock(mutex_);message_=std::move(text);state_=s;}
void DeathmatchLauncher::cancel(){std::lock_guard lock(dispatch_);cancelled_=true;if(state_==LaunchStatus::Running||state_==LaunchStatus::Ready)report(LaunchStatus::Cancelled,"Запуск Deathmatch остановлен");}
void DeathmatchLauncher::start(){if(state_==LaunchStatus::Running)return;if(thread_.joinable())thread_.join();cancelled_=false;report(LaunchStatus::Running,"Проверка меню Deathmatch");thread_=std::thread([this]{run();});}
void DeathmatchLauncher::run(){
 try{
  auto wait=[&](int ms){for(int t=0;t<ms;t+=50){if(cancelled_)throw std::runtime_error("Cancelled");std::this_thread::sleep_for(std::chrono::milliseconds(50));}};
  ProcessTarget target=target_;
  if(!target.pid){Close snap{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(snap.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Process enumeration failed");
   PROCESSENTRY32W e{};e.dwSize=sizeof(e);unsigned count=0;if(Process32FirstW(snap.h,&e))do{if(!_wcsicmp(e.szExeFile,L"cs2.exe")){target.pid=e.th32ProcessID;++count;}}while(Process32NextW(snap.h,&e));
   if(count!=1)throw std::runtime_error("Select exactly one CS2 client");}
  Close process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target.pid)};
  if(!process.h)throw std::runtime_error("CS2 process unavailable");
  FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process.h,&c,&e,&k,&u))throw std::runtime_error("Cannot verify process identity");
  auto created=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
  if(target.created&&target.created!=created)throw std::runtime_error("CS2 identity changed");target.created=created;
  wchar_t path[32768]{};DWORD length=32768;if(!QueryFullProcessImageNameW(process.h,0,path,&length)||_wcsicmp(std::filesystem::path(path).filename().c_str(),L"cs2.exe"))throw std::runtime_error("Wrong executable");
  double previousPacket = -1;
  auto inMatch=[&]{return gsi_ && confirmsLaunchedMatch(gsi_->clientState(),previousPacket);};
  auto ready=[&](const char* text){std::lock_guard lock(dispatch_);if(!cancelled_)report(LaunchStatus::Ready,text);};
  wait(500);
  if(inMatch()){ready("Матч уже загружен");return;}
  Search search{target.pid};EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search));if(search.count!=1)throw std::runtime_error("Cannot identify a unique CS2 window");
  auto capture=[&]{
   if(cancelled_)throw std::runtime_error("Cancelled");
   if(!sameProcess(process.h,target)||IsIconic(search.window))throw std::runtime_error("CS2 must remain behind other windows, not minimized");
   DWORD owner=0;GetWindowThreadProcessId(search.window,&owner);if(owner!=target.pid)throw std::runtime_error("Window identity changed");
   RECT rect{};GetClientRect(search.window,&rect);if(rect.right!=640||rect.bottom!=480)throw std::runtime_error("Deathmatch calibration requires 640x480");
   if(GetForegroundWindow()==search.window)throw std::runtime_error("Leave CS2 behind the panel");
   auto frame=captureWgc(search.window);
   if(frame.width()!=642||frame.height()!=512)throw std::runtime_error("Window frame differs from calibration");return frame;
  };
  auto click=[&](int x,int y){std::lock_guard lock(dispatch_);if(cancelled_)throw std::runtime_error("Cancelled");std::string error;
   if(!queueBackgroundClick(process.h,target,search.window,x,y,640,480,error))throw std::runtime_error(error);};
  auto frame=capture();
  auto refreshUntil=[&](auto condition){
   wait(3000);frame=capture();return condition(frame);
  };
  // Opening Play is harmless; starting a match requires all four independent menu patches.
  if(!patch(frame,"menu-tabs",128,82)){
   if(!patch(frame,"play-main",315,37)&&!patch(frame,"play-selected",315,37))throw std::runtime_error("Play button not recognized");
   click(332,14);refreshUntil([&](const Image& f){return patch(f,"menu-tabs",128,82);});
  }
  if(!patch(frame,"menu-tabs",128,82))throw std::runtime_error("Play menu not recognized");
  if(!patch(frame,"matchmaking",220,62)){click(245,39);refreshUntil([&](const Image& f){return patch(f,"matchmaking",220,62);});}
  if(!patch(frame,"matchmaking",220,62))throw std::runtime_error("Matchmaking tab not confirmed");
  if(!patch(frame,"deathmatch",326,81)){click(351,57);refreshUntil([&](const Image& f){return patch(f,"deathmatch",326,81);});}
  if(!patch(frame,"matchmaking",220,62)||!patch(frame,"deathmatch",326,81))throw std::runtime_error("Network Deathmatch not confirmed");
  refreshUntil([&](const Image& f){return patch(f,"matchmaking",220,62)&&patch(f,"deathmatch",326,81)&&patch(f,"group-selected",137,118)&&patch(f,"group-border",137,120)&&patch(f,"start",473,477);});
  // Preserve the user's selected map group. Never select an unrecognized group automatically.
  if((!patch(frame,"group-selected",137,118)||!patch(frame,"group-border",137,120))||!patch(frame,"start",473,477))throw std::runtime_error("Selected map group / Start not recognized");
  if(!gsi_ || !gsi_->listening())throw std::runtime_error("GSI listener unavailable; search not started");
  previousPacket=gsi_->clientState().receivedAt;
  click(537,458);
  {std::lock_guard lock(dispatch_);if(!cancelled_)report(LaunchStatus::Running,"Поиск Deathmatch / загрузка: ожидаю подтверждения GSI");}
  for(int i=0;i<300;++i){wait(1000);if(!sameProcess(process.h,target))throw std::runtime_error("CS2 exited");
   if(inMatch()){ready("Матч подтверждён GSI");return;}}
  throw std::runtime_error("No fresh GSI match confirmation within 300 seconds; check CS2. Search was not retried or cancelled.");
 }catch(const winrt::hresult_error& e){report(cancelled_?LaunchStatus::Cancelled:LaunchStatus::Failed,"Capture failed: "+winrt::to_string(e.message()));}
 catch(const std::exception&e){report(cancelled_?LaunchStatus::Cancelled:LaunchStatus::Failed,cancelled_?"Запуск остановлен":e.what());}
 catch(...){report(LaunchStatus::Failed,"Unexpected menu launcher failure");}
}
}
