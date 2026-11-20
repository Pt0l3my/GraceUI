// The server security suite and grace-serve's I/O tests (§9.1): doctest over real sockets on loopback, run by CTest on
// Linux with the sanitizers BUILD.md names, and by the Windows test builds under Wine.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
