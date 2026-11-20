/* hooks_vtable.c: the addresses of the COM functions GraceUI hooks (D102), read from the vtables of short-lived objects
 * of its own, all released before any hook exists: for the render half a device, queue and swap chain on a hidden
 * window; for the input half a DirectInput keyboard and mouse. Written in C so that each entry is read by its name from
 * the SDK's C interfaces (lpVtbl->Present) rather than by an index. Every swap chain and queue of the process shares
 * these functions with the ones made here: the device is the adapter's one device, the game's own if it has made it
 * (Direct3D 12 hands out one per adapter); and every DirectInput device of a kind shares its class's vtable.
 *
 * graceui.dll imports no Direct3D DLL (D170): CreateDXGIFactory1, D3D12CreateDevice and D3DCompile are bound here, on
 * the init thread once the game's window exists, from the modules the game has loaded or else from System32 alone.
 * Under Wine, loading dxgi.dll before the game's first window, while other threads make USER calls, can hang in the
 * loader (EldenNet's part 12, 2026-11-20): a mod that loads graceui.dll early no longer loads them with it.
 *
 * SAFETY: every interface pointer here is created and released in its function, on this thread; the casts turn the
 * vtables' entries, which are functions, into the one function type the caller stores until it hands them to MinHook,
 * which never calls them through that type, and DirectInput8Create back from that type into its own; and they turn
 * GetProcAddress's answers into the three functions' own types, each as its SDK header declares it.
 */
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <initguid.h> /* the D3D12, DXGI and DirectInput ids are defined here, so nothing links dxguid */
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dinput.h>
#include <dxgi1_4.h>

#include "hooks_vtable.h"

extern IMAGE_DOS_HEADER __ImageBase; /* graceui.dll's own module, for its window class */

/* ---- The Direct3D functions, bound at run time (D170). Written once by the init thread before the hooks exist, read
 * after: the hooks' installation publishes them. */
typedef HRESULT(WINAPI* create_factory_fn)(REFIID, void**);
typedef HRESULT(WINAPI* create_device_fn)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(WINAPI* compile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                    UINT, UINT, ID3DBlob**, ID3DBlob**);
static create_factory_fn g_create_factory = NULL;
static create_device_fn g_create_device = NULL;
static compile_fn g_compile = NULL;

/* A system module: the one already loaded (the game's), or else System32's, never one from the game's folder. */
static HMODULE system_module(const wchar_t* name) {
  HMODULE module = GetModuleHandleW(name);
  return module != NULL ? module : LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

/* GetProcAddress's answer, copied into a pointer of the function's own type (`out`, of `size` bytes). */
static int bind(HMODULE module, const char* name, void* out, size_t size) {
  const FARPROC found = module != NULL ? GetProcAddress(module, name) : NULL;
  if (found == NULL || size != sizeof found) {
    return 0;
  }
  CopyMemory(out, &found, sizeof found); /* SAFETY: the function's own type, as its SDK header declares it */
  return 1;
}

int grace_host_bind_d3d(void) {
  const int factory = bind(system_module(L"dxgi.dll"), "CreateDXGIFactory1", &g_create_factory, sizeof g_create_factory);
  const int device = bind(system_module(L"d3d12.dll"), "D3D12CreateDevice", &g_create_device, sizeof g_create_device);
  const int compile = bind(system_module(L"d3dcompiler_47.dll"), "D3DCompile", &g_compile, sizeof g_compile);
  return factory && device && compile;
}

/* Dear ImGui's DX12 backend calls CreateDXGIFactory1 at its init (warm-up): as D3DCompile below, a stand-in. */
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory) {
  if (g_create_factory == NULL) {
    return E_FAIL;
  }
  return g_create_factory(riid, factory);
}

/* Dear ImGui's DX12 backend calls D3DCompile when it makes its shaders (at warm-up): this definition stands in for the
 * import, unchanged in the backend (D28), and calls the function the init thread bound; never loading anything. */
HRESULT WINAPI D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO* defines,
                          ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2,
                          ID3DBlob** code, ID3DBlob** errors) {
  if (g_compile == NULL) {
    return E_FAIL;
  }
  return g_compile(data, size, name, defines, include, entry, target, flags1, flags2, code, errors);
}

static const wchar_t kWindowClass[] = L"GraceUI.HookProbe";

int grace_host_read_hook_targets(grace_host_fn targets[GRACE_HOST_RENDER_HOOKS]) {
  HINSTANCE module = (HINSTANCE)&__ImageBase; /* SAFETY: the linker's symbol for this module's base */
  WNDCLASSEXW window_class;
  HWND window = NULL;
  IDXGIFactory2* factory = NULL;
  ID3D12Device* device = NULL;
  ID3D12CommandQueue* queue = NULL;
  IDXGISwapChain1* chain1 = NULL;
  IDXGISwapChain3* chain3 = NULL;
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  DXGI_SWAP_CHAIN_DESC1 chain_desc;
  int found = 0;

  ZeroMemory(&window_class, sizeof window_class);
  window_class.cbSize = sizeof window_class;
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = module;
  window_class.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&window_class)) {
    return 0;
  }
  /* Never shown: no WS_VISIBLE, and never ShowWindow. */
  window = CreateWindowExW(0, kWindowClass, L"", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, module, NULL);
  if (window == NULL) {
    goto done;
  }
  if (g_create_factory == NULL || g_create_device == NULL ||
      FAILED(g_create_factory(&IID_IDXGIFactory2, (void**)&factory))) {
    goto done;
  }
  if (FAILED(g_create_device(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&device))) {
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
  if (FAILED(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)queue, window, &chain_desc, NULL, NULL,
                                                  &chain1))) {
    goto done;
  }
  if (FAILED(IDXGISwapChain1_QueryInterface(chain1, &IID_IDXGISwapChain3, (void**)&chain3))) {
    goto done;
  }
  /* SAFETY: each entry is a function of the implementation's; stored as a function pointer of one type, never called. */
  targets[GRACE_HOST_HOOK_PRESENT] = (grace_host_fn)chain3->lpVtbl->Present;
  targets[GRACE_HOST_HOOK_PRESENT1] = (grace_host_fn)chain3->lpVtbl->Present1;
  targets[GRACE_HOST_HOOK_RESIZE_BUFFERS] = (grace_host_fn)chain3->lpVtbl->ResizeBuffers;
  targets[GRACE_HOST_HOOK_RESIZE_BUFFERS1] = (grace_host_fn)chain3->lpVtbl->ResizeBuffers1;
  targets[GRACE_HOST_HOOK_EXECUTE_COMMAND_LISTS] = (grace_host_fn)queue->lpVtbl->ExecuteCommandLists;
  found = 1;

done:
  if (chain3 != NULL) {
    IDXGISwapChain3_Release(chain3);
  }
  if (chain1 != NULL) {
    IDXGISwapChain1_Release(chain1);
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
  return found;
}

typedef HRESULT(WINAPI* direct_input8_create_fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

int grace_host_read_dinput_targets(grace_host_fn create, grace_host_fn targets[GRACE_HOST_DINPUT_TARGETS]) {
  HINSTANCE module = (HINSTANCE)&__ImageBase; /* SAFETY: the linker's symbol for this module's base */
  direct_input8_create_fn create_input = (direct_input8_create_fn)create; /* SAFETY: DirectInput8Create, by name */
  IDirectInput8W* input = NULL;
  IDirectInputDevice8W* keyboard = NULL;
  IDirectInputDevice8W* mouse = NULL;
  int found = 0;

  targets[0] = NULL;
  targets[1] = NULL;
  if (create_input == NULL ||
      FAILED(create_input(module, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void**)&input, NULL))) {
    goto done;
  }
  if (FAILED(IDirectInput8_CreateDevice(input, &GUID_SysKeyboard, &keyboard, NULL))) {
    goto done;
  }
  /* SAFETY: each entry is a function of the implementation's; stored as a function pointer of one type, never called. */
  targets[0] = (grace_host_fn)keyboard->lpVtbl->GetDeviceState;
  found = 1;
  if (SUCCEEDED(IDirectInput8_CreateDevice(input, &GUID_SysMouse, &mouse, NULL))) {
    grace_host_fn mouse_target = (grace_host_fn)mouse->lpVtbl->GetDeviceState;
    if (mouse_target != targets[0]) {
      targets[1] = mouse_target;
    }
  }

done:
  if (mouse != NULL) {
    IDirectInputDevice8_Release(mouse);
  }
  if (keyboard != NULL) {
    IDirectInputDevice8_Release(keyboard);
  }
  if (input != NULL) {
    IDirectInput8_Release(input);
  }
  return found;
}
