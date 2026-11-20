# grace-pages: GraceUI's built-in spaces (§2.5, D79, D82, D160): its own space (the pages at the rail's foot and its
# own settings) and the demo space, written in spaces/<id>/space.toml and compiled by tools/pages/build.py, whose page
# lint checks them, into builtin.cpp; and a space's settings store, its one validating entry point (D167). Pure: no
# I/O, no Win32.
add_library(grace_pages STATIC builtin.cpp settings_store.cpp)
target_include_directories(grace_pages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_pages PUBLIC grace_protocol)
gu_own_code(grace_pages)
gu_dll_module(grace_pages)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_pages_tests tests/main.cpp tests/pages_tests.cpp tests/store_tests.cpp)
  target_link_libraries(grace_pages_tests PRIVATE grace_pages grace_protocol gu_doctest)
  gu_own_code(grace_pages_tests)
  add_test(NAME grace_pages_tests COMMAND grace_pages_tests)
  find_package(Python3 3.11 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME pages_check COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/pages/build.py --check)
    add_test(NAME pages_tool_tests
             COMMAND ${Python3_EXECUTABLE} -I -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/pages)
  endif()
endif()
