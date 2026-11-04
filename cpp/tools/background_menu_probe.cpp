// Read-only background capture diagnostic. Never activates windows or sends input.
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iostream>
#include "vision/image.h"
#include "state/process_target.h"
#include "wgc_probe.h"
struct Search {DWORD pid; HWND window=nullptr; unsigned count=0;};
BOOL CALLBACK locate(HWND h,LPARAM arg){auto& s=*reinterpret_cast<Search*>(arg);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==s.pid&&IsWindowVisible(h)&&!GetWindow(h,GW_OWNER)){s.window=h;++s.count;}return TRUE;}
struct Handle {HANDLE h;~Handle(){if(h)CloseHandle(h);}};
struct Surface {HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;~Surface(){if(old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}};
int main(int argc,char** argv){
 if(argc!=3 && !(argc==4&&std::string(argv[3])=="--wgc")){std::cerr<<"Usage: background_menu_probe PID output-prefix\n";return 2;}
 try {
  std::string number=argv[1];if(number.empty()||number.find_first_not_of("0123456789")!=std::string::npos)return 2;
  auto numeric=std::stoull(number);if(!numeric||numeric>MAXDWORD)return 2;
  DWORD pid=DWORD(numeric);Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid)};
  if(!process.h)return 3;
  wchar_t path[32768]{};DWORD length=32768;
  if(!QueryFullProcessImageNameW(process.h,0,path,&length)||_wcsicmp(std::filesystem::path(path).filename().c_str(),L"cs2.exe"))return 3;
  FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(process.h,&created,&exit,&kernel,&user))return 3;
  cs2n::ProcessTarget target{pid,(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime};
  Search search{pid};EnumWindows(locate,reinterpret_cast<LPARAM>(&search));if(search.count!=1)return 4;
  HWND foreground=GetForegroundWindow();if(foreground==search.window){std::cerr<<"Background test requires another window foreground; no focus change performed\n";return 5;}
  if(argc==4){
   auto image=captureWgc(search.window);DWORD owner=0;GetWindowThreadProcessId(search.window,&owner);
   if(!cs2n::sameProcess(process.h,target)||owner!=pid||GetForegroundWindow()!=foreground)return 7;
   std::string error;if(!cs2n::Image::savePpm(image,std::string(argv[2])+".ppm",error))return 8;
   std::ofstream metadata(std::string(argv[2])+".txt");metadata<<"backend=WGC pid="<<pid<<" created="<<target.created<<" minimized="<<IsIconic(search.window)<<" width="<<image.width()<<" height="<<image.height()<<"\nScene UNKNOWN; requires visual validation.\n";
   std::cout<<"WGC frame saved without foreground change. Scene UNKNOWN.\n";return 0;
  }
  RECT rect{};if(!GetClientRect(search.window,&rect))return 4;int width=rect.right,height=rect.bottom;
  if(width<=0||height<=0||width>4096||height>4096)return 4;
  Surface surface;surface.dc=CreateCompatibleDC(nullptr);if(!surface.dc)return 6;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  void* bits=nullptr;surface.bitmap=CreateDIBSection(surface.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!surface.bitmap||!bits)return 6;
  surface.old=SelectObject(surface.dc,surface.bitmap);if(!surface.old||surface.old==HGDI_ERROR){surface.old=nullptr;return 6;}
  std::memset(bits,0,size_t(width)*height*4);
  // PrintWindow can block: caller must run this diagnostic in a bounded child process.
  bool captured=PrintWindow(search.window,surface.dc,PW_CLIENTONLY)!=FALSE;
  DWORD owner=0;GetWindowThreadProcessId(search.window,&owner);
  if(!cs2n::sameProcess(process.h,target)||owner!=pid||GetForegroundWindow()!=foreground)return 7;
  cs2n::Image image(width,height);auto pixels=static_cast<const std::uint32_t*>(bits);size_t nonblack=0;
  for(int i=0;i<image.pixelCount();++i){image.pixels()[i]=cs2n::rgbFromDib(pixels[i]);if(pixels[i]&0xffffff)++nonblack;}
  std::ofstream metadata(std::string(argv[2])+".txt");metadata<<"pid="<<pid<<" created="<<target.created<<" minimized="<<IsIconic(search.window)<<" printwindow="<<captured<<" nonblack="<<nonblack<<" pixels="<<image.pixelCount()<<"\n";
  metadata<<"Scene UNKNOWN. A returned bitmap does not establish fresh game rendering.\n";
  std::string error;if(!cs2n::Image::savePpm(image,std::string(argv[2])+".ppm",error)){std::cerr<<error;return 8;}
  std::cout<<"PrintWindow="<<captured<<" nonblack="<<nonblack<<"/"<<image.pixelCount()<<". Scene UNKNOWN.\n";
  return captured&&nonblack?0:9;
 }catch(const std::exception& e){std::cerr<<e.what();return 10;}
}
