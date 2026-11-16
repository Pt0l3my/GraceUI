# grace-host: the overlay inside graceui.dll (D69, D107), the only module that compiles Dear ImGui's DX12 and Win32
# backends and MinHook (invariant 16). Its raw pointers, casts and hook code live only in its abi, hooks and draw files
# (D110; tools/check_safety.py). The input half's rules are grace-input's, pure (D124); its hooks are hooks_input.cpp's.
add_library(grace_host STATIC
  abi.cpp
  draw.cpp
  draw_alloc.cpp
  hooks.cpp
  hooks_input.cpp
  hooks_vtable.c
  input.cpp
  log.cpp)
target_include_directories(grace_host
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(grace_host
  PUBLIC grace_abi
  PRIVATE grace_input gu_imgui gu_minhook d3d12 dxgi user32 imm32)
target_compile_definitions(grace_host PUBLIC
  GRACEUI_BUILD_MAJOR=${PROJECT_VERSION_MAJOR}
  GRACEUI_BUILD_MINOR=${PROJECT_VERSION_MINOR}
  GRACEUI_BUILD_PATCH=${PROJECT_VERSION_PATCH}
  WIN32_LEAN_AND_MEAN
  NOMINMAX)
if(GRACEUI_TEST_EXPORTS)
  target_compile_definitions(grace_host PUBLIC GRACEUI_TEST_EXPORTS)
endif()
gu_own_code(grace_host)
gu_dll_module(grace_host)
