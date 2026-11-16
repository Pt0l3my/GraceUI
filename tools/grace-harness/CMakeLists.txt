# grace-harness: a Direct3D 12 window program that loads a test build of graceui.dll and checks its render half without
# a game (§2.2, D85). It links no hook library and no Dear ImGui: it is the game here, nothing more. BUILD.md says how
# to run it under Wine.
add_executable(grace_harness main.cpp)
set_target_properties(grace_harness PROPERTIES OUTPUT_NAME grace-harness)
target_link_libraries(grace_harness PRIVATE grace_abi d3d12 dxgi user32)
target_compile_definitions(grace_harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_dependencies(grace_harness graceui)
gu_own_code(grace_harness)
if(MINGW)
  target_link_options(grace_harness PRIVATE -municode)  # its entry point is wmain
endif()
