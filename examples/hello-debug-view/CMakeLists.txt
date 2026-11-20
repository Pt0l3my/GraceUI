# hello-debug-view.dll: the third example mod of our own, in C++ (D85, D156): EldenNet's part 12 in miniature. A banner
# as a notice under its own key, and a debug view on a panel drawn by its own headless Dear ImGui, at GraceUI's
# version, with no backend (D76), opened and closed by a hotkey of GraceUI's (F11, D155) whose presses it takes through
# poll. It links graceui-sys's headers (graceui_imgui.hpp among them) and Dear ImGui's core (gu_imgui_core) and nothing
# of GraceUI's: no hook library, no renderer backend (invariant 16).
add_library(hello_debug_view SHARED hello_debug_view.cpp)
set_target_properties(hello_debug_view PROPERTIES PREFIX "" OUTPUT_NAME hello-debug-view)
target_include_directories(hello_debug_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hello_debug_view PRIVATE graceui_sys gu_imgui_core)
target_compile_definitions(hello_debug_view PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(hello_debug_view)
