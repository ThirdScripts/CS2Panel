#include <windows.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QFile>
#include <QTimer>
#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include "bridge.h"
#include "ui/main_window.h"
#include <shellapi.h>
extern "C" int gsiEndpointSelfTest();
extern "C" int panelRegressionSelfTest();
int WINAPI cs2NavigatorMain(HINSTANCE,HINSTANCE,PWSTR,int);
#include "compatibility.h"
extern "C" int WINAPI legacyPanelEntry(HINSTANCE,HINSTANCE,PWSTR,int);
int main(int argc,char** argv){
 // Dispatch before Qt creates an application: workers use the existing native
 // event loop and the very same EXE, retaining per-client process isolation.
 int wideCount=0;auto wide=CommandLineToArgvW(GetCommandLineW(),&wideCount);
 if(wide && wideCount>1 && wcscmp(wide[1],L"--navigator")==0){
  std::wstring line;for(int i=2;i<wideCount;++i){if(i>2)line+=L' ';line+=wide[i];}LocalFree(wide);
  return cs2NavigatorMain(GetModuleHandleW(nullptr),nullptr,line.data(),SW_HIDE);
 }
 if(wide)LocalFree(wide);
 if(argc==2 && std::string(argv[1])=="--navigator-ui-test"){
  for(auto size:{std::pair{740,430},std::pair{1000,650},std::pair{1400,900}}){
   cs2n::MainWindow window;if(!window.create(size.first,size.second,true,1))return 51;
   for(int theme:{0,1,2,3,0}){window.setVisualTheme(theme);if(!window.layoutFits())return 52;}
  }
  cs2n::MainWindow standalone;if(!standalone.create(1000,650,true,0)||!standalone.layoutFits())return 53;
  return 0;
 }

 qputenv("QT_QUICK_CONTROLS_STYLE","Basic");
 QGuiApplication app(argc,argv);app.setApplicationName("CS2 Panel");app.setOrganizationName("CS2Panel");
 qInstallMessageHandler([](QtMsgType type,const QMessageLogContext&,const QString& message){QFile log(QCoreApplication::applicationDirPath()+"/interface.log");if(log.open(QIODevice::Append|QIODevice::Text)){log.write(message.toUtf8());log.write("\n");}if(type==QtFatalMsg)abort();});
 auto args=app.arguments();
 if(args.contains("--compat-test"))return compatibility::selfTest();
 if(args.contains("--compat-diagnostic")){if(args.size()!=4)return 40;auto r=compatibility::inspect(args[2],args[3]);QFile out;out.open(stdout,QIODevice::WriteOnly);out.write(r.reason.toUtf8()+"\n");return r.allowed?0:41;}
 if(args.contains("--gsi-endpoint-test"))return gsiEndpointSelfTest();

 if(args.contains("--status-json")){try{PanelBridge bridge;QFile report(QCoreApplication::applicationDirPath()+"/status-check.json");if(!report.open(QIODevice::WriteOnly))return 1;report.write(bridge.diagnostics().toUtf8());return 0;}catch(...){return 2;}}
 if(args.contains("--configure-gsi")||args.contains("--self-test")||args.contains("--worker")||args.contains("--open-steam")||args.contains("--diagnose")){auto line=args.mid(1).join(' ').toStdWString();return legacyPanelEntry(GetModuleHandleW(nullptr),nullptr,line.data(),0);}

 if(args.contains("--theme-test")){
  QTemporaryDir temp;if(!temp.isValid())return 81;
  {PanelBridge b(nullptr,temp.path());if(b.theme()!="blue")return 82;b.setTheme("black");if(b.theme()!="black")return 83;b.setTheme("invalid");if(b.theme()!="black")return 84;}
  {PanelBridge b(nullptr,temp.path());if(b.theme()!="black")return 85;b.setTheme("blue");}
  {PanelBridge b(nullptr,temp.path());if(b.theme()!="blue")return 86;}
  for(const auto* theme:{"kitty","terminal","black","blue"}){
   {PanelBridge b(nullptr,temp.path());b.setTheme(theme);if(b.theme()!=theme)return 90;}
   {PanelBridge b(nullptr,temp.path());if(b.theme()!=theme)return 91;}
  }
  {PanelBridge b(nullptr,temp.path());QFile::remove(temp.path()+"/theme.txt");QDir().mkdir(temp.path()+"/theme.txt");b.setTheme("kitty");if(b.theme()!="blue")return 92;}
  return 0;
 }
 if(args.contains("--theme-preview-black")||args.contains("--theme-preview-blue")||args.contains("--theme-preview-kitty")||args.contains("--theme-preview-terminal")){
  for(const auto* font:{"segoeui.ttf","segoeuib.ttf","seguisb.ttf","consola.ttf"})QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/"+font);
  QTemporaryDir temp;PanelBridge b(nullptr,temp.path());b.setTheme(args.contains("--theme-preview-kitty")?"kitty":args.contains("--theme-preview-terminal")?"terminal":args.contains("--theme-preview-black")?"black":"blue");
  QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("panel",&b);
  engine.setInitialProperties({{"intro",false},{"width",1320},{"height",910}});
  if(args.contains("--diagnostic-preview")){b.select(2);engine.setInitialProperties({{"intro",false},{"width",1000},{"height",680},{"diagnosticsOpen",true},{"diagnosticsText",b.diagnoseEnvironment(2)}});}
  engine.load(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()+"/qml/Main.qml"));
  if(engine.rootObjects().isEmpty())return 87;
  auto w=qobject_cast<QQuickWindow*>(engine.rootObjects().first());if(!w)return 88;
  QTimer::singleShot(1200,&app,[&]{auto img=w->grabWindow();app.exit(!img.isNull()&&img.save(QCoreApplication::applicationDirPath()+"/theme-"+b.theme()+".png")?0:89);});
  return app.exec();
 }
 if(args.contains("--panel-regression-test"))return panelRegressionSelfTest();
 if(args.contains("--bridge-test")){try{QTemporaryDir temp;{PanelBridge bridge(nullptr,temp.path());bridge.rename(2,QStringLiteral("Проверка имени"));bridge.addEnvironment();if(bridge.environments().size()!=7)return 11;}PanelBridge restored(nullptr,temp.path());restored.select(2);return restored.environments().size()==7&&restored.selected().value("name").toString()==QStringLiteral("Проверка имени")?0:12;}catch(...){return 13;}}
 try{QTemporaryDir uiTestRoot;const bool uiTest=args.contains("--qml-test");PanelBridge bridge(nullptr,uiTest?uiTestRoot.path():QString{});QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("panel",&bridge);if(uiTest)engine.setInitialProperties({{"visible",false}});engine.load(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()+"/qml/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
 auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());if(!window)return 3;if(uiTest){bridge.select(2);return bridge.environments().size()==6?0:42;}bridge.setWindow(window);
 if(args.contains("--smoke-detail")||args.contains("--smoke-info")){bridge.select(2);if(args.contains("--smoke-info"))window->setProperty("infoOpen",true);}
 if(args.contains("--smoke-test")||args.contains("--smoke-detail")||args.contains("--smoke-info")){QTimer::singleShot(3500,&app,[&]{QFile report(QCoreApplication::applicationDirPath()+"/ui-smoke.json");if(report.open(QIODevice::WriteOnly))report.write(bridge.diagnostics().toUtf8());auto name=args.contains("--smoke-info")?"ui-info.png":args.contains("--smoke-detail")?"ui-detail.png":"ui-preview.png";window->grabWindow().save(QCoreApplication::applicationDirPath()+"/"+name);app.quit();});}
 return app.exec();}catch(const std::exception& e){qCritical("%s",e.what());return 1;}
}
