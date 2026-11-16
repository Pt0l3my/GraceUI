# hello-notice.dll: an example mod of our own, in C (D85): graceui-sys finds GraceUI, connects and shows a notice. It
# links graceui-sys's headers and nothing of GraceUI's: no hook library, no Dear ImGui (invariant 16).
add_library(hello_notice SHARED hello_notice.c)
set_target_properties(hello_notice PROPERTIES PREFIX "" OUTPUT_NAME hello-notice)
target_link_libraries(hello_notice PRIVATE graceui_sys)
target_compile_definitions(hello_notice PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(hello_notice)
