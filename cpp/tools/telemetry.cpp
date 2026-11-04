#include <chrono>
#include <iostream>
#include <thread>
#include <Windows.h>
#include "state/cs2_memory.h"
int main(int argc,char** argv) {
  cs2n::Cs2MemoryProvider memory(argc>1?argv[1]:"assets/memory.json");
  for(int i=0;i<20;++i) {
    auto s=memory.poll();
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    struct WindowStatus{DWORD pid;bool minimized=false;} window{memory.processId()};
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL{auto& w=*reinterpret_cast<WindowStatus*>(data);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
      if(pid==w.pid&&IsWindowVisible(hwnd)&&!GetWindow(hwnd,GW_OWNER)){w.minimized=IsIconic(hwnd)!=FALSE;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&window));
    if(s) std::cout<<"foreground="<<(foregroundPid==memory.processId())<<" minimized="<<window.minimized<<" alive="<<s->alive<<" xyz="<<s->position.x<<","<<s->position.y<<","<<s->position.z<<" yaw="<<s->yaw*180/cs2n::kPi<<" velocity="<<s->velocity.length()<<" sensitivity="<<memory.sensitivity()<<" combat="<<s->combatDataValid<<" opponents="<<s->opponents.size()<<" weapon="<<s->weapon.item<<" ammo="<<s->weapon.ammo<<" eyeZ="<<s->eye.z<<std::endl;
    else std::cout<<memory.error()<<std::endl;
    if(s) std::cout<<"team="<<s->team<<" observers_valid="<<s->observersValid<<" spectators="<<s->spectators<<std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  return memory.connected()?0:1;
}
