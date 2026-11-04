#include "bridge.h"
#include <cmath>
#include <map>
#include "compatibility.h"
#include <QCoreApplication>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>
extern "C" int WINAPI legacyPanelEntry(HINSTANCE,HINSTANCE,PWSTR,int);
#define wWinMain legacyPanelEntry
#include "panel.cpp"
#undef wWinMain

static Slot& slotById(int id){for(auto& s:slots)if(s->id==id)return *s;throw std::runtime_error("Environment not found");}
PanelBridge::PanelBridge(QObject* parent,const QString& testRoot):QObject(parent){
 PWSTR profile=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&profile)))throw std::runtime_error("Cannot find user profile");
 root=fs::path(profile)/L".cs2panel-native";CoTaskMemFree(profile);
 testMode_=!testRoot.isEmpty();
 if(testMode_)root=testRoot.toStdWString();
 fs::create_directories(root);nextId=1;
 {QFile f(QString::fromStdWString((root/L"theme.txt").wstring()));if(f.open(QIODevice::ReadOnly)){const auto v=QString::fromUtf8(f.read(64)).trimmed();if(v=="black"||v=="kitty"||v=="terminal")theme_=v;}}
 panelTheme=theme_=="kitty"?2:theme_=="terminal"?3:theme_=="black"?1:0;
 std::ifstream in(root/L"profiles.txt");int id=0;while(in>>id){if(id<1||id>10000||std::any_of(slots.begin(),slots.end(),[id](auto& s){return s->id==id;}))throw std::runtime_error("Invalid environment list");slots.push_back(std::make_unique<Slot>(id));nextId=std::max(nextId,id+1);}
 if(slots.empty())for(int i=0;i<6;++i)add();
 for(auto& s:slots){std::ifstream file(root/(L"name-"+std::to_wstring(s->id)+L".txt"),std::ios::binary);wchar_t text[65]{};file.read(reinterpret_cast<char*>(text),64*sizeof(wchar_t));if(file.gcount())s->label.assign(text,size_t(file.gcount()/sizeof(wchar_t)));}
 connect(&timer_,&QTimer::timeout,this,&PanelBridge::poll);timer_.start(2000);poll();
 if(!testMode_)QTimer::singleShot(1500,this,[this]{checkCompatibility();});
}
PanelBridge::~PanelBridge(){detachBot();if(host_)DestroyWindow(static_cast<HWND>(host_));slots.clear();}
QVariantMap PanelBridge::selected()const{for(auto entry:data_)if(entry.toMap().value("id").toInt()==selected_)return entry.toMap();return {};}
void PanelBridge::poll(){
 try{if(!testMode_)refresh();QVariantList next;for(auto& s:slots){QVariantMap row;row["id"]=s->id;row["name"]=QString::fromStdWString(s->label);row["game"]=isGame(*s);row["steam"]=s->alive();row["bot"]=QString::fromStdWString(botStatus(*s));if(isGame(*s)){auto result=compatibility::inspect(QString::fromStdWString(sandbox::gameDir()),QCoreApplication::applicationDirPath()+"/bot/assets");row["bot"]=compatibility::botLabel(result,row["bot"].toString());}row["hasBot"]=bool(botWindow(*s));row["pid"]=s->alive()?int(s->pid):0;row["cpu"]=std::round(s->cpu*10)/10;row["ram"]=double(s->memory/1048576);row["box"]=s->boxed?QString::fromStdWString(sandbox::name(s->id)):QStringLiteral("Основной Steam");row["status"]=QString::fromStdWString(s->status);next.push_back(row);}if(next!=data_){data_=next;Q_EMIT changed();}if(selected_>0&&host_&&IsWindowVisible(static_cast<HWND>(host_)))attachBot(slotById(selected_));}catch(const std::exception& e){Q_EMIT notice(QString::fromUtf8(e.what()),true);}
}
void PanelBridge::select(int id){if(id!=-1&&std::none_of(slots.begin(),slots.end(),[id](const auto& s){return s->id==id;})){Q_EMIT notice(QStringLiteral("Окружение не найдено"),true);return;}detachBot();if(host_)ShowWindow(static_cast<HWND>(host_),SW_HIDE);selected_=id;Q_EMIT changed();}
void PanelBridge::rename(int id,const QString& text){auto trimmed=text.trimmed();if(trimmed.isEmpty()||trimmed.size()>64){Q_EMIT notice(QStringLiteral("Название должно содержать от 1 до 64 символов"),true);return;}try{auto& s=slotById(id);auto value=trimmed.toStdWString();QSaveFile file(QString::fromStdWString((root/(L"name-"+std::to_wstring(id)+L".txt")).wstring()));if(!file.open(QIODevice::WriteOnly)||file.write(reinterpret_cast<const char*>(value.data()),value.size()*sizeof(wchar_t))!=qint64(value.size()*sizeof(wchar_t))||!file.commit())throw std::runtime_error("Cannot save environment name");s.label=value;poll();Q_EMIT notice(QStringLiteral("Название сохранено"),false);}catch(const std::exception& e){Q_EMIT notice(QString::fromUtf8(e.what()),true);}}
void PanelBridge::addEnvironment(){try{if(nextId>10000)throw std::runtime_error("Environment limit reached");add();poll();Q_EMIT notice(QStringLiteral("Окружение добавлено. Вход в Steam выполняется отдельно."),false);}catch(const std::exception& e){Q_EMIT notice(QString::fromUtf8(e.what()),true);}}
void PanelBridge::action(int id,const QString& operation){
 if(busy_)return;busy_=true;Q_EMIT busyChanged();
 QTimer::singleShot(30,this,[this,id,operation]{try{auto& s=slotById(id);
 if(operation=="steam"){if(s.boxed)sandbox::launch(id);else spawn(sandbox::nativeSteam(),L"");}
 else if(operation=="game")launchGame(s);
 else if(operation=="switch")switchTo(s);
 else if(operation=="bot"){auto result=compatibility::inspect(QString::fromStdWString(sandbox::gameDir()),QCoreApplication::applicationDirPath()+"/bot/assets");if(!result.allowed)throw std::runtime_error(result.reason.toUtf8().constData());if(!result.commandMetadataMatches)Q_EMIT notice(result.reason,false);botCommand(s,false);}
 else if(operation=="stop")botCommand(s,true);
 else throw std::runtime_error("Unknown action");
 Q_EMIT notice(operation=="stop"?QStringLiteral("Сигнал остановки отправлен выбранному навигатору"):operation=="bot"?QStringLiteral("Запрос открытия навигатора отправлен; готовность ещё не подтверждена"):QStringLiteral("Команда выполнена для выбранного окружения"),false);
 }catch(const std::exception& e){Q_EMIT notice(QString::fromUtf8(e.what()),true);}busy_=false;Q_EMIT busyChanged();poll();});
}
void PanelBridge::navigatorRect(qreal x,qreal y,qreal width,qreal height,bool visible){
 if(!window_)return;if(!visible||selected_<0){detachBot();if(host_)ShowWindow(static_cast<HWND>(host_),SW_HIDE);return;}
 if(!host_){host_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,reinterpret_cast<HWND>(window_->winId()),nullptr,GetModuleHandleW(nullptr),nullptr);}
 botHost=static_cast<HWND>(host_);const auto scale=window_->devicePixelRatio();SetWindowPos(botHost,nullptr,int(x*scale),int(y*scale),int(width*scale),int(height*scale),SWP_NOZORDER|SWP_SHOWWINDOW);attachBot(slotById(selected_));if(embeddedBot)SetWindowPos(embeddedBot,nullptr,0,0,int(width*scale),int(height*scale),SWP_NOZORDER|SWP_SHOWWINDOW);
}
void PanelBridge::setActive(bool active){timer_.setInterval(active?2000:8000);if(!active&&host_)ShowWindow(static_cast<HWND>(host_),SW_HIDE);}
QString PanelBridge::diagnostics(){poll();return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(data_)).toJson());}


extern "C" int gsiEndpointSelfTest(){
 QTemporaryDir temporary;if(!temporary.isValid())return 61;
 const auto saved=root;struct Restore{fs::path saved;~Restore(){root=saved;}} restore{saved};
 root=temporary.path().toStdWString();
 try{
  bool missing=false;try{endpoint(4,false);}catch(...){missing=true;}if(!missing)return 62;
  auto a=endpoint(4),again=endpoint(4,false),b=endpoint(5);
  if(a.port!=32004||b.port!=32005||a.token.size()!=32||a.token!=again.token||a.token==b.token)return 63;
  bool invalid=false;try{endpoint(0);}catch(...){invalid=true;}if(!invalid)return 64;
  {std::ofstream out(root/L"gsi-4.txt");out<<"32005\ninvalid\n";}
  bool malformed=false;try{endpoint(4);}catch(...){malformed=true;}if(!malformed)return 65;
  return 0;
 }catch(...){return 66;}
}


extern "C" int panelRegressionSelfTest(){
 QTemporaryDir temporary;if(!temporary.isValid())return 71;
 try{
  PanelBridge bridge(nullptr,temporary.path());
  if(!bridge.diagnoseEnvironment(9999).contains(QStringLiteral("не завершена")))return 80;
  const auto ep=endpoint(2);
  const auto report=bridge.diagnoseEnvironment(2);
  if(!report.contains("32002")||report.contains(QString::fromStdString(ep.token)))return 81;
  bridge.select(2);bridge.select(9999);
  if(bridge.selectedId()!=2)return 72;
  const auto count=slots.size();const auto next=nextId;
  fs::create_directory(root/L"profiles.tmp");
  bool rejected=false;try{add();}catch(const std::exception&){rejected=true;}
  if(!rejected||slots.size()!=count||nextId!=next)return 73;
  struct Cleanup{HWND w=nullptr;~Cleanup(){detachBot();if(w)DestroyWindow(w);}} cleanup;
  cleanup.w=CreateWindowExW(0,L"STATIC",L"Regression placeholder",WS_POPUP,0,0,20,20,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!cleanup.w)return 74;
  Slot target(42);target.pid=GetCurrentProcessId();
  target.process=std::make_unique<Handle>(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target.pid));
  if(!target.process->h)return 75;
  target.born=creation(target.process->h);
  embeddedBot=cleanup.w;embeddedSlot=target.id;embeddedPid=target.pid;embeddedBorn=target.born;
  if(botWindow(target)!=cleanup.w)return 76;
  ++target.born;if(botWindow(target))return 77;
  --target.born;target.process.reset();if(botWindow(target))return 78;
  return 0;
 }catch(...){return 79;}
}

void PanelBridge::setTheme(const QString& value){
 if(value!=QStringLiteral("blue")&&value!=QStringLiteral("black")&&value!="kitty"&&value!="terminal")return;
 if(value==theme_)return;
 QSaveFile file(QString::fromStdWString((root/L"theme.txt").wstring()));
 const auto bytes=value.toUtf8();
 if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
  Q_EMIT notice(QStringLiteral("Не удалось сохранить тему"),true);return;
 }
 theme_=value;panelTheme=value=="kitty"?2:value=="terminal"?3:value=="black"?1:0;
 if(embeddedBot)PostMessageW(embeddedBot,WM_APP+211,panelTheme,0);
 Q_EMIT themeChanged();
}

void PanelBridge::checkCompatibility(int environmentId){
 if(busy_||testMode_)return;
 QStringList runtimeArguments;
 if(environmentId>0){
  try{
   refresh();auto& s=slotById(environmentId);
   if(!isGame(s))throw std::runtime_error("Сначала запустите CS2 в выбранном окружении и откройте локальный матч с ботами.");
   runtimeArguments={"--runtime-pid",QString::number(s.pid),"--runtime-created",QString::number(s.born)};
  }catch(const std::exception& e){Q_EMIT notice(QString::fromUtf8(e.what()),true);return;}
 }
 const auto home=QCoreApplication::applicationDirPath()+"/maintenance/compatibility";
 const auto python=QStandardPaths::findExecutable("python");
 if(python.isEmpty()||!QFile::exists(home+"/update.py")){
  Q_EMIT notice(QStringLiteral("Проверка профилей: требуется Python и комплект maintenance/compatibility"),true);return;
 }
 const auto output=QString::fromStdWString(root.wstring())+"/compatibility-checks/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 auto process=new QProcess(this);
 busy_=true;Q_EMIT busyChanged();
 auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PYTHONUTF8","1");process->setProcessEnvironment(environment);
 process->setProgram(python);
 QStringList arguments{home+"/update.py","prepare","--game",QString::fromStdWString(sandbox::gameDir()),
                       "--assets",home+"/approved","--output",output};
 arguments.append(runtimeArguments);process->setArguments(arguments);
 connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error){
  if(error!=QProcess::FailedToStart)return;
  busy_=false;Q_EMIT busyChanged();Q_EMIT notice(QStringLiteral("Не удалось запустить проверку профилей: ")+process->errorString(),true);
  process->deleteLater();
 });
 connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,output](int code,QProcess::ExitStatus status){
  busy_=false;Q_EMIT busyChanged();
  QFile file(output+"/report.json");
  QJsonObject report;if(file.open(QIODevice::ReadOnly))report=QJsonDocument::fromJson(file.readAll()).object();
  const bool known=status==QProcess::NormalExit&&code==0&&report["eligible"].toBool();
  QString message=known?QStringLiteral("Проверенные профили подготовлены. Рабочие файлы не изменены."):
    QStringLiteral("Автообновление заблокировано: нужны подтверждения для текущей версии.");
  const bool captured=report["runtime_collection"].isObject();
  if(captured)message=known?QStringLiteral("Данные матча собраны. Для этой версии уже есть проверенный профиль."):
    QStringLiteral("Данные матча собраны, новый профиль-кандидат сохранён. Перед установкой нужна проверка его работы.");
  if(process->property("timedout").toBool())message=QStringLiteral("Проверка профилей превысила 60 секунд и остановлена.");
  else if(report.isEmpty())message=QStringLiteral("Проверка не завершилась: ")+QString::fromUtf8(process->readAllStandardOutput()+process->readAllStandardError()).left(500);
  Q_EMIT notice(message+QStringLiteral("\nОтчёт: ")+output,!(known||captured));
  process->deleteLater();
 });
 auto timeout=new QTimer(process);timeout->setSingleShot(true);
 connect(timeout,&QTimer::timeout,process,[process]{process->setProperty("timedout",true);process->kill();});
 timeout->start(60000);
 process->start();
}

QString PanelBridge::diagnoseEnvironment(int id){
 QStringList lines;
 try{
  auto& s=slotById(id);
  lines << QStringLiteral("Окружение: %1").arg(QString::fromStdWString(s.label));
  const auto steamPath=s.boxed?sandbox::steam(id):sandbox::nativeSteam();
  lines << (fs::exists(steamPath)?QStringLiteral("Steam: файл запуска найден."):QStringLiteral("Steam: файл не найден. Проверьте путь Steam в panel-settings.ini."));
  try{
   bool running=false;Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
   if(snapshot.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot enumerate processes");
   PROCESSENTRY32W e{};e.dwSize=sizeof(e);
   if(Process32FirstW(snapshot.h,&e))do{if(_wcsicmp(e.szExeFile,L"steam.exe")==0){
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID));
    if(h.h && _wcsicmp(imagePath(h.h).c_str(),steamPath.c_str())==0 && (!s.boxed||sandbox::boxOf(e.th32ProcessID)==sandbox::name(id)))running=true;
   }}while(Process32NextW(snapshot.h,&e));
   if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error("Incomplete process enumeration");
   lines << (running?QStringLiteral("Steam: процесс выбранного окружения найден."):QStringLiteral("Steam: подходящий процесс не найден. Нажмите «Открыть Steam»."));
  }catch(...){lines << QStringLiteral("Steam: состояние процесса неизвестно — не удалось проверить доступ/изоляцию.");}
  if(s.boxed){
   try{auto ids=sandbox::pids(id);lines << QStringLiteral("Sandboxie: API доступен, процессов окружения: %1. Это не подтверждает вход в Steam.").arg(ids.size());}
   catch(...){lines << QStringLiteral("Sandboxie: API недоступен. Проверьте установку Sandboxie-Plus и путь в panel-settings.ini.");}
  }else lines << QStringLiteral("Sandboxie: основному окружению не требуется.");
  lines << (isGame(s)?QStringLiteral("CS2: процесс привязан, PID %1.").arg(s.pid):QStringLiteral("CS2: живой клиент не привязан. Запустите CS2 в выбранном окружении."));
  try{auto e=endpoint(id,false);lines << QStringLiteral("GSI: конфигурация endpoint корректна, порт %1. Доставка пакетов этой проверкой не подтверждается; смотрите журнал навигатора. При отсутствии данных подключите навигатор и перезапустите CS2.").arg(e.port);}
  catch(...){lines << QStringLiteral("GSI: endpoint отсутствует/повреждён. Подключите навигатор через панель; при повторной ошибке проверьте файл gsi-%1.txt. Токен не публикуйте.").arg(id);}
  const auto result=compatibility::inspect(QString::fromStdWString(sandbox::gameDir()),QCoreApplication::applicationDirPath()+"/bot/assets");
  lines << "Профили: "+result.reason;
  if(!result.allowed||!result.commandMetadataMatches)lines << QStringLiteral("Действие: выполните «Проверить профили»; не меняйте номер версии вручную.");
 }catch(const std::exception&){lines << QStringLiteral("Проверка не завершена: окружение или его настройки недоступны.");}
 return lines.join("\n\n");
}
