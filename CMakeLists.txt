# GraceUI: EldenNet's menu and dashboard, and the overlay host that every mod draws through (G15, D69).
# The in-game parts are C++20 (D106): graceui.dll and its harness, built for Windows x64 with clang-cl against the MSVC
# CRT and SDK that xwin downloads (cmake/clang-cl-xwin.cmake, the release build) or with MinGW-w64
# (cmake/mingw-w64.cmake), the harness run under Wine here. On Linux the pure modules and their unit tests build alone.
# BUILD.md says how.
cmake_minimum_required(VERSION 3.20)
project(GraceUI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(GRACEUI_TEST_EXPORTS
  "graceui.dll with its test-only exports (GraceUI_Test_*) and counters, and the harness; never a release build" OFF)
if(WIN32)
  set(_gu_unit_tests_default OFF)
else()
  set(_gu_unit_tests_default ON)
endif()
option(GRACEUI_UNIT_TESTS "The pure modules' unit tests (doctest under CTest, D106)" ${_gu_unit_tests_default})
option(GRACEUI_FUZZ "Linux and Clang only: the libFuzzer targets of grace-abi and grace-serve (BUILD.md)" OFF)
set(GRACEUI_SANITIZE "" CACHE STRING
  "Linux only: the sanitizers for the unit tests, as -fsanitize takes them (address,undefined or thread)")

# A path under the build tree that ends up in debug information is written relative to it, as the toolchain file does
# for the source tree.
if(MSVC)
  add_compile_options(/clang:-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/)
endif()

if(GRACEUI_SANITIZE)
  if(WIN32)
    message(FATAL_ERROR "GRACEUI_SANITIZE is for the Linux unit tests")
  endif()
  add_compile_options(-fsanitize=${GRACEUI_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${GRACEUI_SANITIZE})
endif()

if(WIN32)
  include(cmake/third_party.cmake)
  include(cmake/fonts.cmake)
endif()
if(GRACEUI_UNIT_TESTS)
  include(cmake/doctest.cmake)
  enable_testing()
elseif(WIN32 AND GRACEUI_TEST_EXPORTS)
  include(cmake/doctest.cmake)  # for grace-serve's suite on Windows, run under Wine (BUILD.md)
endif()

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

# The pure modules (no Win32, no Dear ImGui): built everywhere, and the only targets on Linux (D106). grace-abi's header
# is the public ABI, its layout checked in C and C++ by every build (D109); its rules are pure (D131). grace-serve's pure
# core is the server's protocol code (G30, §7.6, D158).
add_subdirectory(src/grace-tokens)
add_subdirectory(src/grace-protocol)
add_subdirectory(src/grace-pages)
add_subdirectory(src/grace-view)
add_subdirectory(src/grace-draw)
add_subdirectory(src/grace-input)
add_subdirectory(src/grace-abi)
add_subdirectory(src/grace-serve)
add_subdirectory(src/grace-spaces)

if(WIN32)
  add_subdirectory(src/grace-host)
  add_subdirectory(src/graceui)
  add_subdirectory(src/graceui-sys)
  add_subdirectory(examples)
  if(GRACEUI_TEST_EXPORTS)
    add_subdirectory(tools/grace-harness)
  endif()
endif()

# GraceUI's version file (D52): tools/release/manifest.py writes graceui.toml for a GraceUI folder and checks one.
if(GRACEUI_UNIT_TESTS)
  find_package(Python3 3.11 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME manifest_tool_tests
             COMMAND ${Python3_EXECUTABLE} -I -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/release)
  endif()
endif()
