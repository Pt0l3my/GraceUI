// dllmain.cpp: graceui.dll does nothing at load or at detach (D71, §2.9): no hook, no thread, no allocation under the
// loader lock. The host starts on the first GraceUI_RequestApi, called from a mod's own thread (D70).
#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
