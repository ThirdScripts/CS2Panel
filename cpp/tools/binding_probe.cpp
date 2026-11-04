#include <Windows.h>
#include <iostream>
#include <filesystem>
#include "state/cs2_memory.h"
int main(int argc,char** argv){
 try{
  if(argc!=3)return 2;
  auto pid=std::stoul(argv[1]);auto created=std::stoull(argv[2]);
  if(!pid||pid>MAXDWORD||!created)return 2;
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::filesystem::current_path(std::filesystem::path(exe).parent_path());
  cs2n::Cs2MemoryProvider provider("assets/memory.json",false,{DWORD(pid),created});
  auto state=provider.poll();
  std::cout<<"requested="<<pid<<" attached="<<provider.processId()<<" connected="<<provider.connected()<<" state="<<bool(state)<<" diagnostic="<<provider.error()<<'\n';
  return provider.connected()&&provider.processId()==pid?0:1;
 }catch(...){return 2;}
}
