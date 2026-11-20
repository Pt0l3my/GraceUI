# grace-protocol: the bridge protocol (§6, D108, D159): the envelope, every message kind, the error codes, the limits,
# the settings description and the framework's commands, as C++ types generated with their checked decoders and
# encoders from protocol.toml by tools/protocol/generate.py, which writes the web app's TypeScript types beside them.
# Pure: no I/O, threads or clocks; it links nlohmann/json alone, header-only (gu_nlohmann_json). Parsing never throws.
include(${CMAKE_SOURCE_DIR}/cmake/nlohmann_json.cmake)

add_library(grace_protocol STATIC
  codec.cpp
  frame.cpp
  messages.cpp)
target_include_directories(grace_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_protocol PUBLIC gu_nlohmann_json)
gu_own_code(grace_protocol)
gu_dll_module(grace_protocol)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_protocol_tests tests/main.cpp tests/protocol_tests.cpp)
  target_link_libraries(grace_protocol_tests PRIVATE grace_protocol gu_doctest)
  target_compile_definitions(grace_protocol_tests PRIVATE
    GRACE_PROTOCOL_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/frames.jsonl")
  gu_own_code(grace_protocol_tests)
  add_test(NAME grace_protocol_tests COMMAND grace_protocol_tests)
  find_package(Python3 3.11 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME protocol_check COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/protocol/generate.py --check)
    add_test(NAME protocol_tool_tests
             COMMAND ${Python3_EXECUTABLE} -I -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/protocol)
  endif()
  # The generated TypeScript type-checks under tsc --strict, where tsc is installed (the web shell brings its own, S11.2.k).
  find_program(GU_TSC tsc)
  if(GU_TSC)
    add_test(NAME protocol_typescript
             COMMAND ${GU_TSC} --strict --noEmit --target es2022 --lib es2022 ${CMAKE_SOURCE_DIR}/web/src/protocol.ts)
  endif()
endif()

# The frame parser under libFuzzer (§9.1): Linux with Clang only, -DGRACEUI_FUZZ=ON, as grace-abi's fuzzers.
if(GRACEUI_FUZZ)
  target_compile_options(grace_protocol PRIVATE -fsanitize=fuzzer-no-link)
  add_executable(grace_frame_fuzz fuzz/frame_fuzz.cpp)
  target_link_libraries(grace_frame_fuzz PRIVATE grace_protocol)
  target_compile_options(grace_frame_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(grace_frame_fuzz PRIVATE -fsanitize=fuzzer)
endif()
