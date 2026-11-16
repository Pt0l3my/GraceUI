# GraceUI: EldenNet's menu and dashboard, and the overlay host that every mod draws through (G15, D69).
# The in-game parts are C++20 (D106): graceui.dll and its harness, built for Windows x64 with clang-cl against the MSVC
# CRT and SDK that xwin downloads (cmake/clang-cl-xwin.cmake, the release build) or with MinGW-w64
# (cmake/mingw-w64.cmake), the harness run under Wine here. BUILD.md says how.
cmake_minimum_required(VERSION 3.20)
project(GraceUI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(GRACEUI_TEST_EXPORTS
  "graceui.dll with its test-only exports (GraceUI_Test_*) and counters, and the harness; never a release build" OFF)

if(NOT WIN32)
  message(FATAL_ERROR "graceui.dll and its harness are Windows programs: configure with cmake/clang-cl-xwin.cmake or "
    "cmake/mingw-w64.cmake (BUILD.md)")
endif()

# A path under the build tree that ends up in debug information is written relative to it, as the toolchain file does
# for the source tree.
if(MSVC)
  add_compile_options(/clang:-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/)
endif()

include(cmake/third_party.cmake)

# Our own code: 0 warnings (the toolchain's /WX, or -Werror here), and casts written out by name, so that
# tools/check_safety.py can confine every reinterpret_cast to the files D110 allows.
function(gu_own_code target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>)
endfunction()

# The modules graceui.dll links hold no static object with a dynamic initialiser (it would run under the loader lock,
# and graceui.dll does nothing at load, D71) or with a destructor that runs at exit (it is never unloaded and does
# nothing at detach, §2.9). Clang checks both.
function(gu_dll_module target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Werror=exit-time-destructors -Werror=global-constructors>)
  endif()
endfunction()

# graceui.dll and the harness side by side.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/grace-abi)
