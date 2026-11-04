#include "sandbox_path.h"
#include <iostream>
int mode=0,calls=0;
LONG __stdcall fake(const WCHAR*,WCHAR* out,WCHAR*,WCHAR*,ULONG* bytes,ULONG*,ULONG*){
 ++calls;
 if(mode==1){*bytes=65536;return 0;}
 if(mode==2)return LONG(0xC0000022u);
 if(mode==3){*bytes=3;return 0;}
 if(!out){*bytes=8;return 0;}
 if(*bytes>65534)throw std::runtime_error("USHORT overflow");
 if(mode==4 && calls==2){*bytes=16;return LONG(0xC0000023u);}
 if(mode==5){for(unsigned i=0;i<*bytes/2;++i)out[i]=L'x';return 0;}
 out[0]=L'C';out[1]=L':';out[2]=L'\\';out[3]=0;return 0;
}
int wmain(int argc,wchar_t** argv){
 try{
  if(argc==3){
   auto lib=LoadLibraryW(argv[1]);if(!lib)return 10;
   auto api=reinterpret_cast<sandbox::QueryBoxPath>(GetProcAddress(lib,"SbieApi_QueryBoxPath"));
   std::wcout<<sandbox::queryRoot(api,argv[2])<<L"\n";FreeLibrary(lib);return 0;
  }
  for(mode=0;mode<6;++mode){
   calls=0;bool rejected=false;
   try{if(sandbox::queryRoot(fake,L"test")!=L"C:\\")return 2;}
   catch(const std::runtime_error&){rejected=true;}
   if(rejected!=(mode!=0&&mode!=4))return 3;
  }
  bool rejected=false;try{sandbox::queryRoot(nullptr,L"test");}catch(const std::runtime_error&){rejected=true;}
  if(!rejected)return 4;
  std::cout<<"7 sandbox path scenarios passed\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
