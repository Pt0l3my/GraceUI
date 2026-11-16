/* shield.c: grace-harness-shield.dll, a stand-in for an anti-cheat's module in the harness's anti-cheat scenario: an
 * empty DLL of our own, whose file name the harness tells a test build of GraceUI's anti-cheat check to look for
 * (GraceUI_Test_SetAntiCheatNames), so that a hit is simulated with no anti-cheat software present (D133). It does
 * nothing. */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
  (void)module;
  (void)reason;
  (void)reserved;
  return TRUE;
}
