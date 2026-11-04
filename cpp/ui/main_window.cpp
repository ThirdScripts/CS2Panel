#include "ui/main_window.h"
#include "ui/theme.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/types.h"
#include "movement/windows_input.h"

namespace cs2n {
namespace {

constexpr int kIdCanvas = 100;
constexpr int kIdStart = 101;
constexpr int kIdStop = 102;
constexpr int kIdEmergency = 103;
constexpr int kIdCombat=106;
constexpr int kIdIntensity=107;
constexpr int kIdTimer = 200;

std::wstring widen(const std::string& text) {
  if (text.empty()) return L"";
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (needed <= 0) return L"";
  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
  out.resize(out.size()-1);
  return out;
}


}  // namespace

COLORREF MainWindow::themed(COLORREF c) const {
 return themeColor(c,theme_);
}
MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
  practice_.reset();
  if (window_ != nullptr && IsWindow(window_)) DestroyWindow(window_);
  if(surfaceBrush_)DeleteObject(surfaceBrush_);
  if(uiFont_)DeleteObject(uiFont_);
  if(headingFont_)DeleteObject(headingFont_);
}

bool MainWindow::create(int width, int height, bool background, DWORD targetPid) {
  surfaceBrush_=CreateSolidBrush(themed(RGB(14,27,43)));
  uiFont_=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  headingFont_=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  targetPid_=targetPid;
  background_ = background;
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &MainWindow::windowProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = L"CS2NavigatorWindow";
  RegisterClassExW(&windowClass);

  window_ = CreateWindowExW(0, L"CS2NavigatorWindow", background_ ? L"CS2 Navigator — фоновое управление (экспериментальный режим)" : L"CS2 Navigator — обычное управление", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (window_ == nullptr) return false;
  RECT client{};GetClientRect(window_,&client);
  lastWidth_ = client.right;
  lastHeight_ = client.bottom;
  buildControls();
  SetTimer(window_, kIdTimer, 100, nullptr);
  if(!targetPid_){RegisterHotKey(window_,8,MOD_NOREPEAT,VK_F8);
    RegisterHotKey(window_,10,MOD_NOREPEAT,VK_F10);}
  else {const auto title=L"CS2 Navigator — PID "+std::to_wstring(targetPid_);SetWindowTextW(window_,title.c_str());
    SetWindowTextW(startButton_,L"Запустить");}
  return true;
}

bool MainWindow::layoutFits() const {
  RECT client{};GetClientRect(window_,&client);
  for(HWND control:{canvas_,startButton_,stopButton_,emergencyButton_,statusLabel_,poseLabel_,mapLabel_,rateLabel_,stuckLabel_,combatBox_,combatLabel_,intensityBox_,logList_}){
    if(!(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE))continue;
    RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&r),2);
    if(r.left<0||r.top<0||r.right>client.right||r.bottom>client.bottom)return false;
  }
  return true;
}

void MainWindow::show() {
  if (window_ != nullptr) {
    ShowWindow(window_, SW_SHOW);
    ShowWindow(window_, SW_RESTORE);
  }
}

int MainWindow::runLoop() {
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if(IsDialogMessageW(window_,&message))continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

LRESULT CALLBACK MainWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  MainWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<MainWindow*>(create->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }
  if (self != nullptr) return self->handle(message, wParam, lParam);
  return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::handle(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_APP+211:
      if(wParam<=3 && theme_!=int(wParam)){
        theme_=int(wParam);
        auto nextFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,theme_==3?L"Consolas":L"Segoe UI");
        if(nextFont){for(HWND c:{startButton_,stopButton_,emergencyButton_,statusLabel_,poseLabel_,mapLabel_,rateLabel_,stuckLabel_,combatBox_,combatLabel_,intensityBox_,logList_})SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(nextFont),TRUE);if(uiFont_)DeleteObject(uiFont_);uiFont_=nextFont;}
        auto brush=CreateSolidBrush(themed(RGB(14,27,43)));
        if(brush){if(surfaceBrush_)DeleteObject(surfaceBrush_);surfaceBrush_=brush;}
        RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
      }
      return 0;
    case WM_SIZE: {
      RECT rect{};
      GetClientRect(window_, &rect);
      lastWidth_ = rect.right - rect.left;
      lastHeight_ = rect.bottom - rect.top;
      layout();
      return 0;
    }
    case WM_HOTKEY:
        if(wParam==10) onCommand(kIdEmergency);
      if(engine_ && wParam==8 && (background_ || cs2ForegroundActive())) {
        onCommand(kIdStart);
      }
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
      auto dc=reinterpret_cast<HDC>(wParam);
      SetTextColor(dc,themed(RGB(174,196,216)));SetBkColor(dc,themed(RGB(14,27,43)));
      return reinterpret_cast<LRESULT>(surfaceBrush_);
    }
    case WM_DRAWITEM: {
      auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
      if(draw->CtlID==kIdCanvas) {
        int w=draw->rcItem.right, h=draw->rcItem.bottom;
        HDC buffer=CreateCompatibleDC(draw->hDC);HBITMAP bitmap=CreateCompatibleBitmap(draw->hDC,w,h);
        auto old=SelectObject(buffer,bitmap);painter_.paint(buffer,w,h,theme_);
        BitBlt(draw->hDC,0,0,w,h,buffer,0,0,SRCCOPY);
        SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);return TRUE;
      }
      if(draw->CtlType==ODT_BUTTON){
        const bool primary=draw->CtlID==kIdStart, danger=draw->CtlID==kIdEmergency;
        auto color=primary?themed(RGB(41,175,217)):danger?themed(RGB(71,30,46)):themed(RGB(25,47,66));
        if(draw->itemState&ODS_SELECTED)color=themed(RGB(33,83,108));
        auto brush=CreateSolidBrush(color);auto pen=CreatePen(PS_SOLID,1,primary?themed(RGB(75,205,244)):themed(RGB(43,72,96)));
        auto ob=SelectObject(draw->hDC,brush),op=SelectObject(draw->hDC,pen);
        RoundRect(draw->hDC,draw->rcItem.left,draw->rcItem.top,draw->rcItem.right,draw->rcItem.bottom,10,10);
        SelectObject(draw->hDC,ob);SelectObject(draw->hDC,op);DeleteObject(brush);DeleteObject(pen);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,(draw->itemState&ODS_DISABLED)?themed(RGB(89,112,132)):primary?themed(RGB(5,25,39)):themed(RGB(220,235,248)));
        auto of=SelectObject(draw->hDC,uiFont_);wchar_t text[128]{};GetWindowTextW(draw->hwndItem,text,128);
        DrawTextW(draw->hDC,text,-1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(draw->hDC,of);
        if(draw->itemState&ODS_FOCUS){auto r=draw->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(draw->hDC,&r);}return TRUE;
      }
      return DefWindowProcW(window_,message,wParam,lParam);
    }
    case WM_COMMAND:
      onCommand(LOWORD(wParam));
      return 0;
    case WM_TIMER:
      if (wParam == kIdTimer) onTimer();
      return 0;
    case WM_PAINT:
      onPaint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_DESTROY:
      if(practice_)practice_->cancel();practicePending_=false;
      if(engine_) engine_->stop();
      UnregisterHotKey(window_,8);UnregisterHotKey(window_,10);
      KillTimer(window_, kIdTimer);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window_, message, wParam, lParam);
  }
}

HWND MainWindow::makeButton(const wchar_t* text, int x, int y, int width, int height, int identifier) {
  return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, width, height, window_,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), GetModuleHandleW(nullptr),
                         nullptr);
}

void MainWindow::buildControls() {
  combatBox_=CreateWindowExW(0,L"BUTTON",L"Оборона: все противники",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
    280,0,220,30,window_,reinterpret_cast<HMENU>(kIdCombat),GetModuleHandleW(nullptr),nullptr);
  SendMessageW(combatBox_,BM_SETCHECK,BST_CHECKED,0);
  intensityBox_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,
    800,100,250,160,window_,reinterpret_cast<HMENU>(kIdIntensity),GetModuleHandleW(nullptr),nullptr);
  for(const auto* label:{L"Интенсивность: спокойная",L"Интенсивность: средняя",L"Интенсивность: высокая",L"Интенсивность: монстр"})
    SendMessageW(intensityBox_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
  SendMessageW(intensityBox_,CB_SETCURSEL,1,0);
  combatLabel_=CreateWindowExW(0,L"STATIC",L"Оборона готова",WS_CHILD|WS_VISIBLE,0,124,1000,22,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
  canvas_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 640, 480, window_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCanvas)), GetModuleHandleW(nullptr),
                            nullptr);
  startButton_ = makeButton(L"Старт / F8", 0, 0, 160, 30, kIdStart);
  stopButton_ = makeButton(L"Стоп", 170, 0, 90, 30, kIdStop);
  emergencyButton_ = makeButton(L"СТОП / F10", 270, 0, 150, 30, kIdEmergency);
  statusLabel_ = CreateWindowExW(0, L"STATIC", L"State: IDLE", WS_CHILD | WS_VISIBLE, 0, 36, 800, 22, window_,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  poseLabel_ = CreateWindowExW(0, L"STATIC", L"Position: —", WS_CHILD | WS_VISIBLE, 0, 58, 800, 22, window_,
                               nullptr, GetModuleHandleW(nullptr), nullptr);
  mapLabel_ = CreateWindowExW(0, L"STATIC", L"Map: —", WS_CHILD | WS_VISIBLE, 0, 80, 400, 22, window_, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
  rateLabel_ = CreateWindowExW(0, L"STATIC", L"Rates: —", WS_CHILD | WS_VISIBLE, 400, 80, 400, 22, window_, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
  stuckLabel_ = CreateWindowExW(0, L"STATIC", L"Stuck: 0", WS_CHILD | WS_VISIBLE, 0, 102, 800, 22, window_, nullptr,
                                GetModuleHandleW(nullptr), nullptr);

  logList_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                             0, 0, 200, 200, window_, nullptr, GetModuleHandleW(nullptr), nullptr);

  for (HWND control : {statusLabel_, poseLabel_, mapLabel_, rateLabel_, stuckLabel_, combatBox_,combatLabel_,intensityBox_,logList_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  }
  for(HWND control:{startButton_,stopButton_,emergencyButton_}){
    SetWindowLongPtrW(control,GWL_STYLE,(GetWindowLongPtrW(control,GWL_STYLE)&~BS_TYPEMASK)|BS_OWNERDRAW|WS_TABSTOP);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont_),TRUE);
  }
  for(HWND control:{statusLabel_,poseLabel_,mapLabel_,rateLabel_,stuckLabel_,combatBox_,combatLabel_,intensityBox_,logList_}){
    SetWindowTheme(control,L"",L"");SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont_),TRUE);
  }
  SetWindowTextW(startButton_,L"Запустить");SetWindowTextW(stopButton_,L"Стоп");SetWindowTextW(emergencyButton_,L"Остановить · F10");
  layout();
}

void MainWindow::layout() {
  if (canvas_ == nullptr) return;
  const int gap=16, side=260;
  const int content=std::max(240,lastWidth_-side-gap*3), right=content+gap*2;
  const int bottom=targetPid_?lastHeight_-16:lastHeight_-64;
  MoveWindow(startButton_,16,46,132,36,TRUE);
  MoveWindow(stopButton_,158,46,92,36,TRUE);
  MoveWindow(emergencyButton_,260,46,164,36,TRUE);
  MoveWindow(statusLabel_,16,94,std::max(200,lastWidth_-32),22,TRUE);
  MoveWindow(mapLabel_,16,124,content,22,TRUE);
  MoveWindow(canvas_,16,154,content,std::max(80,bottom-224),TRUE);
  MoveWindow(poseLabel_,16,bottom-60,content,22,TRUE);
  MoveWindow(rateLabel_,16,bottom-34,content,22,TRUE);
  MoveWindow(intensityBox_,right,124,side,180,TRUE);
  MoveWindow(combatBox_,right,162,side,28,TRUE);
  MoveWindow(combatLabel_,right,198,side,42,TRUE);
  MoveWindow(stuckLabel_,right,246,side,22,TRUE);
  MoveWindow(logList_,right,278,side,std::max(60,bottom-278),TRUE);
  InvalidateRect(window_,nullptr,FALSE);
}

void MainWindow::onCommand(int identifier) {
  if(identifier==kIdIntensity&&engine_){const auto selected=SendMessageW(intensityBox_,CB_GETCURSEL,0,0);if(selected>=0&&selected<=3)engine_->setIntensity(double(selected)*.5);}
  if(identifier==kIdCombat && engine_) engine_->setCombatEnabled(SendMessageW(combatBox_,BM_GETCHECK,0,0)==BST_CHECKED);
  if (identifier == kIdStart) {
    if (engine_) {
      if(practicePending_){practice_->cancel();practicePending_=false;}
      else if(engine_->running())engine_->stop();
      else if(practice_){practicePending_=true;practice_->start();}
      else engine_->start();
    }
  } else if (identifier == kIdStop) {
    if(practice_)practice_->cancel();practicePending_=false;
    if (engine_) engine_->stop();
  } else if (identifier == kIdEmergency) {
    if(practice_)practice_->cancel();practicePending_=false;
    if (engine_) engine_->emergencyStop();
  }
}

void MainWindow::appendLog(const std::string& line) {
  if (logList_ == nullptr) return;
  const std::wstring wide = widen(line);
  SendMessageW(logList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
  const int count = static_cast<int>(SendMessageW(logList_, LB_GETCOUNT, 0, 0));
  if (count > 300) SendMessageW(logList_, LB_DELETESTRING, 0, 0);
  SendMessageW(logList_, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
}

void MainWindow::onTimer() {
  if(targetProcess_ && WaitForSingleObject(targetProcess_,0)==WAIT_OBJECT_0){
    if(practice_)practice_->cancel();practicePending_=false;
    if(engine_)engine_->stop();
    DestroyWindow(window_);return;
  }
  SetPropW(window_, L"CS2NavigatorRunning", reinterpret_cast<HANDLE>(practicePending_ || (engine_ && engine_->running()) ? 2 : 1));
  if(stopEvent_&&WaitForSingleObject(stopEvent_,0)==WAIT_OBJECT_0){onCommand(kIdStop);ResetEvent(stopEvent_);}
  if(practicePending_){
    auto status=practice_->status();
    if(status==session::LaunchStatus::Ready){practicePending_=false;appendLog(practice_->message());if(engine_)engine_->start();}
    else if(status==session::LaunchStatus::Failed||status==session::LaunchStatus::Cancelled){practicePending_=false;appendLog("Deathmatch: "+practice_->message());}
  }
  refreshStatus();
  if(practicePending_){SetWindowTextW(startButton_,L"Остановить запуск");SetWindowTextW(statusLabel_,widen(practice_->message()).c_str());}
  {
    DrawModel model;
    model.mesh = mesh_;

    if (engine_) {
      model.path = engine_->currentPath();
      const auto snapshot = engine_->snapshot();
      model.mesh = snapshot.activeMesh;
      if (snapshot.havePlayer) {
        model.position = snapshot.player.position.xy();
        model.yaw = snapshot.player.yaw;
        model.havePose = true;
      }
    }
    painter_.set(model);
  }
  auto snap=engine_?engine_->snapshot():EngineSnapshot{};
  std::string message=snap.status+" "+snap.lastError;
  if(message!=lastLogMessage_) {appendLog(message);lastLogMessage_=message;}
  InvalidateRect(canvas_,nullptr,FALSE);
}

void MainWindow::refreshStatus() {
  if (engine_ == nullptr || statusLabel_ == nullptr) return;
  const EngineSnapshot snapshot = engine_->snapshot();
  SetWindowTextW(startButton_,engine_->running()?L"На паузу":L"Запустить");

  wchar_t combatText[256];
  swprintf_s(combatText,L"Оборона: %s  •  Целей: %d\nЦель: %u  •  Ошибка: %.2f°",
    !snapshot.combatEnabled?L"выкл":snapshot.firing?L"стрельба":snapshot.combatActive?L"наведение":L"поиск",
    snapshot.combatTargets,snapshot.combatTarget,snapshot.combatError);
  SetWindowTextW(combatLabel_,combatText);

  wchar_t buffer[512];
  swprintf_s(buffer, L"Состояние: %hs    %hs", engineStateName(snapshot.state), snapshot.status.c_str());
  SetWindowTextW(statusLabel_, buffer);

  if (snapshot.havePlayer) {
    swprintf_s(buffer, L"Координаты: (%.1f, %.1f)   Камера: %.1f°   Скорость: %.0f", snapshot.player.position.x,
               snapshot.player.position.y, snapshot.player.yaw * 180.0 / kPi, snapshot.player.velocity.length());
  } else {
    if(targetPid_)swprintf_s(buffer,L"Готов к запуску · F10 — аварийная остановка");
    else swprintf_s(buffer, background_ ? L"Фоновый режим. Т и АК-47 — автоматически без фокуса. F8 — старт/стоп. F10 — остановить. " : L"F8 — старт/стоп в CS2. F10 / Escape — остановить.");
  }
  SetWindowTextW(poseLabel_, buffer);

  swprintf_s(buffer, L"Карта: %hs   •   Маршрут: %zu   •   Цель: %zu   •   Осталось: %.0f",
             snapshot.detectedMap.empty() ? (snapshot.player.mapName.empty() ? "-" : snapshot.player.mapName.c_str()) : snapshot.detectedMap.c_str(), snapshot.pathPoints,
             snapshot.targetIndex, snapshot.remainingDistance);
  SetWindowTextW(mapLabel_, buffer);

  swprintf_s(buffer, L"Управление: %.0f Гц   Данные: %.1f Гц   Угол: %.1f°", snapshot.controlHz,
             snapshot.providerHz, snapshot.aimErrorDeg);
  SetWindowTextW(rateLabel_, buffer);

  swprintf_s(buffer, L"Восстановлений: %d   Попытка: %d", snapshot.stuckCount, snapshot.recoveryAttempt);
  SetWindowTextW(stuckLabel_, buffer);
}

void MainWindow::onPaint() {
  PAINTSTRUCT paint{};HDC dc=BeginPaint(window_,&paint);
  FillRect(dc,&paint.rcPaint,surfaceBrush_);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,themed(RGB(225,239,251)));
  auto old=SelectObject(dc,headingFont_);RECT heading{16,10,lastWidth_-16,36};
  DrawTextW(dc,L"Навигация и управление",-1,&heading,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
  SelectObject(dc,old);EndPaint(window_,&paint);
}
} // namespace cs2n
