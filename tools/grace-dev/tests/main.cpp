// grace-dev's tests (D168): the fake host over real sockets on loopback with grace-serve's own test client, its spaces'
// and scenarios' checks, and the --dump golden; run by CTest on Linux and by the Windows test builds under Wine.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
