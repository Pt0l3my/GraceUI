# grace-tokens: the Grace theme's tokens for the in-game view (D108, D157), a header that tools/tokens/build.py
# generates from tokens/grace.toml beside web/src/tokens.css, so the two views cannot drift apart (invariant 13). Pure:
# no Win32 and no Dear ImGui. Its checks (contrast, type, motion, the outputs current) are the tool's, run by CTest.
add_library(grace_tokens INTERFACE)
target_include_directories(grace_tokens INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_tokens_tests tests/main.cpp tests/tokens_tests.cpp)
  target_link_libraries(grace_tokens_tests PRIVATE grace_tokens gu_doctest)
  gu_own_code(grace_tokens_tests)
  add_test(NAME grace_tokens_tests COMMAND grace_tokens_tests)
  find_package(Python3 3.11 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME tokens_check COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/tokens/build.py --check)
    add_test(NAME tokens_tool_tests
             COMMAND ${Python3_EXECUTABLE} -I -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/tokens)
  endif()
endif()
