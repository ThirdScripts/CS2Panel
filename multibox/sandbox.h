#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <tlhelp32.h>
#include <ShlObj.h>
#include "sandbox_path.h"
#include "shared_manifest.h"
namespace sandbox {
inline std::wstring setting(const std::wstring& section,const wchar_t* key,const std::wstring& fallback){
 wchar_t exe[32768]{},value[32768]{};GetModuleFileNameW(nullptr,exe,32768);
 auto ini=std::filesystem::path(exe).parent_path()/L"panel-settings.ini";
 GetPrivateProfileStringW(section.c_str(),key,fallback.c_str(),value,32768,ini.c_str());return value;
}
inline std::wstring directory(){return setting(L"Paths",L"Sandboxie",L"C:\\Program Files\\Sandboxie-Plus")+L"\\";}
inline std::wstring nativeSteam(){return setting(L"Paths",L"NativeSteam",L"C:\\Program Files (x86)\\Steam\\steam.exe");}
inline std::wstring gameDir(){return setting(L"Paths",L"GameDir",L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive");}
inline std::wstring steam(int id){return setting(L"Slot"+std::to_wstring(id),L"Steam",setting(L"Paths",L"IsolatedSteam",L"C:\\Users\\Public\\CS2CompatWin10\\Steam\\steam.exe"));}
inline std::wstring name(int id){auto value=setting(L"Slot"+std::to_wstring(id),L"Box",L"CS2Panel_"+(id<10?std::wstring(L"0"):std::wstring())+std::to_wstring(id));if(value.empty()||value.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::wstring::npos)throw std::runtime_error("Invalid sandbox name");return value;}
// Only fixed executable paths and internally generated arguments are accepted.
inline std::string invoke(const wchar_t* exe,const std::wstring& args,bool wait=true){
 std::wstring path=directory()+exe,cmd=L"\""+path+L"\" "+args;
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
 if(!CreatePipe(&read,&write,&sa,0))throw std::runtime_error("Cannot create provider pipe");
 SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=write;si.hStdError=write;
 PROCESS_INFORMATION pi{};
 if(!CreateProcessW(path.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,directory().c_str(),&si,&pi)){CloseHandle(read);CloseHandle(write);throw std::runtime_error("Sandboxie unavailable: install provider first");}
 CloseHandle(write);CloseHandle(pi.hThread);
 if(!wait){CloseHandle(read);CloseHandle(pi.hProcess);return {};}
 std::string output;auto deadline=GetTickCount64()+5000;bool done=false;
 while(GetTickCount64()<deadline){DWORD available=0;if(PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr)&&available){char buffer[4096];DWORD n=0;if(ReadFile(read,buffer,std::min<DWORD>(available,sizeof(buffer)),&n,nullptr))output.append(buffer,n);if(output.size()>65536)break;}
  if(WaitForSingleObject(pi.hProcess,10)==WAIT_OBJECT_0){DWORD remaining=0;if(!PeekNamedPipe(read,nullptr,0,nullptr,&remaining,nullptr)||!remaining){done=true;break;}}
 }
 DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(read);CloseHandle(pi.hProcess);
 if(!done||code)throw std::runtime_error("Sandboxie command failed or timed out; inspect Sandboxie UI");return output;
}
inline std::wstring boxOf(DWORD pid){
 using Query=LONG (*)(HANDLE,WCHAR*,WCHAR*,WCHAR*,ULONG*);
 static HMODULE module=LoadLibraryW((directory()+L"SbieDll.dll").c_str());
 static Query query=module?reinterpret_cast<Query>(GetProcAddress(module,"SbieApi_QueryProcess")):nullptr;
 if(!query)throw std::runtime_error("Sandboxie process API unavailable");
 wchar_t box[34]{},image[96]{},sid[96]{};ULONG session=0;
 return query(reinterpret_cast<HANDLE>(ULONG_PTR(pid)),box,image,sid,&session)==0?box:L"";
}
inline std::vector<DWORD> pids(int id){
 std::vector<DWORD> result;auto box=name(id);HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snap==INVALID_HANDLE_VALUE)throw std::runtime_error("Process enumeration unavailable");
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 try{if(Process32FirstW(snap,&entry))do{if(boxOf(entry.th32ProcessID)==box)result.push_back(entry.th32ProcessID);}while(Process32NextW(snap,&entry));}catch(...){CloseHandle(snap);throw;}
 CloseHandle(snap);return result;
}
inline void prepare(int id){
 auto box=name(id);auto enabled=invoke(L"SbieIni.exe",L"query "+box+L" Enabled");
 if(enabled.find('y')==std::string::npos){
  invoke(L"SbieIni.exe",L"set "+box+L" Enabled y");
  invoke(L"SbieIni.exe",L"set "+box+L" ConfigLevel 10");
  if(invoke(L"SbieIni.exe",L"query "+box+L" Enabled").find('y')==std::string::npos)throw std::runtime_error("Sandbox configuration was not persisted");
 }
 // Apply to existing boxes too: Steam's libcef.dll exceeds the default migration limit.
 invoke(L"SbieIni.exe",L"set "+box+L" CopyLimitKb 1048576");
 if(invoke(L"SbieIni.exe",L"query "+box+L" CopyLimitKb").find("1048576")==std::string::npos)
  throw std::runtime_error("Sandbox file migration policy was not persisted");
 // Game binaries are shared read-only; Steam's own files remain isolated and writable.
 // Update the shared game through the primary Steam, not through a sandbox copy.
 auto rules=invoke(L"SbieIni.exe",L"query "+box+L" ReadFilePath");
 for(const auto* extension:{L"vpk",L"dll",L"exe"}){
  const auto rule=gameDir()+L"\\*."+extension;
  if(rule.find(L'\"')!=std::wstring::npos)throw std::runtime_error("Invalid shared game path");
  int size=WideCharToMultiByte(CP_UTF8,0,rule.c_str(),-1,nullptr,0,nullptr,nullptr);
  std::string encoded(size,'\0');WideCharToMultiByte(CP_UTF8,0,rule.c_str(),-1,encoded.data(),size,nullptr,nullptr);encoded.pop_back();
  if(rules.find(encoded)==std::string::npos)
   invoke(L"SbieIni.exe",L"append "+box+L" ReadFilePath \""+rule+L"\"");
 }
}
inline void launch(int id){
 prepare(id);if(!std::filesystem::exists(steam(id)))throw std::runtime_error("Isolated Steam executable is missing; check panel-settings.ini");
 // A running image inherited from the host cannot always be renamed by Steam's
 // updater. Seed only steam.exe in an idle box; never overwrite an existing copy.
 if(pids(id).empty()){
  PWSTR publicPath=nullptr;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Public,0,nullptr,&publicPath))){
   auto source=std::filesystem::weakly_canonical(steam(id));
   auto relative=source.lexically_relative(std::filesystem::weakly_canonical(publicPath));CoTaskMemFree(publicPath);
   if(!relative.empty()&&*relative.begin()!=L".."&&source.filename()==L"steam.exe"){
    static HMODULE library=LoadLibraryW((directory()+L"SbieDll.dll").c_str());
    auto query=library?reinterpret_cast<QueryBoxPath>(GetProcAddress(library,"SbieApi_QueryBoxPath")):nullptr;
    std::wstring root=queryRoot(query,name(id));
    if(root.starts_with(L"\\Device\\")){
     for(wchar_t letter=L'A';letter<=L'Z';++letter){wchar_t drive[]={letter,L':',0},device[1024]{};
      if(QueryDosDeviceW(drive,device,1024)&&root.starts_with(std::wstring(device)+L"\\")){root=std::wstring(drive)+root.substr(wcslen(device));break;}}
    }
    if(!std::filesystem::path(root).has_root_name())throw std::runtime_error("Unsupported sandbox root path");
    auto base=std::filesystem::weakly_canonical(root);
    auto destination=std::filesystem::weakly_canonical(base/L"user"/L"public"/relative);
    auto within=destination.lexically_relative(base);
    if(within.empty()||*within.begin()==L"..")throw std::runtime_error("Sandbox bootstrap path escapes sandbox");
    shared_manifest::sync(nativeSteam(),steam(id),gameDir(),destination.parent_path()/L"steamapps"/L"appmanifest_730.acf");
    if(!std::filesystem::exists(destination)){
     std::filesystem::create_directories(destination.parent_path());
     std::filesystem::copy_file(source,destination,std::filesystem::copy_options::skip_existing);
    }
   }
  }
 }
 invoke(L"Start.exe",L"/box:"+name(id)+L" \""+steam(id)+L"\"",false);
}
inline void stop(int id){invoke(L"Start.exe",L"/box:"+name(id)+L" /terminate");}
}


