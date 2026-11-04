// CS2 Navigator 2.0 entry point.
//
// Wires the pieces together: loads the Dust II navigation mesh, starts the GSI
// listener and the radar position provider, composes them into one player state
// source, and hands everything to the engine behind a Win32 window.
//
// Nothing starts moving on launch. The operator presses Start, and the emergency
// stop key (F10) works at every moment.
#include <Windows.h>
#include <commdlg.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "app/engine.h"
#include "core/log.h"
#include "core/win32.h"
#include "movement/windows_input.h"
#include "navigation/navmesh_io.h"
#include "navigation/navigator.h"
#include "state/gsi_provider.h"
#include "ui/main_window.h"
#include "vision/radar_provider.h"

using namespace cs2n;

namespace {

std::string executableDirectory() {
  wchar_t buffer[1024] = {};
  GetModuleFileNameW(nullptr, buffer, 1024);
  std::wstring path(buffer);
  const std::size_t slash = path.find_last_of(L'\\');
  if (slash != std::wstring::npos) path = path.substr(0, slash);
  const int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(needed > 0 ? static_cast<std::size_t>(needed - 1) : 0, '\0');
  if (needed > 0) WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, out.data(), needed, nullptr, nullptr);
  return out;
}

std::string readToken(const std::string& path) {
  std::ifstream file(path);
  if (!file) return "";
  std::string token;
  std::getline(file, token);
  while (!token.empty() && (token.back() == '\r' || token.back() == '\n' || token.back() == ' ')) token.pop_back();
  return token;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
  (void)instance;
  (void)commandLine;

  win32::enableDpiAwareness();
  Logger::instance().startFile("logs", "navigator");

  const std::string base = executableDirectory();
  Logger::instance().info("CS2 Navigator 2.0 starting, base directory: " + base);

  // ---- Navigation mesh -------------------------------------------------
  NavMesh mesh;
  std::string error;
  const std::string meshPath = base + "/assets/dust2.nav.json";
  bool meshLoaded = false;
  if (std::filesystem::exists(meshPath)) {
    meshLoaded = NavMeshIo::loadFile(meshPath, mesh, error);
    if (meshLoaded) {
      Logger::instance().info("Loaded navmesh '" + mesh.name() + "': " + std::to_string(mesh.areaCount()) +
                              " areas, " + std::to_string(mesh.connectionCount()) + " connections");
    } else {
      Logger::instance().error("Cannot load navmesh: " + error);
    }
  } else {
    Logger::instance().warn("No navmesh at " + meshPath + "; navigation will be unavailable until one is built");
  }

  // ---- Player state ----------------------------------------------------
  GsiConfig gsiConfig;
  gsiConfig.token = readToken(base + "/assets/gsi-token.txt");
  gsiConfig.requiredMap = "de_dust2";
  auto gsi = std::make_shared<GsiProvider>(gsiConfig);
  if (!gsi->listening()) Logger::instance().warn("GSI listener did not start; liveness checks will fail");

  RadarProviderConfig radarConfig;
  radarConfig.region = CaptureRegion{8, 8, 373, 373};
  radarConfig.expectedMap = "de_dust2";
  auto radar = std::make_shared<RadarPositionProvider>(radarConfig);

  auto provider = std::make_shared<CompositeProvider>(gsi, radar);

  // ---- Engine ----------------------------------------------------------
  NavigatorConfig navigatorConfig;
  navigatorConfig.strategy = GoalStrategy::RandomReachable;
  Navigator navigator(meshLoaded ? &mesh : nullptr, navigatorConfig);

  WindowsInput input;
  auto engine = std::make_shared<Engine>(&input, provider, navigator);

  // ---- Window ----------------------------------------------------------
  MainWindow window;
  if (!window.create(1120, 760)) {
    Logger::instance().error("Cannot create the main window");
    return 1;
  }
  window.setEngine(engine);
  window.setRadarProvider(radar);
  window.setNavMesh(meshLoaded ? &mesh : nullptr);
  window.show();

  Logger::instance().info("UI ready. Navigation is OFF until Start is pressed. F10 is the emergency stop.");
  const int result = window.runLoop();

  engine->emergencyStop();
  Logger::instance().info("CS2 Navigator 2.0 shutting down");
  Logger::instance().stopFile();
  return result;
}
