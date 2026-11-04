#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <iostream>
#include "movement/game_commands.h"
int main(int argc,char** argv){
 if(argc!=2)return 2;
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::filesystem::current_path(std::filesystem::path(path).parent_path());
 HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(s==INVALID_HANDLE_VALUE)return 3;
 PROCESSENTRY32W p{};p.dwSize=sizeof(p);DWORD pid=0;int count=0;
 if(Process32FirstW(s,&p))do{if(_wcsicmp(p.szExeFile,L"cs2.exe")==0){pid=p.th32ProcessID;++count;}}while(Process32NextW(s,&p));CloseHandle(s);
 if(count!=1)return 4;
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 std::cout<<"before foreground_is_cs2="<<(foreground==pid)<<std::endl;
 cs2n::GameCommands commands(pid);const bool ok=commands.send(argv[1]);
 GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 std::cout<<"queued="<<ok<<" foreground_is_cs2="<<(foreground==pid)<<" error="<<commands.error()<<std::endl;
 return ok?0:1;
}
