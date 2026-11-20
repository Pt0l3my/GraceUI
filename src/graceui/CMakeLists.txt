# graceui.dll: the host (D69). Its exports are graceui.h's two (and, in a test build only, graceui_test.h's); ABI 1's
# table is abi_table.cpp's (D131).
add_library(graceui SHARED abi.cpp abi_table.cpp dllmain.cpp)
set_target_properties(graceui PROPERTIES PREFIX "" OUTPUT_NAME graceui)
target_link_libraries(graceui PRIVATE grace_host)
target_compile_definitions(graceui PRIVATE GRACEUI_BUILDING_DLL WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(graceui)
gu_dll_module(graceui)

# The GraceUI folder as it is installed (§2.7), laid out beside bin/ after each build: graceui.dll, LICENSE.txt,
# NOTICE.txt, fonts/ and the graceui.toml that lists them (D52), for a consumer's own tests to install from. A test
# build's folder carries the test exports and is never a release; the release job lays out its folder the same way,
# with the web app once it is built.
find_package(Python3 3.11 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(GU_FOLDER ${CMAKE_BINARY_DIR}/GraceUI)
  add_custom_command(TARGET graceui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GU_FOLDER}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:graceui> ${GU_FOLDER}/graceui.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/LICENSE ${GU_FOLDER}/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/NOTICE ${GU_FOLDER}/NOTICE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/fonts ${GU_FOLDER}/fonts
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/release/manifest.py write ${GU_FOLDER}
    VERBATIM)
endif()
