# grace-abi: the public C ABI, whose source is its header, graceui.h (D109), and its rules (D131), pure: what connect
# answers, the mods connected and their handles, the notices and the triple buffer the present thread reads them from,
# the checks on a mod's strings, and the anti-cheat check's matching of names; from S10.4.c the panels, the draw-list
# checker and each panel's triple buffer, the textures' and faces' budgets and hand-overs, and the world-state
# providers' claims. No Win32 and no Dear ImGui, so they are unit-tested on Linux; grace-host links them into
# graceui.dll.
add_library(grace_abi INTERFACE)
target_include_directories(grace_abi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(grace_abi_rules STATIC
  consumers.cpp
  drawlist.cpp
  notices.cpp
  panels.cpp
  quiet.cpp
  registry.cpp
  text.cpp
  world.cpp)
target_include_directories(grace_abi_rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
gu_own_code(grace_abi_rules)
gu_dll_module(grace_abi_rules)

# The header's layout, compiled as C and as C++ by every build (D109): its static_asserts are the test, and on Linux
# CTest runs both programs' checks of GRACEUI_API1_COVERS too.
add_executable(grace_abi_layout_c tests/layout.c)
add_executable(grace_abi_layout_cpp tests/layout.cpp)
foreach(layout IN ITEMS grace_abi_layout_c grace_abi_layout_cpp)
  target_include_directories(${layout} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  gu_own_code(${layout})
endforeach()

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_abi_tests
    tests/main.cpp
    tests/alloc_counter.cpp
    tests/anticheat_tests.cpp
    tests/consumers_tests.cpp
    tests/drawlist_tests.cpp
    tests/notices_tests.cpp
    tests/panels_tests.cpp
    tests/quiet_tests.cpp
    tests/registry_tests.cpp
    tests/text_tests.cpp
    tests/world_tests.cpp)
  target_link_libraries(grace_abi_tests PRIVATE grace_abi_rules gu_doctest)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(grace_abi_tests PRIVATE Threads::Threads)
  endif()
  gu_own_code(grace_abi_tests)
  add_test(NAME grace_abi_tests COMMAND grace_abi_tests)
  add_test(NAME grace_abi_layout_c COMMAND grace_abi_layout_c)
  add_test(NAME grace_abi_layout_cpp COMMAND grace_abi_layout_cpp)
endif()

# The draw-list checker under libFuzzer (§9.1): Linux with Clang only, -DGRACEUI_FUZZ=ON, with AddressSanitizer and
# UndefinedBehaviorSanitizer (BUILD.md runs it).
if(GRACEUI_FUZZ)
  if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GRACEUI_FUZZ is for Clang on Linux")
  endif()
  # The rules themselves carry the fuzzer's coverage, so that it is guided by what they do with the bytes.
  target_compile_options(grace_abi_rules PRIVATE -fsanitize=fuzzer-no-link)
  add_executable(grace_drawlist_fuzz fuzz/drawlist_fuzz.cpp)
  target_link_libraries(grace_drawlist_fuzz PRIVATE grace_abi_rules)
  target_compile_options(grace_drawlist_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(grace_drawlist_fuzz PRIVATE -fsanitize=fuzzer)
  gu_own_code(grace_drawlist_fuzz)
  # A mod's font file and ranges: the checks and the character map's count (the atlas's room, D139 as amended).
  add_executable(grace_font_fuzz fuzz/font_fuzz.cpp)
  target_link_libraries(grace_font_fuzz PRIVATE grace_abi_rules)
  target_compile_options(grace_font_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(grace_font_fuzz PRIVATE -fsanitize=fuzzer)
  gu_own_code(grace_font_fuzz)
endif()
