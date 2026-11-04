#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include "state/process_target.h"

namespace cs2n::session {
// Transport only. The caller must validate a fresh scene before every dispatch.
// Success means that messages were queued, never that the game accepted a click.
inline bool queueBackgroundClick(HANDLE process,ProcessTarget target,HWND window,
                                int x,int y,int width,int height,std::string& error){
  DWORD owner=0;GetWindowThreadProcessId(window,&owner);
  RECT r{};
  if(!sameProcess(process,target)||!IsWindow(window)||owner!=target.pid){error="Target identity changed";return false;}
  if(IsIconic(window)){error="Minimized window: no verified fresh scene";return false;}
  if(!GetClientRect(window,&r)||r.right!=width||r.bottom!=height||
     x<0||y<0||x>=width||y>=height||width>32767||height>32767){error="Window geometry changed";return false;}
  if(GetForegroundWindow()==window){error="Background-only probe requires another foreground window";return false;}
  const LPARAM point=MAKELPARAM(x,y);
  if(!PostMessageW(window,WM_MOUSEMOVE,0,point)){error="Mouse move message refused";return false;}
  if(!PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point)){error="Mouse down message refused";return false;}
  if(!PostMessageW(window,WM_LBUTTONUP,0,point)){
    PostMessageW(window,WM_LBUTTONUP,0,point);
    error="Mouse release could not be confirmed";return false;
  }
  return true;
}
}
