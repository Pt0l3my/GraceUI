# grace-input: the input half's rules (D77, D124; invariants 7, 8 and 17), pure: no Win32 and no Dear ImGui, so they are
# unit-tested on Linux. grace-host links them into graceui.dll and feeds them from its window-procedure filter and its
# input detours.
add_library(grace_input STATIC
  devices.cpp
  holder.cpp
  messages.cpp)
target_include_directories(grace_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
gu_own_code(grace_input)
gu_dll_module(grace_input)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_input_tests
    tests/main.cpp
    tests/chars_tests.cpp
    tests/devices_tests.cpp
    tests/holder_tests.cpp
    tests/messages_tests.cpp
    tests/queue_tests.cpp)
  target_link_libraries(grace_input_tests PRIVATE grace_input gu_doctest)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(grace_input_tests PRIVATE Threads::Threads)
  endif()
  gu_own_code(grace_input_tests)
  add_test(NAME grace_input_tests COMMAND grace_input_tests)
endif()
