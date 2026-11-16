# doctest 2.4.11 (D106), for the unit tests of GraceUI's pure modules only: never compiled into graceui.dll. Downloaded
# at configure time from its own repository at the tag's commit, each file pinned by SHA-256, into the build tree; NOTICE
# credits it with these pins. Its licence (MIT) was read in full on 2026-11-16: no clause on the use of AI.
include(${CMAKE_CURRENT_LIST_DIR}/fetch.cmake)

set(GU_DOCTEST_COMMIT ae7a13539fb71f270b87eb2e874fbac80bc8dda2)  # doctest/doctest, tag v2.4.11
set(GU_DOCTEST_DIR ${CMAKE_BINARY_DIR}/_deps/doctest-2.4.11)
gu_fetch(doctest/doctest ${GU_DOCTEST_COMMIT} LICENSE.txt ${GU_DOCTEST_DIR}
  0fe0b331fa1513dcce8604ff1fa925f32d1cea17d8aeb1c2471fad40d291adc5)
gu_fetch(doctest/doctest ${GU_DOCTEST_COMMIT} doctest/doctest.h ${GU_DOCTEST_DIR}
  44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb)

add_library(gu_doctest INTERFACE)
target_include_directories(gu_doctest SYSTEM INTERFACE ${GU_DOCTEST_DIR})
