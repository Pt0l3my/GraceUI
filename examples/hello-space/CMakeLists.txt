# hello-space.dll: the fourth example mod of our own, in C++ (D85, D283): a space with a page in both views, its topics
# published and its commands checked and answered. Its pages are space/space.toml, compiled at its build by GraceUI's
# page tool (tools/pages/build.py --build, which runs the page lint) into a space folder, space/ in the build tree,
# that grace-dev and check:space read, and the header hello_space_data.h that it hands register_pages. It links
# graceui-sys's headers and nlohmann/json, and nothing of GraceUI's: no hook library, no Dear ImGui (invariant 16).
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)  # tomllib
set(_hello_space_out ${CMAKE_CURRENT_BINARY_DIR}/space)
set(_hello_space_header ${CMAKE_CURRENT_BINARY_DIR}/hello_space_data.h)
add_custom_command(
  OUTPUT ${_hello_space_out}/pages.json ${_hello_space_out}/settings.json ${_hello_space_header}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/pages/build.py
          --build ${CMAKE_CURRENT_SOURCE_DIR}/space/space.toml --out ${_hello_space_out} --minor 22
          --header ${_hello_space_header}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/space/space.toml ${CMAKE_CURRENT_SOURCE_DIR}/space/topics.json
          ${CMAKE_SOURCE_DIR}/tools/pages/build.py ${CMAKE_SOURCE_DIR}/tools/protocol/generate.py
          ${CMAKE_SOURCE_DIR}/src/grace-protocol/protocol.toml ${CMAKE_SOURCE_DIR}/src/grace-pages/spaces/graceui/space.toml
  COMMENT "hello-space: its space checked by GraceUI's page lint and compiled"
  VERBATIM)
add_custom_target(hello_space_data DEPENDS ${_hello_space_header})
add_library(hello_space SHARED hello_space.cpp)
add_dependencies(hello_space hello_space_data)
set_target_properties(hello_space PROPERTIES PREFIX "" OUTPUT_NAME hello-space)
target_include_directories(hello_space PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(hello_space PRIVATE graceui_sys gu_nlohmann_json)
target_compile_definitions(hello_space PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
gu_own_code(hello_space)
