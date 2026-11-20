/* SPDX-License-Identifier: MIT
 * standin.c: grace-standin.dll, a stand-in for another overlay (D175), of our own, in the test tree only and never
 * released. The in-game spike S0-E asks whether GraceUI's hooks live beside another overlay's; when no other overlay is
 * there to ask (the Steam overlay may not load when EldenNet's launcher starts the game), this one is. It patches the
 * swap chain's Present with its own code, by the vtable slot rather than the function's first bytes, and with no hook
 * library (one MinHook in the process, invariant 16, holds even for a stand-in): its Present counts each call and calls
 * the one it replaced. Copied from no overlay.
 *
 * It finds the slot as GraceUI does (hooks_vtable.c), from a short-lived swap chain of its own on a window never shown,
 * and binds Direct3D only once the game has loaded it (D170: it loads nothing itself). Whichever of it and GraceUI
 * comes first, both run: GraceUI's detour sits on whatever the slot named when GraceUI read it.
 *
 * grace-spike.dll reads GraceStandIn_Presents and GraceStandIn_Patched. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h> /* the D3D12 and DXGI ids are defined here, so nothing links dxguid */
#include <d3d12.h>
#include <dxgi1_4.h>

extern IMAGE_DOS_HEADER __ImageBase;

typedef HRESULT(WINAPI* create_factory_fn)(REFIID, void**);
typedef HRESULT(WINAPI* create_device_fn)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE* present_fn)(IDXGISwapChain*, UINT, UINT);

static present_fn g_original = NULL;
static volatile LONG64 g_presents = 0;
static volatile LONG g_patched = 0; /* 1 once patched, 2 if it gave up */
static const wchar_t kWindowClass[] = L"GraceStandInDummy";

static HRESULT STDMETHODCALLTYPE standin_present(IDXGISwapChain* chain, UINT sync, UINT flags) {
  InterlockedIncrement64(&g_presents);
  return g_original(chain, sync, flags);
}

__declspec(dllexport) LONG64 GraceStandIn_Presents(void) { return InterlockedCompareExchange64(&g_presents, 0, 0); }
__declspec(dllexport) LONG GraceStandIn_Patched(void) { return InterlockedCompareExchange(&g_patched, 0, 0); }

/* GetProcAddress's answer, copied into a pointer of the function's own type. */
static int bind(HMODULE module, const char* name, void* out, size_t size) {
  const FARPROC found = module != NULL ? GetProcAddress(module, name) : NULL;
  if (found == NULL || size != sizeof found) {
    return 0;
  }
  CopyMemory(out, &found, sizeof found); /* SAFETY: the function's own type, as its SDK header declares it */
  return 1;
}

/* The swap chain's Present slot, from a swap chain of its own; NULL if one cannot be made. */
static void** present_slot(create_factory_fn create_factory, create_device_fn create_device) {
  HINSTANCE module = (HINSTANCE)&__ImageBase; /* SAFETY: the linker's symbol for this module's base */
  WNDCLASSEXW window_class;
  HWND window = NULL;
  IDXGIFactory2* factory = NULL;
  ID3D12Device* device = NULL;
  ID3D12CommandQueue* queue = NULL;
  IDXGISwapChain1* chain = NULL;
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  DXGI_SWAP_CHAIN_DESC1 chain_desc;
  void** slot = NULL;

  ZeroMemory(&window_class, sizeof window_class);
  window_class.cbSize = sizeof window_class;
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = module;
  window_class.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&window_class)) {
    return NULL;
  }
  window = CreateWindowExW(0, kWindowClass, L"", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, module, NULL);
  if (window == NULL || FAILED(create_factory(&IID_IDXGIFactory2, (void**)&factory)) ||
      FAILED(create_device(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&device))) {
    goto done;
  }
  ZeroMemory(&queue_desc, sizeof queue_desc);
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&queue))) {
    goto done;
  }
  ZeroMemory(&chain_desc, sizeof chain_desc);
  chain_desc.Width = 64;
  chain_desc.Height = 64;
  chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  chain_desc.SampleDesc.Count = 1;
  chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  chain_desc.BufferCount = 2;
  chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (FAILED(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)queue, window, &chain_desc, NULL, NULL, &chain))) {
    goto done;
  }
  /* SAFETY: the implementation's vtable, shared by its swap chains, which outlives this one; only its Present slot's
   * address is kept. */
  slot = (void**)&chain->lpVtbl->Present;

done:
  if (chain != NULL) {
    IDXGISwapChain1_Release(chain);
  }
  if (queue != NULL) {
    ID3D12CommandQueue_Release(queue);
  }
  if (device != NULL) {
    ID3D12Device_Release(device);
  }
  if (factory != NULL) {
    IDXGIFactory2_Release(factory);
  }
  if (window != NULL) {
    DestroyWindow(window);
  }
  UnregisterClassW(kWindowClass, module);
  return slot;
}

static DWORD WINAPI patch_worker(LPVOID unused) {
  create_factory_fn create_factory = NULL;
  create_device_fn create_device = NULL;
  void** slot = NULL;
  DWORD old = 0;
  int i;
  (void)unused;
  /* Until the game has loaded both (at most ten minutes): nothing is loaded from here. */
  for (i = 0; i < 12000; ++i) {
    if (GetModuleHandleW(L"dxgi.dll") != NULL && GetModuleHandleW(L"d3d12.dll") != NULL) {
      break;
    }
    Sleep(50);
  }
  if (!bind(GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory1", &create_factory, sizeof create_factory) ||
      !bind(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateDevice", &create_device, sizeof create_device) ||
      (slot = present_slot(create_factory, create_device)) == NULL ||
      !VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
    InterlockedExchange(&g_patched, 2);
    return 0;
  }
  /* SAFETY: the slot holds a Present of IDXGISwapChain's type; ours has the same type. Never undone. */
  CopyMemory(&g_original, slot, sizeof g_original);
  {
    present_fn ours = standin_present;
    void* replacement = NULL;
    CopyMemory(&replacement, &ours, sizeof replacement);
    InterlockedExchangePointer(slot, replacement);
  }
  VirtualProtect(slot, sizeof *slot, old, &old);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof *slot);
  InterlockedExchange(&g_patched, 1);
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    HANDLE thread;
    DisableThreadLibraryCalls(module);
    thread = CreateThread(NULL, 0, patch_worker, NULL, 0, NULL); /* runs once the loader is done */
    if (thread != NULL) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
