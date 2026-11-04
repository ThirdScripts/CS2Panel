#include <windows.h>
#include <commctrl.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <span>
#include "sandbox.h"
namespace fs=std::filesystem;
struct Handle {
 HANDLE h=nullptr;
 explicit Handle(HANDLE v=nullptr):h(v){}
 ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
std::wstring executable(){wchar_t b[32768];auto n=GetModuleFileNameW(nullptr,b,32768);if(!n||n==32768)throw std::runtime_error("Executable path unavailable");return {b,n};}
uint64_t ticks(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
uint64_t creation(HANDLE h){FILETIME a,b,c,d;if(!GetProcessTimes(h,&a,&b,&c,&d))throw std::runtime_error("Process identity unavailable");return ticks(a);}
std::wstring imagePath(HANDLE h){wchar_t b[32768];DWORD n=32768;if(!QueryFullProcessImageNameW(h,0,b,&n))throw std::runtime_error("Process path unavailable");return {b,n};}
struct Slot {
 std::wstring label; int id; std::unique_ptr<Handle> process,job; DWORD pid=0; uint64_t born=0,lastCpu=0,lastTime=0;
 bool owned=false,boxed=false; std::wstring path,status=L"Готов к запуску Steam в sandbox"; double cpu=0; SIZE_T memory=0;
 explicit Slot(int i):label(L"CS2 №"+std::to_wstring(i)),id(i),boxed(i>1){}
 bool alive()const{return process&&WaitForSingleObject(process->h,0)==WAIT_TIMEOUT;}
 bool matches(DWORD p,uint64_t t)const{return alive()&&p==pid&&t==born;}
 void sample(){
  if(!process)return;
  if(!alive()){status=L"Завершён";cpu=0;memory=0;return;}
  FILETIME a,b,k,u; if(GetProcessTimes(process->h,&a,&b,&k,&u)){
   auto total=ticks(k)+ticks(u),now=GetTickCount64();SYSTEM_INFO si{};GetSystemInfo(&si);
   if(lastTime&&now>lastTime)cpu=100.0*double(total-lastCpu)/(double(now-lastTime)*10000.0*si.dwNumberOfProcessors);
   lastCpu=total;lastTime=now;
  }
  PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof(pm);
  if(GetProcessMemoryInfo(process->h,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm)))memory=pm.WorkingSetSize;
 }
 void startTest(bool hidden=false){
  if(alive())throw std::runtime_error("Slot already has a running process");
  auto j=std::make_unique<Handle>(CreateJobObjectW(nullptr,nullptr));if(!j->h)throw std::runtime_error("CreateJobObject failed");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(j->h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Job configuration failed");
  auto exe=executable();std::wstring cmd=L"\""+exe+L"\" --worker "+std::to_wstring(id)+(hidden?L" --hidden":L"");
  STARTUPINFOW s{};s.cb=sizeof(s);PROCESS_INFORMATION p{};
  if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&s,&p))throw std::runtime_error("Test launch failed");
  auto proc=std::make_unique<Handle>(p.hProcess);Handle thread(p.hThread);
  if(!AssignProcessToJobObject(j->h,p.hProcess)){TerminateProcess(p.hProcess,1);throw std::runtime_error("Job assignment failed");}
  auto birth=creation(p.hProcess);
  if(ResumeThread(p.hThread)==DWORD(-1)){TerminateJobObject(j->h,1);throw std::runtime_error("Resume failed");}
  job=std::move(j);process=std::move(proc);pid=p.dwProcessId;born=birth;path=exe;owned=true;lastCpu=lastTime=0;
  boxed=false;status=L"Тест панели — БЕЗ изоляции";
 }
 void attach(DWORD target){
  if(alive())throw std::runtime_error("Stop or detach the current process first");
  auto h=std::make_unique<Handle>(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,target));
  if(!h->h)throw std::runtime_error("Cannot open this PID for monitoring");
  auto name=imagePath(h->h);if(_wcsicmp(fs::path(name).filename().c_str(),L"cs2.exe"))throw std::runtime_error("Only cs2.exe can be attached");
  auto birth=creation(h->h);if(WaitForSingleObject(h->h,0)!=WAIT_TIMEOUT)throw std::runtime_error("Process already exited");
  job.reset();process=std::move(h);pid=target;born=birth;path=name;owned=false;boxed=false;lastCpu=lastTime=0;status=L"CS2: только наблюдение; вне изоляции";
 }
 void stop(){if(!owned)throw std::runtime_error("External CS2 is read-only: use Detach");if(job&&alive()&&!TerminateJobObject(job->h,0))throw std::runtime_error("Stop failed");}
 void detach(){if(owned&&alive())throw std::runtime_error("Stop owned test before detaching");process.reset();job.reset();pid=0;born=0;memory=0;cpu=0;boxed=false;status=L"Готов к запуску Steam в sandbox";}
};
fs::path root;std::vector<std::unique_ptr<Slot>> slots;HWND mainWindow,table,pidBox,footer;HFONT font;int nextId=1,chosenSlot=-1,page=0; HWND nameEdit; std::vector<HWND> pageControls; void renderPage();
int panelTheme=0;
HWND embeddedBot=nullptr,botHost=nullptr; int embeddedSlot=-1; DWORD embeddedPid=0; uint64_t embeddedBorn=0; LONG_PTR embeddedStyle=0; RECT embeddedRect{};
void detachBot(){if(embeddedBot&&IsWindow(embeddedBot)){ShowWindow(embeddedBot,SW_HIDE);SetParent(embeddedBot,nullptr);SetWindowLongPtrW(embeddedBot,GWL_STYLE,embeddedStyle&~WS_VISIBLE);SetWindowPos(embeddedBot,nullptr,embeddedRect.left,embeddedRect.top,embeddedRect.right-embeddedRect.left,embeddedRect.bottom-embeddedRect.top,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);ShowWindow(embeddedBot,SW_HIDE);}embeddedBot=nullptr;embeddedSlot=-1;embeddedPid=0;embeddedBorn=0;}
HWND botWindow(const Slot& s){if(!s.matches(s.pid,s.born))return nullptr;if(embeddedSlot==s.id&&embeddedPid==s.pid&&embeddedBorn==s.born&&IsWindow(embeddedBot))return embeddedBot;auto title=L"CS2 Navigator — PID "+std::to_wstring(s.pid);if(!s.alive())return nullptr;auto w=FindWindowW(nullptr,title.c_str());if(!w){title=L"[#] "+title+L" [#]";w=FindWindowW(nullptr,title.c_str());}return w;}
std::wstring botStatus(const Slot& s){auto w=botWindow(s);if(!w)return L"окно не найдено";auto state=GetPropW(w,L"CS2NavigatorRunning");return state==HANDLE(2)?L"работает":state==HANDLE(1)?L"остановлен":L"окно открыто";}
void attachBot(Slot& s){
 if(!botHost||!IsWindow(botHost))return;
 auto w=botWindow(s);if(!w){if(embeddedSlot==s.id)detachBot();return;}
 if(embeddedBot==w)return;
 detachBot();ShowWindow(w,SW_HIDE);LONG_PTR style=GetWindowLongPtrW(w,GWL_STYLE);RECT rect{};GetWindowRect(w,&rect);
 SetLastError(0);auto oldParent=SetParent(w,botHost);if(!oldParent&&GetLastError()){SetWindowTextW(footer,L"Windows не разрешила встроить окно. Окно навигатора оставлено скрытым.");return;}
 SetWindowTextW(botHost,L"");embeddedBot=w;embeddedSlot=s.id;embeddedPid=s.pid;embeddedBorn=s.born;embeddedStyle=style;embeddedRect=rect;PostMessageW(w,WM_APP+211,panelTheme,0);
 SetWindowLongPtrW(w,GWL_STYLE,(style&~(WS_MINIMIZE|WS_MAXIMIZE|WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU))|WS_CHILD);
 RECT r{};GetClientRect(botHost,&r);SetWindowPos(w,nullptr,0,0,r.right,r.bottom,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
}
bool isGame(const Slot& s){return s.alive()&&_wcsicmp(fs::path(s.path).filename().c_str(),L"cs2.exe")==0;}
struct GsiEndpoint{unsigned port;std::string token;};
GsiEndpoint endpoint(int id,bool create=true){
 if(id<1||id>10000)throw std::runtime_error("No port available for this profile");
 auto path=root/(L"gsi-"+std::to_wstring(id)+L".txt");GsiEndpoint e{static_cast<unsigned>(32000+id),{}};
 std::ifstream in(path);if(in){in>>e.port>>e.token;if(e.port!=32000+id||e.token.size()!=32||e.token.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid GSI endpoint file");return e;}
 if(!create)throw std::runtime_error("Prepare GSI endpoint in panel first");
 GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create endpoint token");const char* hex="0123456789abcdef";for(auto b:std::span(reinterpret_cast<unsigned char*>(&guid),sizeof(guid))){e.token+=hex[b>>4];e.token+=hex[b&15];}
 std::ofstream out(path);out<<e.port<<'\n'<<e.token<<'\n';if(!out)throw std::runtime_error("Cannot save endpoint");return e;
}
void writeGsi(int id){
 auto e=endpoint(id,false);fs::path cfg=fs::path(sandbox::gameDir())/L"game/csgo/cfg/gamestate_integration_radar_navigator.cfg";
 if(fs::exists(cfg)&&!fs::exists(cfg.wstring()+L".panel-backup"))fs::copy_file(cfg,cfg.wstring()+L".panel-backup");
 std::ofstream out(cfg);out<<"\"CS2 Panel\"\n{\n\"uri\" \"http://127.0.0.1:"<<e.port<<"\"\n\"timeout\" \"5\"\n\"buffer\" \"0.1\"\n\"throttle\" \"0.1\"\n\"heartbeat\" \"1\"\n\"auth\" { \"token\" \""<<e.token<<"\" }\n\"data\" { \"provider\" \"1\" \"map\" \"1\" \"round\" \"1\" \"player_id\" \"1\" \"player_state\" \"1\" \"player_weapons\" \"1\" }\n}\n";out.close();if(!out)throw std::runtime_error("Cannot write isolated GSI configuration");
}
void spawn(const std::wstring& exe,const std::wstring& arguments,bool wait=false){std::wstring cmd=L"\""+exe+L"\" "+arguments;STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,fs::path(exe).parent_path().c_str(),&si,&pi))throw std::runtime_error("Launch failed");Handle process(pi.hProcess),thread(pi.hThread);if(wait){if(WaitForSingleObject(process.h,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Configuration timed out");DWORD code=1;GetExitCodeProcess(process.h,&code);if(code)throw std::runtime_error("Configuration failed");}}
void launchGame(Slot& s){
 if(s.alive()&&_wcsicmp(fs::path(s.path).filename().c_str(),L"cs2.exe")==0)throw std::runtime_error("This client is already running");
 endpoint(s.id);
 if(s.id==1&&!s.boxed){writeGsi(s.id);spawn(sandbox::nativeSteam(),L"-applaunch 730 -windowed -w 800 -h 600 +fps_max 30");s.status=L"Обычная CS2 запускается";}
 else{sandbox::prepare(s.id);sandbox::invoke(L"Start.exe",L"/box:"+sandbox::name(s.id)+L" /wait \""+executable()+L"\" --configure-gsi "+std::to_wstring(s.id));sandbox::invoke(L"Start.exe",L"/box:"+sandbox::name(s.id)+L" \""+sandbox::steam(s.id)+L"\" -applaunch 730 -windowed -w 800 -h 600 +fps_max 30",false);s.boxed=true;s.owned=false;}
}
void prepareNavigatorGsi(Slot& s){
 // Steam-launched games may never have passed through launchGame().
 endpoint(s.id);
 if(s.boxed)sandbox::invoke(L"Start.exe",L"/box:"+sandbox::name(s.id)+L" /wait \""+executable()+L"\" --configure-gsi "+std::to_wstring(s.id));
 else writeGsi(s.id);
}
void botCommand(Slot& s,bool stop){
 if(!s.matches(s.pid,s.born)||_wcsicmp(fs::path(s.path).filename().c_str(),L"cs2.exe"))throw std::runtime_error("Select a live CS2 process first");
 if(s.boxed){auto members=sandbox::pids(s.id);if(std::find(members.begin(),members.end(),s.pid)==members.end())throw std::runtime_error("PID is not in this sandbox");}
 if(!stop)if(auto w=botWindow(s)){attachBot(s);return;}
 auto bot=fs::path(executable());
 GsiEndpoint e{};if(!stop){prepareNavigatorGsi(s);e=endpoint(s.id,false);}std::wstring token(e.token.begin(),e.token.end());
 auto args=L"--navigator --pid "+std::to_wstring(s.pid)+L" --created "+std::to_wstring(s.born)+(stop?L" --stop-bound":L" --panel --gsi-port "+std::to_wstring(e.port)+L" --gsi-token "+token);
 if(s.boxed)sandbox::invoke(L"Start.exe",L"/box:"+sandbox::name(s.id)+L" \""+bot.wstring()+L"\" "+args,false);else spawn(bot.wstring(),args);
 SetWindowTextW(footer,stop?L"Отправлен сигнал остановки только выбранному боту.":L"Открыт бот выбранного PID. Для движения нажмите Старт в его окне.");
}
void save(){fs::create_directories(root);std::ofstream out(root/L"profiles.tmp");for(auto& s:slots)out<<s->id<<'\n';out.close();if(!out||!MoveFileExW((root/L"profiles.tmp").c_str(),(root/L"profiles.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save profiles");}
void add(){slots.push_back(std::make_unique<Slot>(nextId));try{save();}catch(...){slots.pop_back();throw;}++nextId;}
int selected(){return chosenSlot;}
void refresh(){
 static uint64_t lastBoxPoll=0;bool poll=GetTickCount64()-lastBoxPoll>5000;if(poll)lastBoxPoll=GetTickCount64();
 std::vector<DWORD> boxedPids;bool membershipOk=true;
 if(poll)for(auto& entry:slots){auto& s=*entry;if(!s.boxed)continue;try{
  auto members=sandbox::pids(s.id);boxedPids.insert(boxedPids.end(),members.begin(),members.end());DWORD chosen=0;int best=0;
  for(auto pid:members){Handle h(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid));if(!h.h)continue;auto path=imagePath(h.h);auto leaf=fs::path(path).filename().wstring();int rank=_wcsicmp(leaf.c_str(),L"cs2.exe")==0?3:(_wcsicmp(leaf.c_str(),L"steamwebhelper.exe")==0?2:(_wcsicmp(leaf.c_str(),L"steam.exe")==0?1:0));if(rank>best){best=rank;chosen=pid;}}
  if(!chosen){s.status=L"Sandbox: клиент не найден";s.process.reset();s.pid=0;s.born=0;s.memory=0;s.cpu=0;continue;}
  Handle check(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,chosen));if(!check.h)continue;auto born=creation(check.h);
  if(!s.matches(chosen,born)){s.process=std::make_unique<Handle>(check.h);check.h=nullptr;s.pid=chosen;s.born=born;s.path=imagePath(s.process->h);s.lastTime=s.lastCpu=0;}
  s.status=best==3?L"Sandbox: CS2 (сервер не проверен)":L"Sandbox: Steam (вход вручную)";
 }catch(const std::exception&){membershipOk=false;s.status=L"Sandbox: ошибка получения процессов";}}
 if(poll&&membershipOk)for(auto& entry:slots){auto& s=*entry;if(s.id!=1||s.boxed||s.alive())continue;Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W p{};p.dwSize=sizeof(p);std::vector<DWORD> candidates;
  if(snapshot.h!=INVALID_HANDLE_VALUE&&Process32FirstW(snapshot.h,&p))do{if(_wcsicmp(p.szExeFile,L"cs2.exe")==0&&std::find(boxedPids.begin(),boxedPids.end(),p.th32ProcessID)==boxedPids.end())candidates.push_back(p.th32ProcessID);}while(Process32NextW(snapshot.h,&p));
  if(candidates.size()==1){try{s.attach(candidates.front());}catch(...){s.status=L"Нет доступа к обычной CS2";}}
 }
 for(auto& s:slots){s->sample();auto w=botWindow(*s);if(w&&w!=embeddedBot&&IsWindowVisible(w))ShowWindow(w,SW_HIDE); }
 if(chosenSlot<0){for(int n=0;n<6;++n){int i=page*6+n;if(i>=int(slots.size()))break;auto& s=*slots[i];auto text=s.label+L"\r\nCS2: "+(isGame(s)?L"запущена":L"не запущена")+L"\r\nБот: "+botStatus(s);SetDlgItemTextW(mainWindow,1000+n,text.c_str());}}
 else if(chosenSlot<int(slots.size())){auto& s=*slots[chosenSlot];auto text=L"CS2: "+std::wstring(isGame(s)?L"запущена":L"не запущена")+L"    Бот: "+botStatus(s);SetDlgItemTextW(mainWindow,31,text.c_str());attachBot(s);}

}
struct WindowSearch{DWORD pid;HWND hwnd=nullptr;};
BOOL CALLBACK findWindow(HWND w,LPARAM v){auto& q=*reinterpret_cast<WindowSearch*>(v);DWORD p=0;GetWindowThreadProcessId(w,&p);if(p==q.pid&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){q.hwnd=w;return FALSE;}return TRUE;}
void switchTo(Slot& s){if(!s.matches(s.pid,s.born))throw std::runtime_error("Process exited; stale binding rejected");WindowSearch q{s.pid};EnumWindows(findWindow,reinterpret_cast<LPARAM>(&q));if(s.boxed&&!q.hwnd)for(auto p:sandbox::pids(s.id)){q.pid=p;EnumWindows(findWindow,reinterpret_cast<LPARAM>(&q));if(q.hwnd)break;}if(!q.hwnd)throw std::runtime_error("No visible window for this environment");ShowWindow(q.hwnd,SW_RESTORE);if(!SetForegroundWindow(q.hwnd)){FLASHWINFO f{sizeof(f),q.hwnd,FLASHW_TRAY,3,0};FlashWindowEx(&f);}}
void exportBinding(Slot& s){if(!s.matches(s.pid,s.born))throw std::runtime_error("Cannot bind an exited process");std::ofstream out(root/(L"binding-"+std::to_wstring(s.id)+L".txt"));out<<"schema=1\nslot="<<s.id<<"\npid="<<s.pid<<"\ncreation_filetime="<<s.born<<"\nbackend="<<(s.owned?"test":"external-observe")<<"\nbinding_is_diagnostic=true\n";if(!out)throw std::runtime_error("Cannot save binding");SetWindowTextW(footer,L"Диагностическая привязка сохранена. Запуск бота — кнопкой «Открыть бот».");}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
void clearPage(){detachBot();botHost=nullptr;for(auto w:pageControls)DestroyWindow(w);pageControls.clear();}
HWND item(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id){auto w=control(cls,text,style,x,y,width,height,id);pageControls.push_back(w);return w;}
void renderPage(){
 clearPage();SetWindowPos(mainWindow,nullptr,0,0,chosenSlot<0?795:1180,chosenSlot<0?595:965,SWP_NOMOVE|SWP_NOZORDER);
 if(chosenSlot<0){
  item(L"STATIC",L"Ваши окружения",0,20,20,500,30,0);
  item(L"BUTTON",L"+ Добавить окружение",0,540,16,215,36,1);
  for(int n=0;n<6;++n){int i=page*6+n;if(i>=int(slots.size()))break;item(L"BUTTON",slots[i]->label.c_str(),BS_MULTILINE|WS_TABSTOP,20+(n%2)*375,72+(n/2)*115,360,100,1000+n);}
  item(L"BUTTON",L"Назад",0,20,425,100,32,21);item(L"BUTTON",L"Далее",0,135,425,100,32,22);
  EnableWindow(GetDlgItem(mainWindow,21),page>0);EnableWindow(GetDlgItem(mainWindow,22),(page+1)*6<int(slots.size()));
 }else{
  auto& s=*slots[chosenSlot];
  item(L"BUTTON",L"← Окружения",0,20,16,160,36,20);
  nameEdit=item(L"EDIT",s.label.c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,20,74,470,32,30);SendMessageW(nameEdit,EM_SETLIMITTEXT,64,0);
  item(L"BUTTON",L"Сохранить имя",0,510,72,180,36,23);
  item(L"STATIC",L"",0,20,123,730,45,31);
  item(L"BUTTON",L"Открыть Steam",0,20,185,220,42,9);
  item(L"BUTTON",L"Запустить CS2",0,265,185,220,42,11);
  item(L"BUTTON",L"Показать CS2",0,510,185,220,42,3);
  item(L"BUTTON",L"Подключить навигатор",0,20,250,220,42,12);
  item(L"BUTTON",L"Остановить бота",0,265,250,220,42,13);
  item(L"BUTTON",L"Информация",0,510,250,220,42,24);
  item(L"STATIC",L"Навигатор этого окружения • F10 — аварийная остановка",0,20,310,1100,28,0);
  botHost=item(L"STATIC",L"Нажмите «Подключить навигатор», если он ещё не запущен.",WS_BORDER|WS_CLIPCHILDREN,20,345,1120,520,32);
 }
 footer=item(L"STATIC",L"Выберите окружение для управления.",0,20,chosenSlot<0?480:880,1100,40,0);refresh();
}
LRESULT CALLBACK windowProc(HWND w,UINT m,WPARAM a,LPARAM b){
 if(m==WM_CREATE){mainWindow=w;font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");renderPage();SetTimer(w,1,1000,nullptr);return 0;}
 if(m==WM_COMMAND){try{int id=LOWORD(a);if(HIWORD(a)!=BN_CLICKED)return 0;
  if(id>=1000&&id<1006){chosenSlot=page*6+id-1000;renderPage();return 0;}
  if(id==20){chosenSlot=-1;renderPage();return 0;}
  if(id==21||id==22){page+=id==21?-1:1;renderPage();return 0;}
  if(id==1){if(nextId>10000)throw std::runtime_error("Profile limit reached");add();chosenSlot=int(slots.size())-1;page=chosenSlot/6;renderPage();return 0;}
  int n=selected();if(n<0||n>=int(slots.size()))return 0;auto& s=*slots[n];
  if(id==23){wchar_t name[65]{};GetWindowTextW(nameEdit,name,65);std::wstring value=name;auto first=value.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)throw std::runtime_error("Enter a name");value=value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);std::ofstream out(root/(L"name-"+std::to_wstring(s.id)+L".txt"),std::ios::binary);out.write(reinterpret_cast<const char*>(value.data()),value.size()*sizeof(wchar_t));out.close();if(!out)throw std::runtime_error("Cannot save name");s.label=value;SetWindowTextW(footer,L"Имя сохранено.");}
  if(id==24){wchar_t cpu[40];swprintf_s(cpu,L"%.1f %%",s.cpu);auto info=L"Окружение: "+s.label+L"\nИзоляция: "+(s.boxed?sandbox::name(s.id):L"основной Steam")+L"\nPID: "+std::to_wstring(s.pid)+L"\nRAM (working set): "+std::to_wstring(s.memory/1048576)+L" MiB\nCPU всего ПК: "+cpu+L"\nGPU: нет данных\nБот: "+botStatus(s)+L"\n\n"+s.status;MessageBoxW(w,info.c_str(),L"Информация",MB_OK);}
  if(id==11)launchGame(s);if(id==12)botCommand(s,false);if(id==13)botCommand(s,true);if(id==3)switchTo(s);
  if(id==9){if(s.id==1&&!s.boxed)spawn(sandbox::nativeSteam(),L"");else{sandbox::launch(s.id);s.boxed=true;}}
  refresh();
 }catch(const std::exception& e){MessageBoxA(w,e.what(),"CS2 Panel",MB_ICONERROR);}return 0;}
 if(m==WM_TIMER){refresh();return 0;}if(m==WM_CLOSE){detachBot();DestroyWindow(w);return 0;}if(m==WM_DESTROY){detachBot();KillTimer(w,1);PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,a,b);
}
LRESULT CALLBACK workerProc(HWND w,UINT m,WPARAM a,LPARAM b){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,a,b);}
int worker(HINSTANCE instance,const wchar_t* args){WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=workerProc;c.lpszClassName=L"CS2PanelTestWorker";c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);auto title=std::wstring(L"CS2 Panel — тест процесса ")+args;auto w=CreateWindowW(c.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,620,180,nullptr,nullptr,instance,nullptr);if(!w)return 1;CreateWindowW(L"STATIC",L"Тест управления процессом. Это не CS2 и не изолированное окружение.",WS_CHILD|WS_VISIBLE,20,30,560,60,w,nullptr,instance,nullptr);if(!wcsstr(args,L"--hidden"))ShowWindow(w,SW_SHOW);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;}
int selfTest(){try{Slot a(1),b(2);a.startTest(true);b.startTest(true);if(a.pid==b.pid||!a.alive()||!b.alive())return 11;if(a.matches(a.pid,a.born+1)||a.matches(b.pid,b.born))return 12;bool rejected=false;try{Slot observer(3);observer.attach(a.pid);}catch(...){rejected=true;}if(!rejected)return 13;a.sample();a.stop();if(WaitForSingleObject(a.process->h,5000)!=WAIT_OBJECT_0||!b.alive()||a.matches(a.pid,a.born))return 14;b.stop();if(WaitForSingleObject(b.process->h,5000)!=WAIT_OBJECT_0)return 15;HANDLE duplicate=nullptr;{Slot c(3);c.startTest(true);if(!DuplicateHandle(GetCurrentProcess(),c.process->h,GetCurrentProcess(),&duplicate,SYNCHRONIZE,FALSE,0))return 16;}Handle held(duplicate);if(WaitForSingleObject(duplicate,5000)!=WAIT_OBJECT_0)return 17;return 0;}catch(...){return 20;}}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int){
 if(wcsstr(args,L"--worker"))return worker(instance,args);if(wcsstr(args,L"--self-test"))return selfTest();
 try{SetProcessDPIAware();PWSTR profile=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&profile)))return 1;root=fs::path(profile)/L".cs2panel-native";CoTaskMemFree(profile);fs::create_directories(root);
  if(std::wstring(args).starts_with(L"--configure-gsi ")){try{int id=std::stoi(std::wstring(args).substr(16));writeGsi(id);return 0;}catch(...){return 3;}}
  if(std::wstring(args).starts_with(L"--open-steam ")){int id=std::stoi(std::wstring(args).substr(13));if(id<2||id>10000)return 4;sandbox::launch(id);return 0;}
  std::ifstream in(root/L"profiles.txt");int id;while(in>>id){if(id<=0||id>1000000||std::any_of(slots.begin(),slots.end(),[&](auto& s){return s->id==id;}))throw std::runtime_error("Invalid profile list");slots.push_back(std::make_unique<Slot>(id));nextId=std::max(nextId,id+1);}if(slots.empty())for(int i=0;i<6;++i)add();
  if(std::wstring(args)==L"--diagnose"){std::ofstream report(root/L"diagnostics.txt");for(auto& s:slots){report<<"slot="<<s->id<<" isolated="<<s->boxed;if(s->boxed){auto box=sandbox::name(s->id);report<<" box="<<std::string(box.begin(),box.end());for(auto pid:sandbox::pids(s->id))report<<" pid="<<pid;}report<<'\n';}return report?0:5;}
  for(auto& s:slots){std::ifstream name(root/(L"name-"+std::to_wstring(s->id)+L".txt"),std::ios::binary);wchar_t data[65]{};if(name){name.read(reinterpret_cast<char*>(data),64*sizeof(wchar_t));auto count=name.gcount()/sizeof(wchar_t);if(count>0)s->label.assign(data,size_t(count));}}
  INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=windowProc;c.lpszClassName=L"CS2PanelNative";c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&c);
  auto w=CreateWindowW(c.lpszClassName,L"CS2 Panel — C++ / привязка клиентов",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,795,595,nullptr,nullptr,instance,nullptr);if(!w)return 2;ShowWindow(w,SW_SHOW);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}slots.clear();DeleteObject(font);return 0;
 }catch(const std::exception& e){MessageBoxA(nullptr,e.what(),"CS2 Panel",MB_ICONERROR);return 1;}
}
