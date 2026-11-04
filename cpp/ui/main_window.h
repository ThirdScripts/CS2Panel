// Main window: a Win32 GUI with no toolkit dependency.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/engine.h"
#include "ui/painter.h"
#include "vision/radar_provider.h"
#include "session/practice_launcher.h"

namespace cs2n {

struct AppSettings {
  CaptureRegion region{8, 8, 373, 373};
  MarkerTuning tuning;
  std::string meshPath;
  std::string logsDirectory = "logs";
  std::string mapName = "de_dust2";
};

class MainWindow {
 public:
  MainWindow();
  ~MainWindow();

  bool create(int width, int height, bool background = false, DWORD targetPid = 0);
  void show();
  void setVisualTheme(int theme){if(window_)SendMessageW(window_,WM_APP+211,WPARAM(theme),0);}
  bool layoutFits() const;
  void setStopEvent(HANDLE event){stopEvent_=event;}
  void setTargetProcess(HANDLE process){targetProcess_=process;}
  void setDeathmatchLauncher(std::unique_ptr<session::DeathmatchLauncher> launcher){practice_=std::move(launcher);}
  int runLoop();

  // Owned by the window; started once the operator hits Start.
  void setEngine(std::shared_ptr<Engine> engine) { engine_ = std::move(engine); }
  void setRadarProvider(std::shared_ptr<RadarPositionProvider> provider) { radar_ = std::move(provider); }
  void setNavMesh(const NavMesh* mesh) { mesh_ = mesh; }

 private:
  static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);

  void buildControls();
  void onCommand(int identifier);
  void onTimer();
  void onPaint();
  void layout();
  void refreshStatus();
  void appendLog(const std::string& line);
  HWND makeButton(const wchar_t* text, int x, int y, int width, int height, int identifier);

  HBRUSH surfaceBrush_ = nullptr;
  HFONT uiFont_ = nullptr;
  HFONT headingFont_ = nullptr;
  HWND window_ = nullptr;
  bool background_ = false;
  int theme_ = 0;
  std::string lastLogMessage_;
  COLORREF themed(COLORREF color) const;
  DWORD targetPid_ = 0;
  HANDLE stopEvent_ = nullptr;
  HANDLE targetProcess_ = nullptr; // Borrowed; native_main owns the process handle.
  HWND canvas_ = nullptr;
  HWND statusLabel_ = nullptr;
  HWND poseLabel_ = nullptr;
  HWND mapLabel_ = nullptr;
  HWND rateLabel_ = nullptr;
  HWND stuckLabel_ = nullptr;
  HWND logList_ = nullptr;
  HWND startButton_ = nullptr;
  HWND stopButton_ = nullptr;
  HWND emergencyButton_ = nullptr;
  HWND combatBox_=nullptr;
  HWND combatLabel_=nullptr;
  HWND intensityBox_=nullptr;

  Painter painter_;
  std::shared_ptr<Engine> engine_;
  std::unique_ptr<session::DeathmatchLauncher> practice_;
  bool practicePending_=false;
  std::shared_ptr<RadarPositionProvider> radar_;
  const NavMesh* mesh_ = nullptr;
  int lastWidth_ = 0;
  int lastHeight_ = 0;
};

}  // namespace cs2n
