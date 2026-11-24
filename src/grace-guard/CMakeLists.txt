# grace-guard: the test build's heap guard (D235), pure: no Win32 and no Dear ImGui, so it is unit-tested on Linux, under
# the sanitizers too. grace-host links it into graceui.dll, whose test build makes every block of its own through it
# (draw_alloc.cpp); a release build links it and calls none of it.
add_library(grace_guard STATIC heap_guard.cpp)
target_include_directories(grace_guard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
gu_own_code(grace_guard)
gu_dll_module(grace_guard)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_guard_tests tests/main.cpp tests/heap_guard_tests.cpp)
  target_link_libraries(grace_guard_tests PRIVATE grace_guard gu_doctest)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(grace_guard_tests PRIVATE Threads::Threads)
  endif()
  gu_own_code(grace_guard_tests)
  add_test(NAME grace_guard_tests COMMAND grace_guard_tests)
endif()
