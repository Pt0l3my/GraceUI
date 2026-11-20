# nlohmann/json 3.12.0 (D159), for grace-protocol: header-only, downloaded at configure time from its own repository at
# the tag's commit, each file pinned by SHA-256, into the build tree; NOTICE credits it with these pins. Its licence (MIT)
# was read in full on 2026-11-20: no clause on the use of AI; every part of the single header is marked MIT.
include(${CMAKE_CURRENT_LIST_DIR}/fetch.cmake)

set(GU_JSON_COMMIT 55f93686c01528224f448c19128836e7df245f72)  # nlohmann/json, tag v3.12.0
set(GU_JSON_DIR ${CMAKE_BINARY_DIR}/_deps/nlohmann-json-3.12.0)
gu_fetch(nlohmann/json ${GU_JSON_COMMIT} LICENSE.MIT ${GU_JSON_DIR}
  46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603)
gu_fetch(nlohmann/json ${GU_JSON_COMMIT} single_include/nlohmann/json.hpp ${GU_JSON_DIR}
  aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63)

add_library(gu_nlohmann_json INTERFACE)
target_include_directories(gu_nlohmann_json SYSTEM INTERFACE ${GU_JSON_DIR}/single_include)
# No implicit conversions out of a JSON value: every read goes through grace-protocol's checked readers.
target_compile_definitions(gu_nlohmann_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
