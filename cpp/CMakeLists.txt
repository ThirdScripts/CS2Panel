cmake_minimum_required(VERSION 3.25)
project(cs2navigator LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../tools/cmake/ShowIncludes.cmake")

if(NOT MSVC)
  message(FATAL_ERROR "MSVC toolchain is required (Microsoft C++ Build Tools).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files are UTF-8; without this flag MSVC reads them in the system code
# page and silently corrupts every non-ASCII literal.
add_compile_options(/utf-8)

option(CS2N_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(cs2n_core STATIC
  perception/audio.cpp
  perception/radar.cpp
  combat/geometry.cpp
  combat/controller.cpp
  core/log.cpp
  core/json.cpp
  core/win32.cpp
  state/provider.cpp
  state/client_state.cpp
  state/gsi_provider.cpp
  state/memory_provider.cpp
  state/cs2_memory.cpp
  navigation/navmesh.cpp
  navigation/navmesh_io.cpp
  navigation/navmesh_builder.cpp
  navigation/path_planner.cpp
  navigation/navigator.cpp
  movement/look_controller.cpp
  movement/move_controller.cpp
  movement/stuck_detector.cpp
  movement/windows_input.cpp
  movement/memory_input.cpp
  movement/game_commands.cpp
  vision/image.cpp
  vision/radar_localizer.cpp
  vision/radar_provider.cpp
  vision/capture.cpp
  app/engine.cpp
  session/practice_launcher.cpp
)
target_include_directories(cs2n_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cs2n_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(cs2n_core PUBLIC user32 gdi32 ws2_32 ole32 mmdevapi windowsapp d3d11 dxgi)

if(CS2N_WARNINGS_AS_ERRORS)
  target_compile_options(cs2n_core PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
else()
  target_compile_options(cs2n_core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

# Reuse the same runtime in the unified panel executable.
option(CS2N_CORE_ONLY "Build only the navigator library for embedding" OFF)
if(CS2N_CORE_ONLY)
  return()
endif()

add_executable(cs2n_app WIN32 app/native_main.cpp ui/main_window.cpp ui/painter.cpp)
target_link_libraries(cs2n_app PRIVATE cs2n_core comctl32 winmm)
add_executable(cs2n_background WIN32 app/native_main.cpp ui/main_window.cpp ui/painter.cpp)
target_compile_definitions(cs2n_background PRIVATE CS2N_BACKGROUND_DEFAULT)
target_link_libraries(cs2n_background PRIVATE cs2n_core comctl32 winmm)

add_executable(cs2n_tests
  tests/test_deathmatch.cpp
  tests/test_main.cpp
  tests/test_json.cpp
  tests/test_navmesh.cpp
  tests/test_path_planner.cpp
  tests/test_look_controller.cpp
  tests/test_stuck_detector.cpp
  tests/test_state.cpp
  tests/test_client_state.cpp
  tests/test_gsi_network.cpp
  tests/test_memory_provider.cpp
  tests/test_vision.cpp
  tests/test_fsm.cpp
  tests/test_combat.cpp
  tests/test_move_controller.cpp
)
target_link_libraries(cs2n_tests PRIVATE cs2n_core)
file(GLOB local_maps CONFIGURE_DEPENDS "assets/*.nav.json" "assets/*.mesh")
if(local_maps)
  target_compile_definitions(cs2n_tests PRIVATE CS2N_LOCAL_MAP_TEST)
else()
  message(STATUS "Local map assets absent: packaged-map integration test is unavailable")
endif()

add_executable(cs2n_sim tools/simulate.cpp)
target_link_libraries(cs2n_sim PRIVATE cs2n_core)

add_executable(cs2n_mem_probe tools/mem_probe/main.cpp)
# Header-only dependency: the probe only needs the snapshot layout, so it stays
# a tiny standalone process with no library attached.
target_include_directories(cs2n_mem_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cs2n_mem_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_executable(cs2n_navgen tools/navgen/main.cpp)
target_link_libraries(cs2n_navgen PRIVATE cs2n_core)

enable_testing()
add_executable(cs2n_binding_probe tools/binding_probe.cpp)
target_link_libraries(cs2n_binding_probe PRIVATE cs2n_core)
add_test(NAME unit COMMAND cs2n_tests)
add_test(NAME runner_empty_filter COMMAND cs2n_tests __audit_nonexistent_filter__)
set_tests_properties(runner_empty_filter PROPERTIES WILL_FAIL TRUE)
add_executable(cs2n_telemetry tools/telemetry.cpp)
add_executable(cs2n_camera_diagnostic tools/camera_diagnostic.cpp)
target_link_libraries(cs2n_camera_diagnostic PRIVATE cs2n_core winmm)
add_executable(cs2n_command_probe tools/command_probe.cpp)
target_link_libraries(cs2n_command_probe PRIVATE cs2n_core)
add_executable(cs2n_background_probe WIN32 tools/background_probe.cpp)
target_link_libraries(cs2n_background_probe PRIVATE cs2n_core)
target_link_libraries(cs2n_telemetry PRIVATE cs2n_core)
add_executable(cs2n_check_routes tools/check_routes.cpp)
target_link_libraries(cs2n_check_routes PRIVATE cs2n_core)

add_executable(cs2n_gsi_state_probe tools/gsi_state_probe.cpp)
target_link_libraries(cs2n_gsi_state_probe PRIVATE cs2n_core)

add_test(NAME gsi_probe_invalid_duration COMMAND cs2n_gsi_state_probe unused.cfg 0)
set_tests_properties(gsi_probe_invalid_duration PROPERTIES WILL_FAIL TRUE)

add_executable(cs2n_menu_probe tools/menu_probe.cpp)
add_executable(cs2n_compatibility_probe tools/compatibility_probe.cpp)
target_link_libraries(cs2n_compatibility_probe PRIVATE cs2n_core)
target_link_libraries(cs2n_menu_probe PRIVATE cs2n_core windowsapp)

add_executable(cs2n_background_menu_probe tools/background_menu_probe.cpp)
target_link_libraries(cs2n_background_menu_probe PRIVATE cs2n_core windowsapp d3d11 dxgi)

add_executable(cs2n_deathmatch_launch_probe tools/deathmatch_launch_probe.cpp)
target_link_libraries(cs2n_deathmatch_launch_probe PRIVATE cs2n_core)

target_link_options(cs2n_deathmatch_launch_probe PRIVATE /MAP)

# All executables share one asset destination: serialize the copy once per build.
add_custom_target(cs2n_assets ALL COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/assets)
add_dependencies(cs2n_app cs2n_assets)
add_dependencies(cs2n_background cs2n_assets)
add_dependencies(cs2n_tests cs2n_assets)
add_dependencies(cs2n_deathmatch_launch_probe cs2n_assets)
