// grace-spaces' tests (D169): doctest over real sockets on loopback, through the server suite's client, run by CTest on
// Linux and by the Windows test builds under Wine.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
