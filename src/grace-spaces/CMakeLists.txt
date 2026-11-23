# grace-spaces (D169): the spaces GraceUI holds for both views. The mods' spaces across the ABI (registry.h): each mod's
# one space, checked at register_pages and handed to grace-serve for the browser view, its frames routed both ways and
# its commands' answers routed by reply handles with a deadline; and GraceUI's own space (own_space.h), whose one
# settings store the in-game menu and the browser's views share. It links grace-serve, so it is not pure, but it takes
# no Win32: its tests run on Linux against the real server, with the server suite's client.
add_library(grace_spaces STATIC registry.cpp own_space.cpp devices.cpp)
target_include_directories(grace_spaces PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_spaces PUBLIC grace_serve grace_protocol grace_pages PRIVATE grace_view)
gu_own_code(grace_spaces)
gu_dll_module(grace_spaces)

if(GRACEUI_UNIT_TESTS OR (WIN32 AND GRACEUI_TEST_EXPORTS))
  set(io_tests ${CMAKE_SOURCE_DIR}/src/grace-serve/io/tests)
  add_executable(grace_spaces_tests tests/main.cpp tests/spaces_tests.cpp tests/devices_tests.cpp ${io_tests}/client.cpp ${io_tests}/harness.cpp)
  target_include_directories(grace_spaces_tests PRIVATE ${io_tests})
  target_link_libraries(grace_spaces_tests PRIVATE grace_spaces grace_serve grace_pages gu_doctest)
  if(WIN32)
    target_link_libraries(grace_spaces_tests PRIVATE ws2_32)
  endif()
  target_compile_definitions(grace_spaces_tests PRIVATE GRACE_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures")
  gu_own_code(grace_spaces_tests)
  if(GRACEUI_UNIT_TESTS)
    add_test(NAME grace_spaces_tests COMMAND grace_spaces_tests)
  endif()
endif()
