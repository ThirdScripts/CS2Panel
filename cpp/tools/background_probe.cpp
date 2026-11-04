#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include "state/cs2_memory.h"
#include "movement/game_commands.h"
namespace {
HWND label=nullptr,target=nullptr;DWORD gamePid=0;
std::unique_ptr<cs2n::Cs2MemoryProvider> provider;
cs2n::PlayerState before;bool pending=false;int kind=0;std::ofstream logFile;
void report(const std::string& s){logFile<<s<<std::endl;SetWindowTextA(label,s.c_str());}
bool sameTarget(){DWORD pid=0;GetWindowThreadProcessId(target,&pid);return IsWindow(target)&&pid==gamePid;}
void release(){if(pending&&sameTarget()&&(kind==1||kind==3||kind==4)){
 const unsigned key=kind==1?'W':kind==3?VK_F7:'2',scan=kind==1?0x11:kind==3?0x41:0x03;
 PostMessageW(target,WM_KEYUP,key,LPARAM(1u|(scan<<16)|(3u<<30)));}}
BOOL CALLBACK findWindow(HWND w,LPARAM){DWORD pid=0;GetWindowThreadProcessId(w,&pid);
 if(pid==gamePid&&IsWindowVisible(w)&&GetWindow(w,GW_OWNER)==nullptr){target=w;return FALSE;}return TRUE;}
void begin(HWND self,int command){
 if(pending)return;
 gamePid=0;int count=0;auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snap==INVALID_HANDLE_VALUE){report("Process enumeration failed");return;}
 PROCESSENTRY32W e{};e.dwSize=sizeof(e);
 if(Process32FirstW(snap,&e))do{if(_wcsicmp(e.szExeFile,L"cs2.exe")==0){gamePid=e.th32ProcessID;++count;}}while(Process32NextW(snap,&e));CloseHandle(snap);
 if(count!=1){report("Requires exactly one CS2 process");return;}
 target=nullptr;EnumWindows(findWindow,0);
 if(!target){report("No game window");return;}
 auto p=provider->poll();if(!p||!p->alive){report("No live player state; retry after respawn");return;}
 DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
 if(foregroundPid==gamePid){report("Test requires CS2 in background");return;}
 before=*p;kind=command;pending=true;
 report(cs2n::format("START pid=%lu minimized=%d foreground=%lu test=%d",gamePid,IsIconic(target),foregroundPid,kind));
 if(kind==5||kind==6){cs2n::GameCommands commands(gamePid);
  if(!commands.send(kind==5?"jointeam 2":"cl_dm_buyrandomweapons 0; buy ak47; buy deagle; slot1")){pending=false;report(commands.error());return;}}
 else if(kind==1||kind==3||kind==4){const unsigned key=kind==1?'W':kind==3?VK_F7:'2',scan=kind==1?0x11:kind==3?0x41:0x03;
 if(!PostMessageW(target,WM_KEYDOWN,key,1|(scan<<16))){pending=false;report("PostMessage failed");return;}}
 else {RECT r{};GetClientRect(target,&r);const int x=(r.right-r.left)/2,y=(r.bottom-r.top)/2;
  PostMessageW(target,WM_MOUSEMOVE,0,MAKELPARAM(x,y));PostMessageW(target,WM_MOUSEMOVE,0,MAKELPARAM(x+80,y));}
 SetTimer(self,1,180,nullptr);
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){
 if(m==WM_CREATE){
  CreateWindowW(L"BUTTON",L"Test background W (180 ms)",WS_CHILD|WS_VISIBLE,10,10,250,35,w,(HMENU)1,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Test background mouse move",WS_CHILD|WS_VISIBLE,270,10,250,35,w,(HMENU)2,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Test background F7 buy",WS_CHILD|WS_VISIBLE,10,55,250,35,w,(HMENU)3,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Test background pistol slot",WS_CHILD|WS_VISIBLE,270,55,250,35,w,(HMENU)4,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Direct queue: join T",WS_CHILD|WS_VISIBLE,530,10,250,35,w,(HMENU)5,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Direct queue: buy AK-47",WS_CHILD|WS_VISIBLE,530,55,250,35,w,(HMENU)6,nullptr,nullptr);
  label=CreateWindowW(L"STATIC",L"Diagnostic only. Navigator is unchanged. No automatic input.",WS_CHILD|WS_VISIBLE,10,100,780,90,w,nullptr,nullptr,nullptr);return 0;
 }
 if(m==WM_COMMAND){begin(w,LOWORD(a));return 0;}
 if(m==WM_TIMER&&a==1){KillTimer(w,1);release();SetTimer(w,2,400,nullptr);return 0;}
 if(m==WM_TIMER&&a==2){KillTimer(w,2);auto p=provider->poll();pending=false;
  if(!p||!p->alive){report("INCONCLUSIVE: player dead or state unavailable");return 0;}
  report(cs2n::format("RESULT test=%d minimized=%d distance=%.3f yaw_delta=%.3f weapon=%d/%d team=%d/%d health=%d/%d",kind,IsIconic(target),cs2n::distance(before.position.xy(),p->position.xy()),cs2n::wrapAngle(p->yaw-before.yaw)*180/cs2n::kPi,before.weapon.item,p->weapon.item,before.team,p->team,before.health,p->health));return 0;
 }
 if(m==WM_DESTROY){release();PostQuitMessage(0);return 0;}
 return DefWindowProcW(w,m,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::filesystem::current_path(std::filesystem::path(path).parent_path());
 logFile.open("background-probe.log",std::ios::app);provider=std::make_unique<cs2n::Cs2MemoryProvider>("assets/memory.json");
 WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=proc;c.lpszClassName=L"CS2BackgroundProbe";c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
 auto w=CreateWindowW(c.lpszClassName,L"CS2 Background Input Probe",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,830,250,nullptr,nullptr,instance,nullptr);
 if(!w)return 1;ShowWindow(w,SW_SHOW);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
}


