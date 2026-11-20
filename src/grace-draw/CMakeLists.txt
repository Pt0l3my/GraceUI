# grace-draw: grace-view's display list tessellated into ABI 1's draw-list layout (§2.2, §3.16; D162), checked by the same
# checker as a mod's panel. Pure: no ImGui, no Win32; it links grace-view, the tokens and grace-abi's draw-list types.
add_library(grace_draw STATIC tessellate.cpp)
target_include_directories(grace_draw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_draw PUBLIC grace_view grace_tokens grace_abi_rules)
gu_own_code(grace_draw)
gu_dll_module(grace_draw)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_draw_tests tests/main.cpp tests/draw_tests.cpp ${CMAKE_SOURCE_DIR}/src/grace-abi/tests/alloc_counter.cpp)
  target_include_directories(grace_draw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/grace-view/tests
                                                      ${CMAKE_SOURCE_DIR}/src/grace-abi/tests)
  target_link_libraries(grace_draw_tests PRIVATE grace_draw grace_pages gu_doctest)
  gu_own_code(grace_draw_tests)
  add_test(NAME grace_draw_tests COMMAND grace_draw_tests)
endif()
