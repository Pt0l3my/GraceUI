# grace-harness: a Direct3D 12 window program that loads a test build of graceui.dll and checks it without a game (§2.2,
# D85): the render half, the input half through DirectInput, XInput and the window's messages, ABI 1's table, the
# example mods (examples/), two copies, and simulated anti-cheat hits. It links no hook library and no Dear ImGui: it is
# the game here, and a mod through graceui-sys, nothing more. BUILD.md says how to run it under Wine.
add_executable(grace_harness main.cpp)
set_target_properties(grace_harness PROPERTIES OUTPUT_NAME grace-harness)
target_link_libraries(grace_harness PRIVATE grace_abi graceui_sys d3d12 dxgi user32 dinput8 dxguid imm32)
target_compile_definitions(grace_harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(grace_harness PRIVATE ${CMAKE_SOURCE_DIR}/examples/hello-panel)  # what the example draws
add_dependencies(grace_harness graceui hello_notice hello_panel grace_harness_shield)
gu_own_code(grace_harness)
if(MINGW)
  target_link_options(grace_harness PRIVATE -municode)  # its entry point is wmain
endif()

# grace-harness-shield.dll: an empty DLL of ours that stands in for an anti-cheat's module (shield.c).
add_library(grace_harness_shield SHARED shield.c)
set_target_properties(grace_harness_shield PROPERTIES PREFIX "" OUTPUT_NAME grace-harness-shield)
target_compile_definitions(grace_harness_shield PRIVATE WIN32_LEAN_AND_MEAN)
gu_own_code(grace_harness_shield)
