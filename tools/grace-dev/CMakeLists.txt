# grace-dev: GraceUI's fake host (§9.2, D168), a developer's tool, never part of graceui.dll or a release. It links the
# real grace-serve, grace-protocol and grace-pages, and serves spaces from their page data and fixtures on loopback,
# playing their scenarios; and it drives grace-view and grace-draw to dump a page's in-game structure on Linux. Its
# library, grace_dev_core, is grace-dev's own and its tests': nothing else links it (tools/check_safety.py checks so).
# Built on Linux, and in the Windows test builds (-DGRACEUI_TEST_EXPORTS=ON). tools/grace-dev/README.md says how to use
# it.

# The built-in spaces' fixtures (fixtures/<space>/topics.json and scenario.json), compiled into grace-dev as grace-pages
# compiles their page data, so that `grace-dev --space demo` needs no checkout beside it. Written at configure time;
# editing a fixture configures again.
file(GLOB _gu_dev_fixtures CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/fixtures/*/topics.json ${CMAKE_SOURCE_DIR}/fixtures/*/scenario.json)
list(SORT _gu_dev_fixtures)
set(_gu_dev_text "// Written by tools/grace-dev/CMakeLists.txt from fixtures/<space>/: do not edit.\n")
string(APPEND _gu_dev_text "#include \"space_data.h\"\n\nnamespace grace::dev {\nnamespace {\n\n")
set(_gu_dev_entries "")
set(_gu_dev_count 0)
foreach(_gu_dev_file IN LISTS _gu_dev_fixtures)
  file(READ ${_gu_dev_file} _gu_dev_content)
  string(LENGTH "${_gu_dev_content}" _gu_dev_size)
  string(FIND "${_gu_dev_content}" ")gracedevfixture\"" _gu_dev_clash)
  if(_gu_dev_size GREATER 60000 OR NOT _gu_dev_clash EQUAL -1)
    message(FATAL_ERROR "${_gu_dev_file}: past 60,000 bytes, or holding the raw string's delimiter: split it")
  endif()
  get_filename_component(_gu_dev_dir ${_gu_dev_file} DIRECTORY)
  get_filename_component(_gu_dev_space ${_gu_dev_dir} NAME)
  get_filename_component(_gu_dev_name ${_gu_dev_file} NAME)
  string(APPEND _gu_dev_text
    "constexpr std::string_view k${_gu_dev_count} = R\"gracedevfixture(${_gu_dev_content})gracedevfixture\";\n")
  string(APPEND _gu_dev_entries "      {\"${_gu_dev_space}\", \"${_gu_dev_name}\", k${_gu_dev_count}},\n")
  math(EXPR _gu_dev_count "${_gu_dev_count} + 1")
endforeach()
string(APPEND _gu_dev_text "\n}  // namespace\n\nstd::span<const EmbeddedFixture> embedded_fixtures() noexcept {\n")
if(_gu_dev_count EQUAL 0)
  string(APPEND _gu_dev_text "  return {};\n")
else()
  string(APPEND _gu_dev_text
    "  static constexpr EmbeddedFixture kFiles[] = {\n${_gu_dev_entries}  };\n  return kFiles;\n")
endif()
string(APPEND _gu_dev_text "}\n\n}  // namespace grace::dev\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp.next "${_gu_dev_text}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp.next ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp COPYONLY)

add_library(grace_dev_core STATIC
  cli.cpp
  dump.cpp
  host.cpp
  json_text.cpp
  scenario.cpp
  space_data.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp)
target_include_directories(grace_dev_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The layout tests' fake face (dump.h says why).
target_include_directories(grace_dev_core PRIVATE ${CMAKE_SOURCE_DIR}/src/grace-view/tests)
target_link_libraries(grace_dev_core PUBLIC grace_serve grace_protocol grace_pages grace_view grace_draw)
target_compile_definitions(grace_dev_core PUBLIC GRACE_DEV_VERSION="${PROJECT_VERSION}"
  GRACE_DEV_INCREMENT="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
gu_own_code(grace_dev_core)

add_executable(grace_dev main.cpp)
set_target_properties(grace_dev PROPERTIES OUTPUT_NAME grace-dev)
target_link_libraries(grace_dev PRIVATE grace_dev_core)
if(WIN32)
  target_compile_definitions(grace_dev PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()
if(MINGW)
  target_link_options(grace_dev PRIVATE -municode)  # its entry point is wmain
endif()
gu_own_code(grace_dev)

# Its tests: the fake host over real sockets with grace-serve's own test client (src/grace-serve/io/tests), its
# scenarios, its spaces' checks and the --dump golden; on Linux under CTest, and in the Windows test builds as
# grace_dev_tests.exe, which tools/grace-dev/README.md runs under Wine. CTest also runs grace-dev itself: --dump against
# the same golden, a bad space folder and a bad scenario failing the start, and a run stopped by SIGTERM.
if(GRACEUI_UNIT_TESTS OR (WIN32 AND GRACEUI_TEST_EXPORTS))
  add_executable(grace_dev_tests
    tests/main.cpp
    tests/dump_tests.cpp
    tests/host_tests.cpp
    tests/scenario_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/grace-serve/io/tests/client.cpp
    ${CMAKE_SOURCE_DIR}/src/grace-serve/io/tests/harness.cpp)
  target_include_directories(grace_dev_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/grace-serve/io/tests)
  target_link_libraries(grace_dev_tests PRIVATE grace_dev_core gu_doctest)
  if(WIN32)
    target_link_libraries(grace_dev_tests PRIVATE ws2_32)
  endif()
  target_compile_definitions(grace_dev_tests PRIVATE
    GRACE_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures"
    GRACE_DEV_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
  gu_own_code(grace_dev_tests)
  if(GRACEUI_UNIT_TESTS)
    add_test(NAME grace_dev_tests COMMAND grace_dev_tests)
    set(_gu_dev_cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli.cmake)
    add_test(NAME grace_dev_cli_dump COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:grace_dev>
      "-DARGS=--dump|demo|demo.overview|--size|800x600" -DSTATUS=0
      -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/demo-overview-800x600.txt -P ${_gu_dev_cli})
    add_test(NAME grace_dev_cli_bad_space COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:grace_dev>
      "-DARGS=--space-dir|${CMAKE_CURRENT_SOURCE_DIR}/tests/bad-space" -DSTATUS=2
      "-DMATCH=the space folder 'bad-space': pages\\.json: pages\\[0\\]\\.help\\.steps: not a list" -P ${_gu_dev_cli})
    add_test(NAME grace_dev_cli_bad_scenario COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:grace_dev>
      "-DARGS=--space|demo|--scenario|${CMAKE_CURRENT_SOURCE_DIR}/tests/bad-scenario.json" -DSTATUS=2
      "-DMATCH=bad-scenario\\.json line 14: step 2: lantern\\.wick is not a topic of the space demo" -P ${_gu_dev_cli})
    find_package(Python3 3.11 COMPONENTS Interpreter)
    if(Python3_FOUND)
      add_test(NAME grace_dev_cli_run COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_run.py
        $<TARGET_FILE:grace_dev>)
    endif()
  endif()
endif()
