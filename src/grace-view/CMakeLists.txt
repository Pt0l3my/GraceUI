# grace-view: one space's menu in game (§2.2, §3.16, §5.7; D162): page data, settings and values laid out with the Grace
# tokens into a display list, and the keyboard's and pad's navigation turned into the protocol's commands. Pure: no
# ImGui, no Win32; it links the protocol's types and the tokens, both pure.
add_library(grace_view STATIC display.cpp document.cpp editor.cpp filter.cpp page_view.cpp text.cpp
            text_field.cpp typed.cpp)
target_include_directories(grace_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_view PUBLIC grace_protocol grace_tokens)
gu_own_code(grace_view)
gu_dll_module(grace_view)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_view_tests tests/main.cpp tests/view_tests.cpp tests/text_field_tests.cpp tests/editor_tests.cpp
                 tests/filter_tests.cpp tests/document_tests.cpp tests/typed_tests.cpp)
  target_include_directories(grace_view_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(grace_view_tests PRIVATE grace_view grace_pages gu_doctest)
  target_compile_definitions(grace_view_tests PRIVATE
    GRACE_VIEW_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
    GRACE_FILTER_CASES="${CMAKE_SOURCE_DIR}/fixtures/filter-cases.json"
    GRACE_DOCUMENT_CASES="${CMAKE_SOURCE_DIR}/fixtures/document-cases.json"
    GRACE_TYPED_CASES="${CMAKE_SOURCE_DIR}/fixtures/typed-cases.json")
  gu_own_code(grace_view_tests)
  add_test(NAME grace_view_tests COMMAND grace_view_tests)
endif()
