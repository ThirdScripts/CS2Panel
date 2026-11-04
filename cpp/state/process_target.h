#pragma once
#include <Windows.h>
#include <cstdint>
namespace cs2n {
struct ProcessTarget { DWORD pid=0; std::uint64_t created=0; };
inline std::uint64_t processCreation(HANDLE process) {
 FILETIME born{},exit{},kernel{},user{};
 if(!GetProcessTimes(process,&born,&exit,&kernel,&user))return 0;
 return (std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime;
}
inline bool sameProcess(HANDLE process,ProcessTarget target) {
 return process&&target.pid&&target.created&&GetProcessId(process)==target.pid&&
  WaitForSingleObject(process,0)==WAIT_TIMEOUT&&processCreation(process)==target.created;
}
}
