# grace-serve's pure core (G30, §7.6, D158): the HTTP/1.1 request-head parser and the response-head writer, the checks of
# Host, Origin, Fetch Metadata and the session cookie, the WebSocket handshake and the frame codec with its session's
# state machine, and the small codecs under them (SHA-1, SHA-256, Base64, UTF-8). No Win32, no sockets, no clocks, no
# threads and no allocation, so it is unit-tested and fuzzed on Linux; the I/O part, grace_serve below (its sockets,
# deadlines and two threads), links it, in graceui.dll and in hosts outside the game (G29 (a)).
add_library(grace_serve_core STATIC
  checks.cpp
  codec.cpp
  frames.cpp
  handshake.cpp
  head.cpp
  pairing.cpp
  refusal.cpp
  response.cpp)
target_include_directories(grace_serve_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
gu_own_code(grace_serve_core)
gu_dll_module(grace_serve_core)

# grace-serve itself (S11.2.i): the server's I/O, in io/, beside the pure core and linking it: its sockets and two
# threads (one non-blocking I/O thread over WSAPoll or poll(2), one worker for disk reads), the web files graceui.toml
# lists, the session cookie and the ticket, and the bridge's routing to each space (§7.2, §7.6, §6, D81). Only
# io/platform_*.cpp include the system's headers for sockets; graceui.dll links this (S11.2.b), and so do grace-dev
# (S11.2.j) and EldenNet's launcher (G29 (a)).
add_library(grace_serve STATIC
  io/bridge.cpp
  io/files.cpp
  io/server.cpp
  io/space.cpp
  io/upload.cpp)
if(WIN32)
  target_sources(grace_serve PRIVATE io/platform_win32.cpp)
  target_link_libraries(grace_serve PRIVATE ws2_32 bcrypt)
else()
  find_package(Threads REQUIRED)
  target_sources(grace_serve PRIVATE io/platform_posix.cpp)
  target_link_libraries(grace_serve PUBLIC Threads::Threads)
endif()
target_include_directories(grace_serve PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(grace_serve PUBLIC grace_serve_core grace_protocol)
gu_own_code(grace_serve)
gu_dll_module(grace_serve)

if(GRACEUI_UNIT_TESTS)
  add_executable(grace_serve_tests
    tests/main.cpp
    tests/chunking_tests.cpp
    tests/checks_tests.cpp
    tests/codec_tests.cpp
    tests/frames_tests.cpp
    tests/handshake_tests.cpp
    tests/head_tests.cpp
    tests/pairing_tests.cpp
    tests/response_tests.cpp
    # grace-abi's counting operator new, for the parser's and the decoder's no-allocation tests.
    ${CMAKE_SOURCE_DIR}/src/grace-abi/tests/alloc_counter.cpp)
  target_include_directories(grace_serve_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/grace-abi/tests)
  target_link_libraries(grace_serve_tests PRIVATE grace_serve_core gu_doctest)
  gu_own_code(grace_serve_tests)
  add_test(NAME grace_serve_tests COMMAND grace_serve_tests)
endif()

# The server security suite (§9.1) and grace-serve's I/O tests, over real sockets on loopback with two spaces (GraceUI's
# own and the demo space, from grace-pages, answered from fixtures/demo/topics.json): on Linux under CTest, and in the
# Windows test builds (-DGRACEUI_TEST_EXPORTS=ON) as grace_serve_io_tests.exe, which BUILD.md runs under Wine.
if(GRACEUI_UNIT_TESTS OR (WIN32 AND GRACEUI_TEST_EXPORTS))
  add_executable(grace_serve_io_tests
    io/tests/main.cpp
    io/tests/bridge_tests.cpp
    io/tests/client.cpp
    io/tests/harness.cpp
    io/tests/http_tests.cpp
    io/tests/lifecycle_tests.cpp
    io/tests/network_tests.cpp
    io/tests/upload_tests.cpp)
  target_link_libraries(grace_serve_io_tests PRIVATE grace_serve grace_pages gu_doctest)
  if(WIN32)
    target_link_libraries(grace_serve_io_tests PRIVATE ws2_32)
  endif()
  target_compile_definitions(grace_serve_io_tests PRIVATE GRACE_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures")
  gu_own_code(grace_serve_io_tests)
  if(GRACEUI_UNIT_TESTS)
    add_test(NAME grace_serve_io_tests COMMAND grace_serve_io_tests)
  endif()
endif()

# The head parser and the frame decoder under libFuzzer (§9.1, "Server code"; SRV §5.4): Linux with Clang only,
# -DGRACEUI_FUZZ=ON, with AddressSanitizer and UndefinedBehaviorSanitizer (BUILD.md runs them). Each feeds its input
# whole, a byte at a time and at splits the input itself chooses, and traps unless the three agree. Their seeds are in
# fuzz/corpus/, with a dictionary for the head. The frame decoder's target is grace_ws_frame_fuzz, since grace-protocol's
# frame parser has grace_frame_fuzz.
if(GRACEUI_FUZZ)
  if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GRACEUI_FUZZ is for Clang on Linux")
  endif()
  target_compile_options(grace_serve_core PRIVATE -fsanitize=fuzzer-no-link)
  foreach(fuzzer IN ITEMS head frame)
    set(target grace_${fuzzer}_fuzz)
    if(fuzzer STREQUAL "frame")
      set(target grace_ws_frame_fuzz)
    endif()
    add_executable(${target} fuzz/${fuzzer}_fuzz.cpp)
    target_link_libraries(${target} PRIVATE grace_serve_core)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    gu_own_code(${target})
  endforeach()
endif()
