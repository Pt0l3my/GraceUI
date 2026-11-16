# graceui-sys: the helper a mod compiles in to find GraceUI and call it (D70, D136): graceui_sys.h, header-only C, and
# graceui_sys.hpp, a thin C++ wrapper over the table. Nothing is built here; a mod links this target for its include
# folder. It needs kernel32 only, and holds no hook library, no Dear ImGui backend and no hook (invariant 16).
add_library(graceui_sys INTERFACE)
target_include_directories(graceui_sys INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(graceui_sys INTERFACE grace_abi)
