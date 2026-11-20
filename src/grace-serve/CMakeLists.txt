# grace-serve's pure core (G30, §7.6, D158): the HTTP/1.1 request-head parser and the response-head writer, the checks of
# Host, Origin, Fetch Metadata and the session cookie, the WebSocket handshake and the frame codec with its session's
# state machine, and the small codecs under them (SHA-1, Base64, UTF-8). No Win32, no sockets, no clocks, no threads and
# no allocation, so it is unit-tested and fuzzed on Linux; the I/O part (its sockets, deadlines and two threads) will
# link it, in graceui.dll and in hosts outside the game (G29 (a)).
add_library(grace_serve_core STATIC
  checks.cpp
  codec.cpp
  frames.cpp
  handshake.cpp
  head.cpp
  refusal.cpp
  response.cpp)
target_include_directories(grace_serve_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
gu_own_code(grace_serve_core)
gu_dll_module(grace_serve_core)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_serve_tests
    tests/main.cpp
    tests/chunking_tests.cpp
    tests/checks_tests.cpp
    tests/codec_tests.cpp
    tests/frames_tests.cpp
    tests/handshake_tests.cpp
    tests/head_tests.cpp
    tests/response_tests.cpp
    # grace-abi's counting operator new, for the parser's and the decoder's no-allocation tests.
    ${CMAKE_SOURCE_DIR}/src/grace-abi/tests/alloc_counter.cpp)
  target_include_directories(grace_serve_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/grace-abi/tests)
  target_link_libraries(grace_serve_tests PRIVATE grace_serve_core gu_doctest)
  gu_own_code(grace_serve_tests)
  add_test(NAME grace_serve_tests COMMAND grace_serve_tests)
endif()

# The head parser and the frame decoder under libFuzzer (§9.1, "Server code"; SRV §5.4): Linux with Clang only,
# -DGRACEUI_FUZZ=ON, with AddressSanitizer and UndefinedBehaviorSanitizer (BUILD.md runs them). Each feeds its input
# whole, a byte at a time and at splits the input itself chooses, and traps unless the three agree. Their seeds are in
# fuzz/corpus/, with a dictionary for the head.
if(GRACEUI_FUZZ)
  if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GRACEUI_FUZZ is for Clang on Linux")
  endif()
  target_compile_options(grace_serve_core PRIVATE -fsanitize=fuzzer-no-link)
  foreach(fuzzer IN ITEMS head frame)
    add_executable(grace_${fuzzer}_fuzz fuzz/${fuzzer}_fuzz.cpp)
    target_link_libraries(grace_${fuzzer}_fuzz PRIVATE grace_serve_core)
    target_compile_options(grace_${fuzzer}_fuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(grace_${fuzzer}_fuzz PRIVATE -fsanitize=fuzzer)
    gu_own_code(grace_${fuzzer}_fuzz)
  endforeach()
endif()
