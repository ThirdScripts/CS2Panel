#include <Windows.h>
#include <TlHelp32.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <QJsonDocument>
#include <QJsonObject>
#include "weekly_drop.h"
#include "vision/image.h"
#include "tools/wgc_probe.h"
namespace fs=std::filesystem;
namespace {
HWND combo,label,startButton,stopButton,markButton;std::thread worker;std::atomic<bool> stop{false},active{false},mark{false};
std::mutex mutex;std::wstring status=L"Выбери CS2 и начни запись до повышения уровня.";fs::path folder;std::vector<DWORD> pids;
void message(std::wstring s){std::lock_guard lock(mutex);status=std::move(s);}
struct Handle{HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Window{DWORD pid;HWND hwnd=nullptr;unsigned count=0;};
BOOL CALLBACK findWindow(HWND w,LPARAM l){auto& s=*reinterpret_cast<Window*>(l);DWORD id=0;GetWindowThreadProcessId(w,&id);if(id==s.pid&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){s.hwnd=w;++s.count;}return TRUE;}
void png(const cs2n::Image& image,const fs::path& path){
 Gdiplus::Bitmap bitmap(image.width(),image.height(),PixelFormat32bppARGB);
 Gdiplus::BitmapData data{};Gdiplus::Rect rect(0,0,image.width(),image.height());
 if(bitmap.LockBits(&rect,Gdiplus::ImageLockModeWrite,PixelFormat32bppARGB,&data)!=Gdiplus::Ok)throw std::runtime_error("PNG bitmap lock failed");
 for(int y=0;y<image.height();++y){auto row=reinterpret_cast<unsigned*>(static_cast<char*>(data.Scan0)+y*data.Stride);for(int x=0;x<image.width();++x){auto p=image.at(x,y);row[x]=0xff000000u|(unsigned(p.r)<<16)|(unsigned(p.g)<<8)|p.b;}}
 bitmap.UnlockBits(&data);CLSID codec{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 if(bitmap.Save(path.c_str(),&codec)!=Gdiplus::Ok)throw std::runtime_error("PNG write failed");
}
void record(DWORD pid,int maxSeconds=1800){
 try{
 Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid)};
 if(!process.h)throw std::runtime_error("Cannot open CS2 for reading");
 wchar_t exe[32768]{};DWORD length=32768;if(!QueryFullProcessImageNameW(process.h,0,exe,&length)||_wcsicmp(fs::path(exe).filename().c_str(),L"cs2.exe"))throw std::runtime_error("Selected process is not CS2");
 FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process.h,&c,&e,&k,&u))throw std::runtime_error("Cannot verify process identity");auto born=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
 PWSTR desktop=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&desktop)))throw std::runtime_error("Desktop unavailable");fs::path root(desktop);CoTaskMemFree(desktop);
 auto output=root/L"CS2 — записи дропа"/QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz").toStdWString();fs::create_directories(output);
 {std::lock_guard lock(mutex);folder=output;}
 std::ofstream log(output/L"events.jsonl",std::ios::binary);if(!log)throw std::runtime_error("Cannot create event log");
 auto writeEvent=[&](QJsonObject object){object["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);object["pid"]=int(pid);log<<QJsonDocument(object).toJson(QJsonDocument::Compact).constData()<<'\n';log.flush();if(!log)throw std::runtime_error("Event log write failed");};
 writeEvent({{"event","start"},{"schema",1},{"process_creation_filetime",QString::number(born)},{"note","Read only. No GSI interception; no commands, input or memory writes. Weekly fields are version-gated."}});
 std::ofstream readme(output/L"README.txt");readme<<"events.jsonl: UTC timestamps, weekly-state samples, version-gated flags, rollover, manual markers.\nPNG: visual evidence every 5 seconds, and on state changes/markers; may be unavailable for minimized windows.\nEarned is NOT proof that individual rewards were claimed. Compare profile fields against screenshots and manual marker.\nNo GSI packets were captured. No full memory dump, Steam tokens or passwords were collected. Screenshots may contain account/inventory details.\n";
 WeeklyDropReader reader;unsigned sample=0;WeeklyState previous=WeeklyState::Unknown;std::uint32_t lastFlags=0;bool hadFields=false;
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(maxSeconds);
 auto lastCapture=std::chrono::steady_clock::now()-std::chrono::seconds(5);bool capturePending=false;
 while(!stop&&std::chrono::steady_clock::now()<deadline){
  auto tick=std::chrono::steady_clock::now();if(WaitForSingleObject(process.h,0)!=WAIT_TIMEOUT){writeEvent({{"event","process_exited"}});break;}
  auto value=reader.sample(process.h,pid,born);bool marked=mark.exchange(false);
  bool changed=value.state!=previous||value.fieldsVerified!=hadFields||(value.fieldsVerified&&value.flags!=lastFlags);
  QJsonObject event{{"event","sample"},{"sample",int(sample)},{"state",value.state==WeeklyState::Unknown?"unknown":value.state==WeeklyState::Earned?"earned":"not_earned"},{"fields_verified",value.fieldsVerified},{"reason",value.reason}};
  if(value.fieldsVerified){event["flags"]=double(value.flags);event["rollover_unix"]=double(value.rollover);}
  writeEvent(event);if(marked)writeEvent({{"event","manual_marker"},{"meaning","User pressed reward-visible/claimed marker; verify screenshots, not an automatic claim detection"}});
  capturePending=capturePending||sample%5==0||changed||marked;
  if(capturePending&&tick-lastCapture>=std::chrono::seconds(3)){
   capturePending=false;lastCapture=tick;
   Window window{pid};EnumWindows(findWindow,reinterpret_cast<LPARAM>(&window));
   if(window.count!=1||IsIconic(window.hwnd))writeEvent({{"event","capture_unavailable"},{"reason","no unique visible non-minimized game window"}});
   else try{auto image=captureWgc(window.hwnd);auto name="frame-"+std::to_string(sample)+".png";png(image,output/name);writeEvent({{"event","frame"},{"file",QString::fromStdString(name)},{"width",image.width()},{"height",image.height()}});}
   catch(const winrt::hresult_error& ex){writeEvent({{"event","capture_error"},{"reason",QString::fromStdWString(ex.message().c_str())}});}
   catch(const std::exception& ex){writeEvent({{"event","capture_error"},{"reason",ex.what()}});}
  }
  previous=value.state;lastFlags=value.flags;hadFields=value.fieldsVerified;++sample;
  message(L"Запись: "+std::to_wstring(sample)+L" отсчётов. Дроп: "+value.text().toStdWString()+L"\n"+value.reason.toStdWString()+L"\n"+output.wstring());
  while(!stop&&std::chrono::steady_clock::now()<tick+std::chrono::seconds(1))std::this_thread::sleep_for(std::chrono::milliseconds(50));
 }
 writeEvent({{"event","stop"},{"reason",stop?"user":"process exit or 30-minute limit"}});message(L"Запись сохранена:\n"+output.wstring());
 }catch(const std::exception& e){message(L"Ошибка: "+QString::fromUtf8(e.what()).toStdWString());}
 catch(...){message(L"Ошибка записи. Уже записанные файлы сохранены.");}
 active=false;
}
void refresh(){if(active)return;pids.clear();SendMessageW(combo,CB_RESETCONTENT,0,0);Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};PROCESSENTRY32W e{};e.dwSize=sizeof(e);if(Process32FirstW(snapshot.h,&e))do{if(!_wcsicmp(e.szExeFile,L"cs2.exe")){pids.push_back(e.th32ProcessID);auto text=L"CS2 — PID "+std::to_wstring(e.th32ProcessID);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}}while(Process32NextW(snapshot.h,&e));if(!pids.empty())SendMessageW(combo,CB_SETCURSEL,0,0);}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM a,LPARAM b){
 if(msg==WM_CREATE){
 combo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,15,15,240,200,w,(HMENU)10,nullptr,nullptr);
 CreateWindowW(L"BUTTON",L"Обновить список",WS_CHILD|WS_VISIBLE,270,15,160,30,w,(HMENU)11,nullptr,nullptr);
 startButton=CreateWindowW(L"BUTTON",L"Начать запись",WS_CHILD|WS_VISIBLE,15,60,160,35,w,(HMENU)1,nullptr,nullptr);
 stopButton=CreateWindowW(L"BUTTON",L"Остановить",WS_CHILD|WS_VISIBLE,185,60,140,35,w,(HMENU)2,nullptr,nullptr);
 markButton=CreateWindowW(L"BUTTON",L"Метка: дроп",WS_CHILD|WS_VISIBLE,335,60,140,35,w,(HMENU)3,nullptr,nullptr);
 CreateWindowW(L"BUTTON",L"Открыть папку",WS_CHILD|WS_VISIBLE,485,60,150,35,w,(HMENU)4,nullptr,nullptr);
 label=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,15,115,620,140,w,nullptr,nullptr,nullptr);
 CreateWindowW(L"STATIC",L"Начни за 1–2 минуты до конца матча. Оставь CS2 не свёрнутой.\nПри появлении набора нажми «Метка: дроп». После ручного выбора\nнаграды нажми метку ещё раз, подожди 10 секунд и останови запись.\nЛимит: 30 минут. Игра не управляется; её закрытие не автоматизировано.",WS_CHILD|WS_VISIBLE,15,260,620,90,w,nullptr,nullptr,nullptr);
 refresh();SetTimer(w,1,200,nullptr);return 0;
 }
 if(msg==WM_COMMAND){switch(LOWORD(a)){
 case 1:if(!active){auto i=SendMessageW(combo,CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<pids.size()){if(worker.joinable())worker.join();stop=false;mark=false;active=true;worker=std::thread(record,pids[i],1800);}else message(L"Запусти CS2 вручную и обнови список.");}break;
 case 2:stop=true;message(L"Останавливаю запись…");break;
 case 3:if(active)mark=true;break;
 case 4:{fs::path saved;{std::lock_guard lock(mutex);saved=folder;}if(!saved.empty())ShellExecuteW(w,L"open",saved.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
 case 11:refresh();break;
 }return 0;}
 if(msg==WM_TIMER){std::lock_guard lock(mutex);SetWindowTextW(label,status.c_str());EnableWindow(startButton,!active);EnableWindow(stopButton,active);EnableWindow(markButton,active);EnableWindow(combo,!active);return 0;}
 if(msg==WM_CLOSE){stop=true;if(worker.joinable())worker.join();DestroyWindow(w);return 0;}
 if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,msg,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int){
 Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 2;
 if(std::wstring(args)==L"--self-test"){
  try {auto test=fs::temp_directory_path()/L"cs2-drop-recorder-codec-test.png";png(cs2n::Image(16,16,{20,120,220}),test);
   std::ifstream file(test,std::ios::binary);unsigned char signature[8]{};file.read(reinterpret_cast<char*>(signature),8);
   bool ok=signature[0]==137&&signature[1]=='P'&&signature[2]=='N'&&signature[3]=='G';
   Gdiplus::GdiplusShutdown(token);return ok?0:1;
  }catch(...){Gdiplus::GdiplusShutdown(token);return 1;}
 }
 unsigned pid=0,seconds=0;if(swscanf_s(args,L"--test %u %u",&pid,&seconds)==2){if(seconds<1||seconds>30)return 2;active=true;record(pid,int(seconds));Gdiplus::GdiplusShutdown(token);std::lock_guard lock(mutex);return status.starts_with(L"Ошибка")?1:0;}
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=proc;cls.lpszClassName=L"CS2DropDataRecorder";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
 auto window=CreateWindowW(cls.lpszClassName,L"CS2 — сбор данных о дропе",WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,675,410,nullptr,nullptr,instance,nullptr);
 if(!window){Gdiplus::GdiplusShutdown(token);return 2;}ShowWindow(window,SW_SHOW);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}stop=true;if(worker.joinable())worker.join();Gdiplus::GdiplusShutdown(token);return 0;
}


