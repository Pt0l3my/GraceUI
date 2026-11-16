# grace-abi: the public C ABI, whose source is its header, graceui.h (D109), and its rules (D131), pure: what connect
# answers, the mods connected and their handles, the notices and the triple buffer the present thread reads them from,
# the checks on a mod's strings, and the anti-cheat check's matching of names. No Win32 and no Dear ImGui, so they are
# unit-tested on Linux; grace-host links them into graceui.dll.
add_library(grace_abi INTERFACE)
target_include_directories(grace_abi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(grace_abi_rules STATIC
  consumers.cpp
  notices.cpp
  text.cpp)
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
    tests/anticheat_tests.cpp
    tests/consumers_tests.cpp
    tests/notices_tests.cpp
    tests/text_tests.cpp)
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
