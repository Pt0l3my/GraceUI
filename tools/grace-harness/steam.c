/* steam.c: grace-harness-steam.dll, the harness's stand-in for Steam's library in the pad scenario (D275): an empty DLL
 * of our own that exports the flat API's names GraceUI looks for when a text field is edited from a pad, and records each
 * call, so that a test build of GraceUI, told this module's name (GraceUI_Test_SetSteamModule), opens and closes its
 * "keyboard" with no Steam present. It is named as nothing of Steam's, and is never shipped. Two accessors, v010 and
 * v011, each with its own object, so that the check sees GraceUI take the newest one the library exports. */
#include <windows.h>

#include <stdbool.h>

static int g_utils_v010;
static int g_utils_v011;
static volatile LONG g_shown;
static volatile LONG g_dismissed;
static volatile LONG g_mode = -1;
static volatile LONG g_x;
static volatile LONG g_y;
static volatile LONG g_w;
static volatile LONG g_h;
static volatile LONG g_newest = 1;  /* every call was on the v011 object */

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
  (void)module;
  (void)reason;
  (void)reserved;
  return TRUE;
}

__declspec(dllexport) void* SteamAPI_SteamUtils_v010(void) { return &g_utils_v010; }

__declspec(dllexport) void* SteamAPI_SteamUtils_v011(void) { return &g_utils_v011; }

__declspec(dllexport) bool SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput(void* utils, int mode, int x, int y, int w,
                                                                            int h) {
  if (utils != &g_utils_v011) {
    InterlockedExchange(&g_newest, 0);
  }
  InterlockedExchange(&g_mode, mode);
  InterlockedExchange(&g_x, x);
  InterlockedExchange(&g_y, y);
  InterlockedExchange(&g_w, w);
  InterlockedExchange(&g_h, h);
  InterlockedIncrement(&g_shown);
  return true;
}

__declspec(dllexport) bool SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput(void* utils) {
  if (utils != &g_utils_v011) {
    InterlockedExchange(&g_newest, 0);
  }
  InterlockedIncrement(&g_dismissed);
  return true;
}

/* What GraceUI asked for so far: [shown, dismissed, mode, x, y, w, h, newest]. */
__declspec(dllexport) void GraceHarnessSteam_Calls(LONG out[8]) {
  out[0] = InterlockedCompareExchange(&g_shown, 0, 0);
  out[1] = InterlockedCompareExchange(&g_dismissed, 0, 0);
  out[2] = InterlockedCompareExchange(&g_mode, 0, 0);
  out[3] = InterlockedCompareExchange(&g_x, 0, 0);
  out[4] = InterlockedCompareExchange(&g_y, 0, 0);
  out[5] = InterlockedCompareExchange(&g_w, 0, 0);
  out[6] = InterlockedCompareExchange(&g_h, 0, 0);
  out[7] = InterlockedCompareExchange(&g_newest, 0, 0);
}
