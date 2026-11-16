# graceui.dll: the host (D69). Its exports are graceui.h's two (and, in a test build only, graceui_test.h's).
add_library(graceui SHARED abi.cpp dllmain.cpp)
set_target_properties(graceui PROPERTIES PREFIX "" OUTPUT_NAME graceui)
target_link_libraries(graceui PRIVATE grace_host)
target_compile_definitions(graceui PRIVATE GRACEUI_BUILDING_DLL WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(graceui)
gu_dll_module(graceui)
