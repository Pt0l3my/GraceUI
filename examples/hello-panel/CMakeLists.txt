# hello-panel.dll: the second example mod of our own, in C++ (D85): a panel drawn with Dear ImGui's core, headless, at
# GraceUI's version, with no backend (D76). It links graceui-sys's headers and Dear ImGui's core (gu_imgui_core) and
# nothing of GraceUI's: no hook library, no renderer backend (invariant 16). Its face is fetched with GraceUI's
# (cmake/fonts.cmake) into bin\hello-panel\.
add_library(hello_panel SHARED hello_panel.cpp)
set_target_properties(hello_panel PROPERTIES PREFIX "" OUTPUT_NAME hello-panel)
target_include_directories(hello_panel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hello_panel PRIVATE graceui_sys gu_imgui_core)
target_compile_definitions(hello_panel PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(hello_panel)
