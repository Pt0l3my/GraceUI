# graceui-mock: a mock of GraceUI's ABI 1 in C, for a mod's own unit tests (D85, D285): a GraceUiApi1 table whose slots
# answer as GraceUI's do for what a test needs, record every call and give poll the events a test queues. Pure C99, the
# standard library alone, built everywhere; never linked into graceui.dll or a mod, only into tests.
add_library(graceui_mock STATIC graceui_mock.c)
target_include_directories(graceui_mock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(graceui_mock PUBLIC grace_abi)
target_compile_definitions(graceui_mock PRIVATE
  GRACEUI_MOCK_VERSION=GRACEUI_VERSION_PACK\(${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH}\))
set_target_properties(graceui_mock PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
gu_own_code(graceui_mock)

if(GRACEUI_UNIT_TESTS)
  add_executable(graceui_mock_tests tests/main.cpp tests/mock_tests.cpp tests/mod_in_c.c)
  target_link_libraries(graceui_mock_tests PRIVATE graceui_mock gu_doctest)
  target_compile_definitions(graceui_mock_tests PRIVATE
    GRACE_BUILD_MAJOR=${PROJECT_VERSION_MAJOR} GRACE_BUILD_MINOR=${PROJECT_VERSION_MINOR}
    GRACE_BUILD_PATCH=${PROJECT_VERSION_PATCH})
  set_source_files_properties(tests/mod_in_c.c PROPERTIES LANGUAGE C)
  gu_own_code(graceui_mock_tests)
  add_test(NAME graceui_mock_tests COMMAND graceui_mock_tests)
endif()
