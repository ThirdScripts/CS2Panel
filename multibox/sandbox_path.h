#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace sandbox {
using QueryBoxPath = LONG (__stdcall *)(const WCHAR*,WCHAR*,WCHAR*,WCHAR*,ULONG*,ULONG*,ULONG*);
inline std::wstring queryRoot(QueryBoxPath query,const std::wstring& box) {
 if(!query)throw std::runtime_error("Sandboxie path API unavailable");
 auto failure=[](LONG status){std::ostringstream s;s<<"Cannot resolve sandbox path for Steam bootstrap (NTSTATUS 0x"<<std::hex<<static_cast<ULONG>(status)<<")";return std::runtime_error(s.str());};
 ULONG bytes=0;
 LONG status=query(box.c_str(),nullptr,nullptr,nullptr,&bytes,nullptr,nullptr);
 if(status!=0 && static_cast<ULONG>(status)!=0xC0000023u)throw failure(status);
 // Sandboxie casts the byte capacity to USHORT (UNICODE_STRING.MaximumLength).
 // 32768 WCHARs = 65536 bytes wraps to zero. Query the actual capacity first.
 for(int attempt=0;attempt<3;++attempt){
  if(bytes<sizeof(WCHAR)||bytes>65534||bytes%sizeof(WCHAR))throw std::runtime_error("Invalid Sandboxie path byte length");
  std::vector<WCHAR> path(bytes/sizeof(WCHAR),L'\0');
  ULONG supplied=bytes;
  status=query(box.c_str(),path.data(),nullptr,nullptr,&bytes,nullptr,nullptr);
  if(static_cast<ULONG>(status)==0xC0000023u && bytes>supplied)continue;
  if(status!=0)throw failure(status);
  const auto end=std::find(path.begin(),path.end(),L'\0');
  if(end==path.end()||end==path.begin()||bytes>supplied)throw std::runtime_error("Invalid Sandboxie path response");
  return std::wstring(path.begin(),end);
 }
 throw std::runtime_error("Sandboxie path changed repeatedly; retry launch");
}
}
