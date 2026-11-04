# Avoid CMake's OEM decoding corrupting localized cl /showIncludes output.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
 set(_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/include-probe")
 file(MAKE_DIRECTORY "${_probe_dir}")
 file(WRITE "${_probe_dir}/cs2panel_include_probe.h" "#pragma once\n")
 file(WRITE "${_probe_dir}/probe.cpp" "#include \"cs2panel_include_probe.h\"\nint cs2panel_probe;\n")
 execute_process(
  COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c "${_probe_dir}/probe.cpp" "/Fo${_probe_dir}/probe.obj"
  OUTPUT_VARIABLE _include_output ERROR_VARIABLE _include_error RESULT_VARIABLE _include_result
  ENCODING UTF-8)
 if(NOT _include_result EQUAL 0)
  message(FATAL_ERROR "Cannot verify MSVC include dependencies: ${_include_error}")
 endif()
 string(REGEX MATCH "(^|\n)([^\"\r\n]+)[A-Za-z]:[^\r\n]*cs2panel_include_probe.h" _include_line "${_include_output}")
 if(NOT _include_line)
  message(FATAL_ERROR "MSVC /showIncludes prefix could not be decoded; refusing unreliable incremental build")
 endif()
 set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
 message(STATUS "Verified MSVC include prefix: ${CMAKE_CL_SHOWINCLUDES_PREFIX}")
endif()
