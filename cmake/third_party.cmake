# Dear ImGui 1.93.0 and MinHook 1.3.4 (D107; EldenNet O25 as the owner changed it on 2026-11-16), downloaded at configure
# time from their original repositories at each tag's commit, every file pinned by SHA-256, into the build tree: the
# repository holds no copy of either (D106). NOTICE credits both, with these pins. Only grace-host compiles them
# (invariant 16, checked by tools/check_safety.py). Changing either version needs the owner's approval (D107), after
# re-reading its licence files.
#
# Dear ImGui: its core, and of its backends only DX12 and Win32 (the Win32 one without its gamepad code, §5.4; see
# src/grace-host/imgui/grace_imconfig.h). MinHook: its hook engine and the 64-bit Hacker Disassembler Engine.
set(GU_IMGUI_COMMIT c1aaac65e896a45f81f47a4d68f9959e0f86c859)    # ocornut/imgui, tag v1.93.0
set(GU_MINHOOK_COMMIT c3fcafdc10146beb5919319d0683e44e3c30d537)  # TsudaKageyu/minhook, tag v1.3.4
set(GU_IMGUI_DIR ${CMAKE_BINARY_DIR}/_deps/imgui-1.93.0)
set(GU_MINHOOK_DIR ${CMAKE_BINARY_DIR}/_deps/minhook-1.3.4)

include(${CMAKE_CURRENT_LIST_DIR}/fetch.cmake)  # gu_fetch: one file, unless the build tree holds it with its hash

foreach(pin IN ITEMS
    "LICENSE.txt|173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3"
    "imconfig.h|fa91f606da783669ffd485f3f9e0cd0d92cddeac459efce4c2b9327cce7a3233"
    "imgui.h|314462301948fbd5673f5ead6f9a53acf54767cbee57e186e69f8f8b022c7094"
    "imgui.cpp|9542082a1eaeffcab7b3d8069e707839ce1714d167005e9f273ac9f656b415ed"
    "imgui_draw.cpp|fff3bb04ca11e32041144e83a37b89ec92f4d5c62d681eb7ef29ee6cd7663e84"
    "imgui_internal.h|61bc50172d907cc931159cdce48fbff1db616cda33251c9c3469e316056939e6"
    "imgui_tables.cpp|36635c1cf2ad1b7c88cdced456cceba3a1d02266d78a63d77e9cce92f4d48d61"
    "imgui_widgets.cpp|d2d388f711688f6a5ce1ae3b30168c1bf4f954ead40235b0b2b9667452850c93"
    "imstb_rectpack.h|889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f"
    "imstb_textedit.h|a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741"
    "imstb_truetype.h|c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528"
    "backends/imgui_impl_dx12.h|f9b26b6994e33c2fb80b343c076273564fde9ed3859685f0c2c52428c4634ebf"
    "backends/imgui_impl_dx12.cpp|b3a780be746161f8a869729f354ad5a23ff1b0800ba77d386d0c52c8150fbc56"
    "backends/imgui_impl_win32.h|2736595b8de1e56871661d5d9b96deec567d35521746fbe2ade8f0543acdfd2b"
    "backends/imgui_impl_win32.cpp|6a602ce28470bb209636c40633156e63a530acd49f137819d837d80688d52df6")
  string(REPLACE "|" ";" pin "${pin}")
  list(GET pin 0 path)
  list(GET pin 1 sha256)
  gu_fetch(ocornut/imgui ${GU_IMGUI_COMMIT} ${path} ${GU_IMGUI_DIR} ${sha256})
endforeach()

foreach(pin IN ITEMS
    "LICENSE.txt|d5c2224982b0b95a16b098a561335fa93f3eaf4d0aa964b7843edb986df78dc8"
    "include/MinHook.h|62dff05e63155c6bf8135c36a0d79dedbdcaaede9da26fe38daf0bea34323e38"
    "src/buffer.c|0ab484d7870c3a8e7a3a1ee61e2070072cf445ed85b619d4c4207fed34356d99"
    "src/buffer.h|9769702bd625249e59a19ede05c84365fcc7707e1fa0e5df08df42227cee13e3"
    "src/hook.c|edcce4c81985d7dd5b73feae96d6498c8f749b1bca4c3d2d9926ea9a17a84755"
    "src/trampoline.c|f13a169f803ff5a8e45c58ce668b4377a9878c0fc4452369b8a1325065186b2b"
    "src/trampoline.h|53ed9934492320617a27bfd952ffe7f5b7a4a1d09963d4512447082fe7b727b4"
    "src/hde/hde64.c|824eafa315322e6da105a7ce4ec1fc0e0adeee0106d32152fbb0b59e709bd15c"
    "src/hde/hde64.h|e99aa4997bda14b534c614c3d8cb78a72c4aca91a1212c8b03ec605d1d75e36e"
    "src/hde/table64.h|b774446d2f110ce954fb0a710f4693c5562ddbd8d56fe84106f2ee80db8b50a2"
    "src/hde/pstdint.h|604d1cdf45c5a95bc9cf1bbf39ca05ef83013541101d34971c5cd7577e3d5247")
  string(REPLACE "|" ";" pin "${pin}")
  list(GET pin 0 path)
  list(GET pin 1 sha256)
  gu_fetch(TsudaKageyu/minhook ${GU_MINHOOK_COMMIT} ${path} ${GU_MINHOOK_DIR} ${sha256})
endforeach()

# Third-party code is compiled as published, with its warnings off: the 0-warnings rule is for our own code.
if(MSVC)
  set(_gu_third_party_flags /w)
else()
  set(_gu_third_party_flags -w)
endif()

# Dear ImGui with GraceUI's configuration. The configuration header changes the library's structures, so every file
# that includes imgui.h must see it: the definition is PUBLIC.
add_library(gu_imgui STATIC
  ${GU_IMGUI_DIR}/imgui.cpp
  ${GU_IMGUI_DIR}/imgui_draw.cpp
  ${GU_IMGUI_DIR}/imgui_tables.cpp
  ${GU_IMGUI_DIR}/imgui_widgets.cpp
  ${GU_IMGUI_DIR}/backends/imgui_impl_dx12.cpp
  ${GU_IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(gu_imgui SYSTEM PUBLIC
  ${GU_IMGUI_DIR} ${GU_IMGUI_DIR}/backends ${CMAKE_SOURCE_DIR}/src/grace-host/imgui)
target_compile_definitions(gu_imgui PUBLIC "IMGUI_USER_CONFIG=\"grace_imconfig.h\"")
target_compile_options(gu_imgui PRIVATE ${_gu_third_party_flags})
# What its backends call: dwmapi and gdi32 imported; CreateDXGIFactory1 and D3DCompile bound at run time by grace-host
# instead (D170), whose stand-ins answer the backend's calls, so no Direct3D DLL is imported (MSVC's pragma for
# d3dcompiler.lib finds nothing left to resolve).
target_link_libraries(gu_imgui PUBLIC dwmapi gdi32)

# MinHook, x64 only.
add_library(gu_minhook STATIC
  ${GU_MINHOOK_DIR}/src/buffer.c
  ${GU_MINHOOK_DIR}/src/hook.c
  ${GU_MINHOOK_DIR}/src/trampoline.c
  ${GU_MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(gu_minhook SYSTEM PUBLIC ${GU_MINHOOK_DIR}/include)
target_compile_options(gu_minhook PRIVATE ${_gu_third_party_flags})

# Dear ImGui's core alone, at the same version, with no backend and none of GraceUI's configuration: what a mod may
# compile to build its panels' draw lists headless (D76, D107). Only the example mods link it (tools/check_safety.py);
# graceui.dll never does.
add_library(gu_imgui_core STATIC
  ${GU_IMGUI_DIR}/imgui.cpp
  ${GU_IMGUI_DIR}/imgui_draw.cpp
  ${GU_IMGUI_DIR}/imgui_tables.cpp
  ${GU_IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(gu_imgui_core SYSTEM PUBLIC ${GU_IMGUI_DIR})
target_compile_definitions(gu_imgui_core PUBLIC
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS IMGUI_DISABLE_FILE_FUNCTIONS
  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS IMGUI_DISABLE_WIN32_FUNCTIONS)
target_compile_options(gu_imgui_core PRIVATE ${_gu_third_party_flags})
