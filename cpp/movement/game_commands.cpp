#include "movement/game_commands.h"
#include <TlHelp32.h>
#include <array>
#include <cmath>
#include <utility>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include "core/json.h"
#include "state/process_target.h"
namespace cs2n {
bool GameCommands::validateProfile(const JsonValue& p, std::string& error) {
 for(const char* key : {"timestamp", "size", "object", "vtable", "function"}) {
  const auto& v=p[key]; const double n=v.asNumber();
  if(!v.isNumber() || !std::isfinite(n) || n<=0 || n>4294967295.0 || std::floor(n)!=n) {
   error=std::string("Invalid command profile field: ")+key;return false;
  }
 }
 const auto size=p["size"].asInt();
 // Validate the entire spans read by the existing verifier, including slot 51.
 for(const auto& span : {std::pair{"object",8LL},std::pair{"vtable",52LL*8},std::pair{"function",32LL}}) {
  const auto offset=p[span.first].asInt();
  if(offset>=size || span.second>size-offset) {error=std::string("Command profile span outside module: ")+span.first;return false;}
 }
 const auto& prefix=p["prefix"].asString();
 if(prefix.size()!=64 || prefix.find_first_not_of("0123456789abcdef")!=std::string::npos) {
  error="Invalid command profile prefix";return false;
 }
 error.clear();return true;
}
bool GameCommands::allowed(const std::string& c){
 return c=="jointeam 2 1"||c=="cl_dm_buyrandomweapons 0; buy ak47; buy deagle; slot1"||c=="slot1"||c=="slot2"||c=="disconnect";
}
bool GameCommands::read(std::uintptr_t a,void* out,std::size_t size)const{
 SIZE_T n=0;return process_&&ReadProcessMemory(process_,reinterpret_cast<void*>(a),out,size,&n)&&n==size;
}
GameCommands::GameCommands(DWORD pid, std::uint64_t created){
 std::ifstream f("assets/game-commands.json");std::ostringstream s;s<<f.rdbuf();JsonValue p;std::string e;
 if(!f || !JsonValue::parse(s.str(),p,e)){error_="Command profile missing or malformed";return;}
 if(!validateProfile(p,error_))return;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snap==INVALID_HANDLE_VALUE){error_="Command module enumeration failed";return;}
 MODULEENTRY32W m{};m.dwSize=sizeof(m);
 if(Module32FirstW(snap,&m))do{if(_wcsicmp(m.szModule,L"engine2.dll")==0){base_=reinterpret_cast<std::uintptr_t>(m.modBaseAddr);break;}}while(Module32NextW(snap,&m));
 CloseHandle(snap);
 if(!base_){error_="Engine module unavailable";return;}
 process_=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE|SYNCHRONIZE,FALSE,pid);
 if(!process_){error_="Command process access failed";return;}
 struct CloseOnFailure {
  HANDLE& handle; bool keep=false;
  ~CloseOnFailure(){if(!keep&&handle){CloseHandle(handle);handle=nullptr;}}
 } guard{process_};
 if(created&&!sameProcess(process_,{pid,created})){error_="Command target identity changed";CloseHandle(process_);process_=nullptr;return;}
 IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
 if(!read(base_,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>4096||
    !read(base_+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
    nt.FileHeader.TimeDateStamp!=p["timestamp"].asInt()||nt.OptionalHeader.SizeOfImage!=p["size"].asInt()){
  error_="Engine version does not match command profile";return;}
 object_=base_+p["object"].asInt();function_=base_+p["function"].asInt();
 std::uintptr_t vt=0,fn=0;std::array<unsigned char,32> bytes{};
 const std::string expected=p["prefix"].asString();
 if(!read(object_,&vt,8)||vt!=base_+p["vtable"].asInt()||
    !read(vt+51*8,&fn,8)||fn!=function_||!read(function_,bytes.data(),bytes.size())||expected.size()!=64){
  error_="Command function validation failed";return;}
 const char* hex="0123456789abcdef";std::string actual;
 for(auto b:bytes){actual+=hex[b>>4];actual+=hex[b&15];}
 if(actual!=expected){error_="Command bytes changed";return;}
 guard.keep=true;
}
GameCommands::~GameCommands(){if(process_)CloseHandle(process_);}
bool GameCommands::available()const{return process_&&error_.empty()&&!failed_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
bool GameCommands::send(const std::string& c){
 if(!allowed(c)||!available())return false;
 // Call the verified engine interface's queue method with local slot 0,
 // the console-command flag, zero delay and null auxiliary context.
 // x64 ABI: 32-byte shadow space plus two stack arguments, aligned at call.
 std::vector<unsigned char> code{0x48,0x83,0xec,0x38,0x48,0xb9};
 auto qword=[&](std::uint64_t v){for(int i=0;i<8;++i)code.push_back(static_cast<unsigned char>(v>>(i*8)));};
 qword(object_);code.insert(code.end(),{0x31,0xd2,0x49,0xb8});
 void* allocation=VirtualAllocEx(process_,nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!allocation){error_="Command allocation failed";return false;}
 const auto address=reinterpret_cast<std::uintptr_t>(allocation);
 qword(address+4096);code.insert(code.end(),{0x41,0xb9,0x01,0x00,0x00,0x00,0x31,0xc0,0x48,0x89,0x44,0x24,0x20,0x48,0x89,0x44,0x24,0x28,0x48,0xb8});
 qword(function_);code.insert(code.end(),{0xff,0xd0,0x31,0xc0,0x48,0x83,0xc4,0x38,0xc3});
 SIZE_T n=0;DWORD old=0;
 bool ok=WriteProcessMemory(process_,allocation,code.data(),code.size(),&n)&&n==code.size()&&
  WriteProcessMemory(process_,reinterpret_cast<void*>(address+4096),c.c_str(),c.size()+1,&n)&&n==c.size()+1&&
  VirtualProtectEx(process_,allocation,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(process_,allocation,code.size());
 if(!ok){VirtualFreeEx(process_,allocation,0,MEM_RELEASE);error_="Command preparation failed";return false;}
 HANDLE thread=CreateRemoteThread(process_,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(allocation),nullptr,0,nullptr);
 if(!thread){VirtualFreeEx(process_,allocation,0,MEM_RELEASE);error_="Command thread creation failed";return false;}
 const DWORD wait=WaitForSingleObject(thread,1000);DWORD result=~0u;
 if(wait==WAIT_OBJECT_0){GetExitCodeThread(thread,&result);VirtualFreeEx(process_,allocation,0,MEM_RELEASE);}
 // Never free a timed-out thread's code/data or terminate an engine-owned call.
 // Disable this bridge after failure, limiting retained memory to one allocation.
 CloseHandle(thread);
 if(wait!=WAIT_OBJECT_0||result!=0){failed_=true;error_="Command call failed or timed out; restart Navigator and CS2";return false;}
 return true;
}
}

