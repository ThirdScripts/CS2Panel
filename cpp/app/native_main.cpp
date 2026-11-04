#include <Windows.h>
#include <mmsystem.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
#include "app/engine.h"
#include "core/log.h"
#include "core/win32.h"
#include "movement/windows_input.h"
#include "movement/memory_input.h"
#include "navigation/navmesh_io.h"
#include "state/cs2_memory.h"
#include "state/gsi_provider.h"
#include "ui/main_window.h"

namespace {
struct ScopedHandle {
  HANDLE value;
  explicit ScopedHandle(HANDLE h) : value(h) {}
  ~ScopedHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;
};
struct TimerPeriod {
  bool active = timeBeginPeriod(1) == TIMERR_NOERROR;
  ~TimerPeriod() { if (active) timeEndPeriod(1); }
};
}

#ifdef CS2N_INTEGRATED
int WINAPI cs2NavigatorMain(HINSTANCE,HINSTANCE,PWSTR commandLine,int) {
#else
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR commandLine,int) {
#endif
  using namespace cs2n;
  ProcessTarget target{};bool requestedBackground=false,stopBound=false,panelMode=false;unsigned gsiPort=0;std::string boundToken;
  try {
    std::wistringstream args(commandLine);std::wstring arg;
    bool gotPid=false,gotCreated=false;
    while(args>>arg){
      if(arg==L"--panel"){panelMode=true;continue;}
      if(arg==L"--background-memory"){requestedBackground=true;continue;}
      if(arg==L"--stop-bound"){stopBound=true;continue;}
      if(arg==L"--gsi-token"){std::wstring value;if(!(args>>value)||value.size()!=32||value.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos)throw std::runtime_error("Invalid GSI token");boundToken.assign(value.begin(),value.end());continue;}
      if(arg==L"--gsi-port"){unsigned value=0;if(!(args>>value)||value<1024||value>65535||gsiPort)throw std::runtime_error("Invalid GSI port");gsiPort=value;continue;}
      if(arg!=L"--pid"&&arg!=L"--created")throw std::runtime_error("Unknown launch option");
      std::wstring value;if(!(args>>value)||value.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Expected numeric target identity");
      auto number=std::stoull(value);if(!number)throw std::runtime_error("Target identity cannot be zero");
      if(arg==L"--pid"){if(gotPid||number>std::numeric_limits<DWORD>::max())throw std::runtime_error("Invalid PID");target.pid=DWORD(number);gotPid=true;}
      else{if(gotCreated)throw std::runtime_error("Duplicate creation time");target.created=number;gotCreated=true;}
    }
    if(gotPid!=gotCreated)throw std::runtime_error("Both --pid and --created are required");
    if(target.pid&&!stopBound&&(!gsiPort||boundToken.empty()))throw std::runtime_error("Bound clients require their own GSI port and token");
    if(stopBound&&!target.pid)throw std::runtime_error("Stop requires an explicit binding");
    if(target.pid){HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target.pid);bool valid=sameProcess(h,target);if(h)CloseHandle(h);if(!valid)throw std::runtime_error("Target exited or identity changed");}
  }catch(const std::exception& e){MessageBoxA(nullptr,e.what(),"Invalid process binding",MB_ICONERROR);return 1;}
  const auto stopName=L"Local\\CS2NavigatorStop-"+std::to_wstring(target.pid)+L"-"+std::to_wstring(target.created);
  if(stopBound){HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,stopName.c_str());if(!event)return 2;bool ok=SetEvent(event)!=FALSE;CloseHandle(event);return ok?0:3;}
  const auto mutexName=std::wstring(L"Local\\CS2NavigatorCpp")+(target.pid?L"-"+std::to_wstring(target.pid)+L"-"+std::to_wstring(target.created):L"");
  HANDLE singleton=CreateMutexW(nullptr,TRUE,mutexName.c_str());
  if(!singleton){MessageBoxW(nullptr,L"Cannot acquire instance lock",L"CS2 Navigator",MB_ICONERROR);return 1;}
  if(GetLastError()==ERROR_ALREADY_EXISTS) {
    const auto title=L"CS2 Navigator — PID "+std::to_wstring(target.pid);
    HWND existing=target.pid?FindWindowW(L"CS2NavigatorWindow",title.c_str()):FindWindowW(L"CS2NavigatorWindow",nullptr);
    if(existing){if(!panelMode){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}}
    else MessageBoxW(nullptr,L"Навигатор для этого клиента запускается, но его окно пока недоступно.",L"CS2 Navigator",MB_OK);
    CloseHandle(singleton);return 0;
  }
  ScopedHandle singletonOwner(singleton);
  HANDLE stopEvent=target.pid?CreateEventW(nullptr,TRUE,FALSE,stopName.c_str()):nullptr;
  ScopedHandle stopOwner(stopEvent);
  if(target.pid&&!stopEvent)return 1;
  win32::enableDpiAwareness();TimerPeriod timerPeriod;
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
  auto base=std::filesystem::path(exe).parent_path();
#ifdef CS2N_INTEGRATED
  base/=L"bot";
#endif
  std::filesystem::current_path(base);
  if(std::filesystem::exists("assets/.profile-update.lock")||std::filesystem::exists("assets/.profile-update-pending.json")){
    MessageBoxW(nullptr,L"Обновление профилей не завершено. Дождитесь окончания или выполните восстановление.",L"CS2 Navigator",MB_ICONERROR);return 1;
  }
  Logger::instance().startFile("logs","navigator-cpp-"+std::to_string(target.pid));
  struct MapAssets {const char* name; const char* stem; NavMesh navigation; VisibilityMesh visibility;};
  MapAssets maps[]={{"de_dust2","dust2"},{"de_mirage","mirage"},{"de_vertigo","vertigo"},{"de_cache","cache"},{"de_inferno","inferno"}};
  std::string error;
  for(auto& map:maps) {
    const std::string prefix=std::string("assets/")+map.stem;
    if(!NavMeshIo::loadFile(prefix+".nav.json",map.navigation,error,false) || map.navigation.name()!=map.name ||
       !map.visibility.load(prefix+".mesh",error)) {
      const auto message=std::string(map.name)+": "+error;
      MessageBoxA(nullptr,message.c_str(),"Cannot load map assets",MB_ICONERROR);return 1;
    }
    Logger::instance().info(std::string("Map loaded: ")+map.name+" areas="+std::to_string(map.navigation.areaCount()));
  }
  GsiConfig gsiConfig; std::ifstream token("assets/gsi-token.txt");std::getline(token,gsiConfig.token);
  if(target.pid){gsiConfig.port=static_cast<std::uint16_t>(gsiPort);gsiConfig.token=boundToken;}
  gsiConfig.staleAfterSeconds=3;
  gsiConfig.requiredMap.clear(); // Engine chooses only registered maps.
  auto memory=std::make_shared<Cs2MemoryProvider>("assets/memory.json",true,target);
  auto gsi=std::make_shared<GsiProvider>(gsiConfig);
  PlayerStateProviderPtr provider=std::make_shared<CompositeProvider>(gsi,memory);
  NavigatorConfig navConfig;navConfig.minGoalDistance=650;
  Navigator navigator(nullptr,navConfig);
#ifdef CS2N_BACKGROUND_DEFAULT
  const bool background=true;
#else
  const bool background=requestedBackground||target.pid;
#endif
  std::unique_ptr<IInputBackend> input;
  if(background){auto experimental=std::make_unique<MemoryInput>(target);
    if(!experimental->error().empty()){MessageBoxA(nullptr,experimental->error().c_str(),"Background input unavailable",MB_ICONERROR);return 1;}
    input=std::move(experimental);
  }else input=std::make_unique<WindowsInput>();
  LookConfig look;look.mode=AimMode::Spring;look.springNaturalFreq=14;look.maxAngularSpeedDeg=300;look.accelerationDeg=2400;
  EngineConfig config;config.controlHz=144;config.recoveryAttempts=6;
  config.stuck.stoppedSeconds=2;config.stuck.noProgressSeconds=4;
  auto engine=std::make_shared<Engine>(input.get(),provider,navigator,look,MoveConfig{},config);
  for(const auto& map:maps)engine->registerMap(map.name,&map.navigation,&map.visibility);
  std::unique_ptr<GameAudio> audio;std::unique_ptr<RadarObserver> radarObserver;
  if(!target.pid){audio=std::make_unique<GameAudio>();radarObserver=std::make_unique<RadarObserver>();engine->setPerception(audio.get(),radarObserver.get());}
  engine->setIntensity(.5);
  MainWindow window;
  if(!window.create(1120,800,background,target.pid)) return 1;
  HANDLE watchedTarget=target.pid?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,target.pid):nullptr;
  if(target.pid&&(!watchedTarget||!sameProcess(watchedTarget,target))){if(watchedTarget)CloseHandle(watchedTarget);return 1;}
  ScopedHandle watchedOwner(watchedTarget);
  window.setEngine(engine);window.setStopEvent(stopEvent);window.setTargetProcess(watchedTarget);if(!panelMode)window.show();
  window.setDeathmatchLauncher(std::make_unique<session::DeathmatchLauncher>(target,gsi));
  Logger::instance().info(background?"EXPERIMENTAL BACKGROUND MEMORY INPUT (writes game memory)":"Foreground SendInput mode");
  Logger::instance().info("C++ ready: F8 start/stop, F10 emergency stop. No input on launch.");
  int result=window.runLoop();
  window.setTargetProcess(nullptr);
  engine->stop();engine.reset();
  return result;
}
