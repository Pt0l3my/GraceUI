// grace-harness: graceui.dll without a game (§2.2, D85). A Direct3D 12 window program that plays the game's part: it
// presents frames of known clear colours, reads the keyboard and mouse through DirectInput and pads through XInput,
// loads a test build of graceui.dll, calls GraceUI_RequestApi(1), reads its frames back to check what GraceUI drew on
// them, and drives the input half through injected input, posted messages and the test exports. Every check prints PASS
// or FAIL with what it saw, and the exit code is 0 only if all passed. BUILD.md runs it under Wine on Xvfb.
//
//   grace-harness.exe [--dll <path to graceui.dll>] [--scenario main|two-queues|input|input-ansi]
//
// With no scenario it runs "main", then itself again in a new process for each of the others: "two-queues" needs a
// process in which the host has not found a present queue yet, "input" one whose XInput DLLs are loaded before GraceUI
// starts, and "input-ansi" one whose game window is an ANSI window.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>

#include <d3d12.h>
#include <dinput.h>
#include <dxgi1_4.h>
#include <imm.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "graceui_test.h"

namespace {

// ---- Results

int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char* name, const char* format, ...) {
  char details[600];
  va_list args;
  va_start(args, format);
  std::vsnprintf(details, sizeof details, format, args);
  va_end(args);
  std::printf("%s %s: %s\n", ok ? "PASS" : "FAIL", name, details);
  std::fflush(stdout);
  ++(ok ? g_passed : g_failed);
}

void note(const char* format, ...) {
  char text[600];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof text, format, args);
  va_end(args);
  std::printf("     %s\n", text);
  std::fflush(stdout);
}

[[noreturn]] void fatal(const char* what) {
  std::printf("FAIL harness: %s\n", what);
  std::fflush(stdout);
  std::exit(2);
}

void require(HRESULT result, const char* what) {
  if (FAILED(result)) {
    char text[200];
    std::snprintf(text, sizeof text, "%s (0x%08lx)", what, static_cast<unsigned long>(result));
    fatal(text);
  }
}

template <class T>
void release(T*& object) {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
  }
}

ULONG references(IUnknown* object) {
  object->AddRef();
  return object->Release();
}

// ---- Colours: a different clear colour for every check, so that a stale frame can never pass for a new one.

struct Colour {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

constexpr Colour kBaseline{40, 60, 90};
constexpr Colour kUntouched{12, 120, 70};
constexpr Colour kPresent{90, 20, 30};
constexpr Colour kPresent1{30, 90, 20};
constexpr Colour kResize{20, 30, 90};
constexpr Colour kResize1{70, 70, 10};
constexpr Colour kSecondCopy{10, 70, 70};
constexpr Colour kRecreated{100, 50, 0};
constexpr Colour kPanel{GRACEUI_TEST_PANEL_R, GRACEUI_TEST_PANEL_G, GRACEUI_TEST_PANEL_B};

std::uint32_t packed(Colour c) {
  return std::uint32_t{c.r} | (std::uint32_t{c.g} << 8) | (std::uint32_t{c.b} << 16) | 0xFF000000u;
}

int channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xFFu); }

bool close_to(std::uint32_t pixel, Colour c, int tolerance) {
  return std::abs(channel(pixel, 0) - c.r) <= tolerance && std::abs(channel(pixel, 8) - c.g) <= tolerance &&
         std::abs(channel(pixel, 16) - c.b) <= tolerance;
}

struct Frame {
  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint32_t> pixels;
  std::uint32_t at(UINT x, UINT y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

std::string describe(std::uint32_t pixel) {
  char text[48];
  std::snprintf(text, sizeof text, "%d,%d,%d,%d", channel(pixel, 0), channel(pixel, 8), channel(pixel, 16),
                channel(pixel, 24));
  return text;
}

bool in_panel(UINT x, UINT y) {
  return x >= GRACEUI_TEST_PANEL_X0 && x < GRACEUI_TEST_PANEL_X1 && y >= GRACEUI_TEST_PANEL_Y0 &&
         y < GRACEUI_TEST_PANEL_Y1;
}

// Every pixel is the clear colour.
bool untouched(const Frame& frame, Colour clear, std::string& why) {
  const std::uint32_t want = packed(clear);
  for (UINT y = 0; y < frame.height; ++y) {
    for (UINT x = 0; x < frame.width; ++x) {
      if (frame.at(x, y) != want) {
        char text[120];
        std::snprintf(text, sizeof text, "pixel (%u,%u) is %s, not the clear colour %s", x, y,
                      describe(frame.at(x, y)).c_str(), describe(want).c_str());
        why = text;
        return false;
      }
    }
  }
  why = "every pixel is the clear colour";
  return true;
}

// The test panel is there, and nothing else: every pixel outside its rectangle is the clear colour; inside it, the
// panel's colour fills most of it and the text's white ink is where the text is.
bool panel_drawn(const Frame& frame, Colour clear, std::string& why) {
  const std::uint32_t want = packed(clear);
  int panel_pixels = 0;
  int ink_pixels = 0;
  int other_pixels = 0;
  for (UINT y = 0; y < frame.height; ++y) {
    for (UINT x = 0; x < frame.width; ++x) {
      const std::uint32_t pixel = frame.at(x, y);
      if (!in_panel(x, y)) {
        if (pixel != want) {
          char text[140];
          std::snprintf(text, sizeof text, "pixel (%u,%u) outside the panel is %s, not the clear colour %s", x, y,
                        describe(pixel).c_str(), describe(want).c_str());
          why = text;
          return false;
        }
        continue;
      }
      if (close_to(pixel, kPanel, 2)) {
        ++panel_pixels;
      } else if (channel(pixel, 0) >= 200 && channel(pixel, 8) >= 200 && channel(pixel, 16) >= 200) {
        ++ink_pixels;
      } else {
        ++other_pixels;
      }
    }
  }
  const int area = (GRACEUI_TEST_PANEL_X1 - GRACEUI_TEST_PANEL_X0) * (GRACEUI_TEST_PANEL_Y1 - GRACEUI_TEST_PANEL_Y0);
  char text[200];
  std::snprintf(text, sizeof text, "%d of %d panel pixels in its colour, %d of white ink, %d others; outside it the "
                "clear colour",
                panel_pixels, area, ink_pixels, other_pixels);
  why = text;
  return panel_pixels >= area * 8 / 10 && ink_pixels >= 20;
}

// ---- The game's part: a window, a device, a queue (two in the two-queues scenario) and a swap chain.

// What reached the game's window procedure: the input half keeps some messages from it (D125).
struct Seen {
  int key_down[256];
  int key_up[256];
  int chars;
  int ime;
  int mouse;
  int kill_focus;
};
Seen g_seen{};

void count(UINT message, WPARAM wparam) {
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: ++g_seen.key_down[wparam & 0xFF]; break;
    case WM_KEYUP:
    case WM_SYSKEYUP: ++g_seen.key_up[wparam & 0xFF]; break;
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_IME_CHAR: ++g_seen.chars; break;
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION: ++g_seen.ime; break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEWHEEL: ++g_seen.mouse; break;
    case WM_KILLFOCUS: ++g_seen.kill_focus; break;
    default: break;
  }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  count(message, wparam);
  return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK window_procedure_ansi(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  count(message, wparam);
  return DefWindowProcA(window, message, wparam, lparam);
}

bool g_ansi = false;  // the input-ansi scenario: the game's window and message loop are ANSI

void pump_messages() {
  MSG message;
  if (g_ansi) {
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageA(&message);
    }
    return;
  }
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

enum class PresentWith { present, present1 };

class Game {
 public:
  explicit Game(bool second_queue) {
    create_window(0);
    require(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    require(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    require(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
    create_objects(second_queue);
    create_swap_chain();
  }

  // A second window, with a swap chain of its own on `shared`'s device and queue.
  explicit Game(Game& shared) : factory_(shared.factory_), device_(shared.device_), queue_(shared.queue_) {
    factory_->AddRef();
    device_->AddRef();
    queue_->AddRef();
    create_window(660);
    create_objects(false);
    create_swap_chain();
  }

  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  void create_window(int x) {
    RECT rect{0, 0, 640, 360};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    if (g_ansi) {
      WNDCLASSEXA window_class{};
      window_class.cbSize = sizeof window_class;
      window_class.lpfnWndProc = &window_procedure_ansi;
      window_class.hInstance = GetModuleHandleW(nullptr);
      window_class.lpszClassName = "GraceHarnessAnsi";
      RegisterClassExA(&window_class);
      window_ = CreateWindowExA(0, "GraceHarnessAnsi", "GraceUI harness", WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 0,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                window_class.hInstance, nullptr);
    } else {
      WNDCLASSEXW window_class{};
      window_class.cbSize = sizeof window_class;
      window_class.lpfnWndProc = &window_procedure;
      window_class.hInstance = GetModuleHandleW(nullptr);
      window_class.lpszClassName = L"GraceHarness";
      RegisterClassExW(&window_class);
      window_ = CreateWindowExW(0, L"GraceHarness", L"GraceUI harness", WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 0,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                window_class.hInstance, nullptr);
    }
    if (window_ == nullptr) {
      fatal("no window");
    }
    pump_messages();
  }

  void create_objects(bool second_queue) {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (second_queue) {
      require(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&second_queue_)), "a second direct queue");
      require(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&second_allocator_)),
              "CreateCommandAllocator");
      require(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator_, nullptr,
                                         IID_PPV_ARGS(&second_list_)),
              "CreateCommandList");
      require(second_list_->Close(), "Close");
      require(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&second_fence_)), "CreateFence");
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = DXGI_MAX_SWAP_CHAIN_BUFFERS;
    require(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)), "CreateDescriptorHeap");
    rtv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (auto& allocator : allocators_) {
      require(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
              "CreateCommandAllocator");
    }
    require(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&readback_allocator_)),
            "CreateCommandAllocator");
    require(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0], nullptr,
                                       IID_PPV_ARGS(&list_)),
            "CreateCommandList");
    require(list_->Close(), "Close");
    require(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = kReadbackBytes;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    require(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)),
            "the readback buffer");
  }

  // Flip-sequential, so that a presented buffer keeps its content for the copy that reads it back.
  void create_swap_chain() {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 640;
    desc.Height = 360;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    IDXGISwapChain1* chain1 = nullptr;
    require(factory_->CreateSwapChainForHwnd(queue_, window_, &desc, nullptr, nullptr, &chain1),
            "CreateSwapChainForHwnd");
    require(chain1->QueryInterface(IID_PPV_ARGS(&swap_chain_)), "IDXGISwapChain3");
    chain1->Release();
    acquire_buffers();
  }

  // Everything the game holds on its swap chain, let go: as a game does before it releases it.
  void destroy_swap_chain() {
    wait_idle();
    release_buffers();
    release(swap_chain_);
  }

  void acquire_buffers() {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    require(swap_chain_->GetDesc1(&desc), "GetDesc1");
    width_ = desc.Width;
    height_ = desc.Height;
    buffer_count_ = desc.BufferCount;
    for (UINT i = 0; i < buffer_count_; ++i) {
      require(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])), "GetBuffer");
      D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
      rtv.ptr += static_cast<SIZE_T>(i) * rtv_increment_;
      device_->CreateRenderTargetView(buffers_[i], nullptr, rtv);
      buffer_fences_[i] = 0;
    }
  }

  void release_buffers() {
    for (auto& buffer : buffers_) {
      release(buffer);
    }
  }

  // The references each buffer has, less the game's own one.
  std::vector<ULONG> buffer_references() const {
    std::vector<ULONG> counts;
    for (UINT i = 0; i < buffer_count_; ++i) {
      counts.push_back(references(buffers_[i]) - 1);
    }
    return counts;
  }

  void wait_for(ID3D12Fence* fence, UINT64 value) {
    if (fence->GetCompletedValue() < value) {
      require(fence->SetEventOnCompletion(value, event_), "SetEventOnCompletion");
      if (WaitForSingleObject(event_, 10000) != WAIT_OBJECT_0) {
        fatal("the GPU did not finish within 10 s");
      }
    }
  }

  void wait_idle() {
    require(queue_->Signal(fence_, ++fence_value_), "Signal");
    wait_for(fence_, fence_value_);
  }

  // One frame of the game's: clear the current back buffer, then present it. Answers the presented buffer's index.
  UINT frame(Colour clear, PresentWith with) {
    const UINT index = swap_chain_->GetCurrentBackBufferIndex();
    wait_for(fence_, buffer_fences_[index]);
    require(allocators_[index]->Reset(), "Reset");
    require(list_->Reset(allocators_[index], nullptr), "Reset");
    transition(buffers_[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * rtv_increment_;
    const float colour[4] = {clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f};
    list_->ClearRenderTargetView(rtv, colour, 0, nullptr);
    transition(buffers_[index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    require(list_->Close(), "Close");
    ID3D12CommandList* lists[] = {list_};
    queue_->ExecuteCommandLists(1, lists);
    if (second_queue_ != nullptr) {
      // The two-queues scenario: a second direct queue executes too, every frame (an empty list).
      wait_for(second_fence_, second_fence_value_);
      require(second_allocator_->Reset(), "Reset");
      require(second_list_->Reset(second_allocator_, nullptr), "Reset");
      require(second_list_->Close(), "Close");
      ID3D12CommandList* second[] = {second_list_};
      second_queue_->ExecuteCommandLists(1, second);
      require(second_queue_->Signal(second_fence_, ++second_fence_value_), "Signal");
    }
    if (with == PresentWith::present) {
      require(swap_chain_->Present(0, 0), "Present");
    } else {
      DXGI_PRESENT_PARAMETERS parameters{};
      require(swap_chain_->Present1(0, 0, &parameters), "Present1");
    }
    require(queue_->Signal(fence_, ++fence_value_), "Signal");
    buffer_fences_[index] = fence_value_;
    pump_messages();
    return index;
  }

  // The presented buffer `index`, copied back once the GPU is done with everything before it on the present queue,
  // GraceUI's drawing included.
  Frame read_back(UINT index) {
    wait_idle();
    D3D12_RESOURCE_DESC desc = buffers_[index]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    if (bytes > kReadbackBytes) {
      fatal("a frame larger than the readback buffer");
    }
    require(readback_allocator_->Reset(), "Reset");
    require(list_->Reset(readback_allocator_, nullptr), "Reset");
    transition(buffers_[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION target{};
    target.pResource = readback_;
    target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    target.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = buffers_[index];
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    list_->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    transition(buffers_[index], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    require(list_->Close(), "Close");
    ID3D12CommandList* lists[] = {list_};
    queue_->ExecuteCommandLists(1, lists);
    wait_idle();
    Frame frame;
    frame.width = static_cast<UINT>(desc.Width);
    frame.height = desc.Height;
    frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);
    void* mapped = nullptr;
    const D3D12_RANGE everything{0, static_cast<SIZE_T>(bytes)};
    require(readback_->Map(0, &everything, &mapped), "Map");
    const unsigned char* rows = static_cast<const unsigned char*>(mapped) + footprint.Offset;
    for (UINT y = 0; y < frame.height; ++y) {
      std::memcpy(&frame.pixels[static_cast<std::size_t>(y) * frame.width],
                  rows + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch, frame.width * 4u);
    }
    const D3D12_RANGE nothing{0, 0};
    readback_->Unmap(0, &nothing);
    return frame;
  }

  Frame frame_and_read(Colour clear, PresentWith with) { return read_back(frame(clear, with)); }

  HRESULT resize(UINT width, UINT height) {
    wait_idle();
    release_buffers();
    const HRESULT result = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    acquire_buffers();
    return result;
  }

  HRESULT resize1(UINT count, UINT width, UINT height) {
    wait_idle();
    release_buffers();
    UINT node_masks[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    IUnknown* queues[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    for (UINT i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; ++i) {
      node_masks[i] = 1;
      queues[i] = queue_;
    }
    const HRESULT result =
        swap_chain_->ResizeBuffers1(count, width, height, DXGI_FORMAT_UNKNOWN, 0, node_masks, queues);
    acquire_buffers();
    return result;
  }

  UINT width() const { return width_; }
  UINT height() const { return height_; }
  HWND window() const { return window_; }

 private:
  static constexpr UINT64 kReadbackBytes = 1024ull * 1024 * 4;

  void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list_->ResourceBarrier(1, &barrier);
  }

  HWND window_ = nullptr;
  IDXGIFactory4* factory_ = nullptr;
  ID3D12Device* device_ = nullptr;
  ID3D12CommandQueue* queue_ = nullptr;
  ID3D12CommandQueue* second_queue_ = nullptr;
  ID3D12CommandAllocator* second_allocator_ = nullptr;
  ID3D12GraphicsCommandList* second_list_ = nullptr;
  ID3D12Fence* second_fence_ = nullptr;
  UINT64 second_fence_value_ = 0;
  IDXGISwapChain3* swap_chain_ = nullptr;
  ID3D12DescriptorHeap* rtv_heap_ = nullptr;
  UINT rtv_increment_ = 0;
  ID3D12Resource* buffers_[DXGI_MAX_SWAP_CHAIN_BUFFERS] = {};
  UINT64 buffer_fences_[DXGI_MAX_SWAP_CHAIN_BUFFERS] = {};
  ID3D12CommandAllocator* allocators_[DXGI_MAX_SWAP_CHAIN_BUFFERS] = {};
  ID3D12CommandAllocator* readback_allocator_ = nullptr;
  ID3D12GraphicsCommandList* list_ = nullptr;
  ID3D12Fence* fence_ = nullptr;
  UINT64 fence_value_ = 0;
  HANDLE event_ = nullptr;
  ID3D12Resource* readback_ = nullptr;
  UINT width_ = 0;
  UINT height_ = 0;
  UINT buffer_count_ = 0;
};

// ---- graceui.dll, found by name through GetProcAddress, as a mod finds it.

template <class T>
T function_of(HMODULE module, const char* name) {
  const FARPROC found = GetProcAddress(module, name);
  if (found == nullptr) {
    return nullptr;
  }
  // The export's own type, from graceui.h or graceui_test.h (through the one function type every cast may pass).
  return reinterpret_cast<T>(reinterpret_cast<void (*)()>(found));
}

struct GraceUi {
  HMODULE module = nullptr;
  decltype(&GraceUI_Version) version = nullptr;
  decltype(&GraceUI_RequestApi) request_api = nullptr;
  decltype(&GraceUI_Test_SetPanel) set_panel = nullptr;
  decltype(&GraceUI_Test_GetState) get_state = nullptr;
  decltype(&GraceUI_Test_ResetCounters) reset_counters = nullptr;
  decltype(&GraceUI_Test_GetFrameCosts) frame_costs = nullptr;
  decltype(&GraceUI_Test_GetLogLine) log_line = nullptr;
  decltype(&GraceUI_Test_GetHookName) hook_name = nullptr;
  decltype(&GraceUI_Test_RequestInput) request_input = nullptr;
  decltype(&GraceUI_Test_ReleaseInput) release_input = nullptr;
  decltype(&GraceUI_Test_SetWorld) set_world = nullptr;
  decltype(&GraceUI_Test_SetCaret) set_caret = nullptr;
  decltype(&GraceUI_Test_SetCodePage) set_code_page = nullptr;
  decltype(&GraceUI_Test_GetInput) get_input = nullptr;
  decltype(&GraceUI_Test_GetChars) get_chars = nullptr;
  decltype(&GraceUI_Test_GetInputCosts) input_costs = nullptr;

  bool load(const std::wstring& path) {
    module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
      return false;
    }
    version = function_of<decltype(version)>(module, "GraceUI_Version");
    request_api = function_of<decltype(request_api)>(module, "GraceUI_RequestApi");
    set_panel = function_of<decltype(set_panel)>(module, "GraceUI_Test_SetPanel");
    get_state = function_of<decltype(get_state)>(module, "GraceUI_Test_GetState");
    reset_counters = function_of<decltype(reset_counters)>(module, "GraceUI_Test_ResetCounters");
    frame_costs = function_of<decltype(frame_costs)>(module, "GraceUI_Test_GetFrameCosts");
    log_line = function_of<decltype(log_line)>(module, "GraceUI_Test_GetLogLine");
    hook_name = function_of<decltype(hook_name)>(module, "GraceUI_Test_GetHookName");
    request_input = function_of<decltype(request_input)>(module, "GraceUI_Test_RequestInput");
    release_input = function_of<decltype(release_input)>(module, "GraceUI_Test_ReleaseInput");
    set_world = function_of<decltype(set_world)>(module, "GraceUI_Test_SetWorld");
    set_caret = function_of<decltype(set_caret)>(module, "GraceUI_Test_SetCaret");
    set_code_page = function_of<decltype(set_code_page)>(module, "GraceUI_Test_SetCodePage");
    get_input = function_of<decltype(get_input)>(module, "GraceUI_Test_GetInput");
    get_chars = function_of<decltype(get_chars)>(module, "GraceUI_Test_GetChars");
    input_costs = function_of<decltype(input_costs)>(module, "GraceUI_Test_GetInputCosts");
    return version != nullptr && request_api != nullptr && set_panel != nullptr && get_state != nullptr &&
           reset_counters != nullptr && frame_costs != nullptr && log_line != nullptr && hook_name != nullptr &&
           request_input != nullptr && release_input != nullptr && set_world != nullptr && set_caret != nullptr &&
           set_code_page != nullptr && get_input != nullptr && get_chars != nullptr && input_costs != nullptr;
  }

  GraceUiTestState state() const {
    GraceUiTestState out{};
    out.size = sizeof out;
    if (get_state(&out) == 0) {
      fatal("GraceUI_Test_GetState refused");
    }
    return out;
  }

  GraceUiTestInput input() const {
    GraceUiTestInput out{};
    out.size = sizeof out;
    if (get_input(&out) == 0) {
      fatal("GraceUI_Test_GetInput refused");
    }
    return out;
  }

  GraceUiTestAnswer request(std::uint32_t owner, std::uint32_t kind, std::uint32_t wants) const {
    GraceUiTestAnswer answer{};
    if (request_input(owner, kind, wants, &answer) == 0) {
      fatal("GraceUI_Test_RequestInput refused");
    }
    return answer;
  }

  std::vector<std::string> hooks() const {
    std::vector<std::string> names;
    char name[128];
    for (std::uint32_t i = 0; hook_name(i, name, sizeof name) > 0; ++i) {
      names.emplace_back(name);
    }
    return names;
  }

  void print_log() const {
    char line[200];
    for (std::uint32_t i = 0; log_line(i, line, sizeof line) > 0; ++i) {
      note("log: %s", line);
    }
  }
};

const char* method_name(std::uint32_t method) {
  switch (method) {
    case GRACEUI_TEST_QUEUE_SWAP_CHAIN: return "the swap chain named it";
    case GRACEUI_TEST_QUEUE_RESIZE1: return "named in ResizeBuffers1";
    case GRACEUI_TEST_QUEUE_PRESENT: return "executed inside the original Present";
    case GRACEUI_TEST_QUEUE_ONLY_DIRECT: return "the device's only direct queue seen executing";
    default: return "not found";
  }
}

// D102's published list: the only names a hook of GraceUI's may carry.
constexpr const char* kPublishedHooks[] = {"IDXGISwapChain::Present", "IDXGISwapChain1::Present1",
                                           "IDXGISwapChain::ResizeBuffers", "IDXGISwapChain3::ResizeBuffers1",
                                           "ID3D12CommandQueue::ExecuteCommandLists", "the game's window procedure",
                                           "IDirectInputDevice8::GetDeviceState", "XInputGetState"};

// Every installed hook is named in D102's list (its name up to " in <DLL>"); answers the list, for the report.
bool hooks_published(const GraceUi& grace, std::string& text) {
  bool ok = true;
  text.clear();
  for (const std::string& name : grace.hooks()) {
    const std::string base = name.substr(0, name.find(" in "));
    bool listed = false;
    for (const char* published : kPublishedHooks) {
      listed = listed || base == published;
    }
    ok = ok && listed;
    text += (text.empty() ? "" : ", ") + name + (listed ? "" : " (NOT PUBLISHED)");
  }
  return ok;
}

// The hooks this process should hold: the render half's five, GetDeviceState where DirectInput is loaded (one function
// for the keyboard and the mouse under Wine), XInputGetState in each of the `xinput` XInput DLLs loaded before the
// start, and the window procedure's filter; all of them in D102's list, installed in one run.
bool hooks_as_expected(const GraceUi& grace, std::uint32_t xinput, std::string& why) {
  const GraceUiTestState state = grace.state();
  const GraceUiTestInput input = grace.input();
  std::string names;
  const bool published = hooks_published(grace, names);
  char text[900];
  std::snprintf(text, sizeof text,
                "%u hooks in %u install run: the render half's %u, GetDeviceState %u, XInputGetState %u (of %u XInput "
                "DLLs loaded), the window procedure %u; %s: %s",
                state.hooks_installed, state.hook_install_runs, input.render_hooks, input.dinput_hooks,
                input.xinput_hooks, xinput, input.window_filter, published ? "all in D102's list" : "NOT all in D102's list",
                names.c_str());
  why = text;
  return published && state.hook_install_runs == 1 && input.render_hooks == 5 && input.dinput_hooks >= 1 &&
         input.dinput_hooks <= 2 && input.xinput_hooks == xinput && input.window_filter == 1 &&
         state.hooks_installed == input.render_hooks + input.dinput_hooks + input.xinput_hooks + input.window_filter;
}

std::wstring folder_of_this_program() {
  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring folder(path, length);
  return folder.substr(0, folder.find_last_of(L"\\/") + 1);
}

// Starts graceui.dll's host as a mod would, and waits (presenting) until its hooks are installed.
void start_host(Game& game, const GraceUi& grace) {
  if (grace.request_api(1) != nullptr) {
    fatal("GraceUI_RequestApi(1) answered a table: GraceUI 0.1 has none yet");
  }
  for (int i = 0; i < 1500; ++i) {  // 30 s
    const GraceUiTestState state = grace.state();
    if (state.host == GRACEUI_TEST_HOST_HOOKED) {
      return;
    }
    if (state.host == GRACEUI_TEST_HOST_FAILED || state.host == GRACEUI_TEST_HOST_IDLE_COPY) {
      grace.print_log();
      fatal("the host did not start");
    }
    game.frame(kBaseline, PresentWith::present);
    Sleep(20);
  }
  grace.print_log();
  fatal("the hooks were not installed within 30 s");
}

struct Spread {
  double median_us = 0;
  double p99_us = 0;
};

Spread spread_of(std::vector<std::uint32_t> ns) {
  Spread spread;
  if (!ns.empty()) {
    std::sort(ns.begin(), ns.end());
    spread.median_us = ns[ns.size() / 2] / 1000.0;
    spread.p99_us = ns[std::min(ns.size() - 1, ns.size() * 99 / 100)] / 1000.0;
  }
  return spread;
}

// The detour's cost per frame: its whole time, the part inside the runtime's ExecuteCommandLists and Signal on
// GraceUI's own submission, and the rest, GraceUI's own work (Dear ImGui's frame, the recording, the bookkeeping).
struct Costs {
  std::uint32_t count = 0;
  Spread total;
  Spread submit;
  Spread own;
};

Costs frame_costs(const GraceUi& grace) {
  std::vector<GraceUiTestFrameCost> samples(4096);
  samples.resize(grace.frame_costs(samples.data(), static_cast<std::uint32_t>(samples.size())));
  std::vector<std::uint32_t> total;
  std::vector<std::uint32_t> submit;
  std::vector<std::uint32_t> own;
  for (const GraceUiTestFrameCost& sample : samples) {
    total.push_back(sample.total_ns);
    submit.push_back(sample.submit_ns);
    own.push_back(sample.total_ns > sample.submit_ns ? sample.total_ns - sample.submit_ns : 0);
  }
  Costs costs;
  costs.count = static_cast<std::uint32_t>(samples.size());
  costs.total = spread_of(total);
  costs.submit = spread_of(submit);
  costs.own = spread_of(own);
  return costs;
}

std::string counts_text(const std::vector<ULONG>& counts) {
  std::string text;
  for (const ULONG count : counts) {
    text += (text.empty() ? "" : ",") + std::to_string(count);
  }
  return text;
}

// ---- The main scenario: one direct queue, as most games have.

void run_main(const std::wstring& dll) {
  Game game(false);
  std::string why;

  // The harness itself, before GraceUI: its frames read back as their clear colour.
  bool ok = untouched(game.frame_and_read(kBaseline, PresentWith::present), kBaseline, why);
  check(ok, "harness", "before GraceUI is loaded, %s", why.c_str());
  const std::vector<ULONG> references_before = game.buffer_references();

  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports (build it with -DGRACEUI_TEST_EXPORTS=ON)");
  }
  const std::uint32_t version = grace.version();
  check(GRACEUI_VERSION_MAJOR(version) == 0 && GRACEUI_VERSION_MINOR(version) == 1, "version",
        "GraceUI_Version answers %u.%u.%u", GRACEUI_VERSION_MAJOR(version), GRACEUI_VERSION_MINOR(version),
        GRACEUI_VERSION_PATCH(version));

  start_host(game, grace);
  GraceUiTestState state = grace.state();
  std::string hooks_why;
  ok = hooks_as_expected(grace, 0, hooks_why);
  const std::uint32_t hooks = state.hooks_installed;
  check(ok && state.init_threads_started == 1, "start",
        "GraceUI_RequestApi(1) answered null and started %u init thread, which installed %s",
        state.init_threads_started, hooks_why.c_str());

  // 1. Nothing to draw: every frame passes through untouched, and the host still binds and warms up.
  Frame frame;
  for (int i = 0; i < 20; ++i) {
    frame = game.frame_and_read(kUntouched, PresentWith::present);
  }
  ok = untouched(frame, kUntouched, why);
  state = grace.state();
  check(ok && state.bound == 1 && state.frames_drawn == 0, "untouched",
        "with the test panel off, %s; bound %u, %llu frames drawn", why.c_str(), state.bound,
        static_cast<unsigned long long>(state.frames_drawn));
  check(state.queue_method != GRACEUI_TEST_QUEUE_NONE && state.renderer_ready == 1 && state.atlas_ready == 1,
        "warm-up", "present queue: %s; renderer ready %u; atlas %ux%u uploaded %u",
        method_name(state.queue_method), state.renderer_ready, state.atlas_width, state.atlas_height,
        state.atlas_ready);

  // 2. The test panel through Present.
  grace.set_panel(1);
  int frames_until_seen = 0;
  ok = false;
  for (int i = 1; i <= 60 && !ok; ++i) {
    frame = game.frame_and_read(kPresent, PresentWith::present);
    ok = panel_drawn(frame, kPresent, why);
    frames_until_seen = i;
  }
  check(ok, "Present", "the test panel drawn on the game's frame, in frame %d: %s", frames_until_seen, why.c_str());

  // 3. The test panel through Present1.
  const std::uint64_t drawn_before = grace.state().frames_drawn;
  for (int i = 0; i < 5; ++i) {
    frame = game.frame_and_read(kPresent1, PresentWith::present1);
  }
  ok = panel_drawn(frame, kPresent1, why);
  const std::uint64_t drawn_with_present1 = grace.state().frames_drawn - drawn_before;
  check(ok && drawn_with_present1 == 5, "Present1", "%llu of 5 frames drawn through Present1; %s",
        static_cast<unsigned long long>(drawn_with_present1), why.c_str());

  // 4. GraceUI holds no reference to any back buffer between frames.
  std::vector<ULONG> references_now = game.buffer_references();
  check(references_now == references_before, "references", "each back buffer's references besides the game's own: "
        "%s before GraceUI, %s after it drew", counts_text(references_before).c_str(),
        counts_text(references_now).c_str());

  // 5. ResizeBuffers to another size: it succeeds (it would fail if anyone held a buffer), and GraceUI draws again.
  HRESULT result = game.resize(800, 450);
  std::vector<ULONG> references_resized = game.buffer_references();
  for (int i = 0; i < 5; ++i) {
    frame = game.frame_and_read(kResize, PresentWith::present);
  }
  ok = SUCCEEDED(result) && frame.width == 800 && frame.height == 450 && panel_drawn(frame, kResize, why);
  references_now = game.buffer_references();
  check(ok && references_now == references_resized, "ResizeBuffers",
        "0x%08lx; at %ux%u %s; references %s after the resize, %s after drawing", static_cast<unsigned long>(result),
        frame.width, frame.height, why.c_str(), counts_text(references_resized).c_str(),
        counts_text(references_now).c_str());

  // 6. ResizeBuffers1 to another size and buffer count, naming the present queue for every buffer.
  result = game.resize1(3, 480, 270);
  references_resized = game.buffer_references();
  for (int i = 0; i < 6; ++i) {
    frame = game.frame_and_read(kResize1, PresentWith::present);
  }
  ok = SUCCEEDED(result) && frame.width == 480 && frame.height == 270 && panel_drawn(frame, kResize1, why);
  references_now = game.buffer_references();
  state = grace.state();
  check(ok && references_now == references_resized, "ResizeBuffers1",
        "0x%08lx; at %ux%u with 3 buffers %s; references %s after the resize, %s after drawing; present queue now: %s",
        static_cast<unsigned long>(result), frame.width, frame.height, why.c_str(),
        counts_text(references_resized).c_str(), counts_text(references_now).c_str(),
        method_name(state.queue_method));

  // 7. A second GraceUI_RequestApi starts nothing.
  const void* table = grace.request_api(1);
  state = grace.state();
  check(table == nullptr && state.init_threads_started == 1 && state.hook_install_runs == 1 &&
            state.hooks_installed == hooks,
        "second request", "answered %s; init threads %u, install runs %u, hooks %u",
        table == nullptr ? "null" : "a table", state.init_threads_started, state.hook_install_runs,
        state.hooks_installed);

  // 8. The same DLL loaded again is the same module: nothing new.
  const HMODULE again = LoadLibraryW(dll.c_str());
  const void* table_again = grace.request_api(1);
  state = grace.state();
  check(again == grace.module && table_again == nullptr && state.init_threads_started == 1 &&
            state.hook_install_runs == 1 && state.hooks_installed == hooks,
        "second load", "LoadLibrary of the same file gave %s module; init threads %u, install runs %u, hooks %u",
        again == grace.module ? "the same" : "another", state.init_threads_started, state.hook_install_runs,
        state.hooks_installed);
  if (again != nullptr) {
    FreeLibrary(again);
  }

  // 9. A second copy of GraceUI from another folder idles (D72, invariant 18), and the first still draws once.
  const std::wstring copy_folder = folder_of_this_program() + L"second-copy";
  CreateDirectoryW(copy_folder.c_str(), nullptr);
  const std::wstring copy = copy_folder + L"\\graceui.dll";
  GraceUi second;
  if (!CopyFileW(dll.c_str(), copy.c_str(), FALSE) || !second.load(copy)) {
    fatal("the second copy of graceui.dll did not load");
  }
  const void* second_table = second.request_api(1);
  const GraceUiTestState second_state = second.state();
  for (int i = 0; i < 5; ++i) {
    frame = game.frame_and_read(kSecondCopy, PresentWith::present);
  }
  ok = panel_drawn(frame, kSecondCopy, why);
  state = grace.state();
  check(second.module != grace.module && second_table == nullptr &&
            second_state.host == GRACEUI_TEST_HOST_IDLE_COPY && second_state.init_threads_started == 0 &&
            second_state.hooks_installed == 0 && state.hooks_installed == hooks && state.hook_install_runs == 1 && ok,
        "second copy",
        "another module (%s) answered null, idles (state %u) with %u init threads and %u hooks; the first keeps its %u "
        "hooks; %s",
        second.module != grace.module ? "yes" : "no", second_state.host, second_state.init_threads_started,
        second_state.hooks_installed, state.hooks_installed, why.c_str());
  second.print_log();

  // 10. The detour's own cost per frame and its allocations after warm-up, with the panel drawn on every frame. First
  // as a game runs its frames, without waiting for the GPU between them (reported); then with the GPU idle before
  // each frame (the check): under Wine the GPU is Mesa's software Vulkan on the same four cores, whose threads would
  // otherwise be rasterising earlier frames while the detour runs.
  for (int i = 0; i < 120; ++i) {
    game.frame(kPresent, PresentWith::present);
  }
  grace.reset_counters();
  GraceUiTestState before = grace.state();
  for (int i = 0; i < 600; ++i) {
    game.frame(kPresent, PresentWith::present);
  }
  state = grace.state();
  const Costs pipelined = frame_costs(grace);
  note("as a game runs, over %u frames: GraceUI's own work median %.1f us, p99 %.1f us; the whole detour median %.1f "
       "us, p99 %.1f us, of which the runtime's submission calls median %.1f us, p99 %.1f us; %llu drawn, %llu "
       "skipped with their slot still on the GPU",
       pipelined.count, pipelined.own.median_us, pipelined.own.p99_us, pipelined.total.median_us,
       pipelined.total.p99_us, pipelined.submit.median_us, pipelined.submit.p99_us,
       static_cast<unsigned long long>(state.frames_drawn - before.frames_drawn),
       static_cast<unsigned long long>(state.frames_skipped_busy - before.frames_skipped_busy));
  const std::uint64_t pipelined_allocations = state.allocations;
  const std::uint64_t pipelined_frees = state.frees;

  grace.reset_counters();
  before = grace.state();
  for (int i = 0; i < 600; ++i) {
    game.frame(kPresent, PresentWith::present);
    game.wait_idle();
  }
  state = grace.state();
  const Costs drawing = frame_costs(grace);
  const std::uint64_t drawn = state.frames_drawn - before.frames_drawn;
  check(drawing.count >= 600 && drawn == 600 && drawing.own.p99_us <= 1000.0, "frame cost",
        "drawing the panel, the GPU idle before each frame, over %u frames: GraceUI's own work median %.1f us, p99 "
        "%.1f us (budget: p99 1000 us); the whole detour median %.1f us, p99 %.1f us, of which the runtime's "
        "submission calls median %.1f us, p99 %.1f us; %llu of 600 drawn",
        drawing.count, drawing.own.median_us, drawing.own.p99_us, drawing.total.median_us, drawing.total.p99_us,
        drawing.submit.median_us, drawing.submit.p99_us, static_cast<unsigned long long>(drawn));
  check(pipelined_allocations == 0 && pipelined_frees == 0 && state.allocations == 0 && state.frees == 0,
        "allocations",
        "on the present thread in the detours, after warm-up: %llu allocations and %llu frees over the 600 pipelined "
        "frames, %llu and %llu over the 600 others",
        static_cast<unsigned long long>(pipelined_allocations), static_cast<unsigned long long>(pipelined_frees),
        static_cast<unsigned long long>(state.allocations), static_cast<unsigned long long>(state.frees));

  grace.set_panel(0);
  for (int i = 0; i < 30; ++i) {
    game.frame(kUntouched, PresentWith::present);
  }
  grace.reset_counters();
  for (int i = 0; i < 300; ++i) {
    game.frame(kUntouched, PresentWith::present);
  }
  const Costs idle = frame_costs(grace);
  state = grace.state();
  check(idle.count >= 300 && idle.total.p99_us <= 1000.0 && state.allocations == 0 && state.frees == 0, "idle cost",
        "nothing to draw, over %u frames: the whole detour median %.1f us, p99 %.1f us; %llu allocations, %llu frees",
        idle.count, idle.total.median_us, idle.total.p99_us, static_cast<unsigned long long>(state.allocations),
        static_cast<unsigned long long>(state.frees));

  // 11. The game releases its swap chain and makes a new one on the same window: that succeeds (no reference of
  // GraceUI's keeps the old one), and GraceUI binds the new one and draws on it.
  grace.set_panel(1);
  game.destroy_swap_chain();
  game.create_swap_chain();
  for (int i = 0; i < 10; ++i) {
    frame = game.frame_and_read(kRecreated, PresentWith::present);
  }
  ok = panel_drawn(frame, kRecreated, why);
  state = grace.state();
  check(ok && state.binds == 2 && state.bound == 1, "new swap chain",
        "a new swap chain on the same window: bound (binds %u), %s; present queue: %s", state.binds, why.c_str(),
        method_name(state.queue_method));

  // 12. A second swap chain, on another window, while one is bound: it passes through untouched. Once the bound one
  // ends, the second is bound and drawn on.
  constexpr Colour kSecondChain{60, 0, 100};
  Game second_window(game);
  for (int i = 0; i < 5; ++i) {
    game.frame(kRecreated, PresentWith::present);
    frame = second_window.frame_and_read(kSecondChain, PresentWith::present);
  }
  const bool passed_through = untouched(frame, kSecondChain, why);
  const std::string passed_why = why;
  game.destroy_swap_chain();
  for (int i = 0; i < 10; ++i) {
    frame = second_window.frame_and_read(kSecondChain, PresentWith::present);
  }
  ok = panel_drawn(frame, kSecondChain, why);
  state = grace.state();
  check(passed_through && ok && state.binds == 3 && state.bound == 1, "second swap chain",
        "while the first was bound, the second's frame: %s; once the first ended: bound (binds %u), %s",
        passed_why.c_str(), state.binds, why.c_str());

  state = grace.state();
  check(state.render_off == 0, "render path", "still on after every check; %llu presents, %llu frames drawn",
        static_cast<unsigned long long>(state.presents), static_cast<unsigned long long>(state.frames_drawn));
  grace.print_log();
}

// ---- Two direct queues: the host must not guess which one presents (D116), until the game names it.

void run_two_queues(const std::wstring& dll) {
  Game game(true);
  std::string why;
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  grace.set_panel(1);
  Frame frame;
  for (int i = 0; i < 30; ++i) {
    frame = game.frame_and_read(kUntouched, PresentWith::present);
  }
  bool ok = untouched(frame, kUntouched, why);
  GraceUiTestState state = grace.state();
  check(ok && state.queue_method == GRACEUI_TEST_QUEUE_NONE && state.direct_queues_seen >= 2, "two queues",
        "%u direct queues executing and the panel on: no queue taken (%s), %s", state.direct_queues_seen,
        method_name(state.queue_method), why.c_str());

  const HRESULT result = game.resize1(2, game.width(), game.height());
  for (int i = 0; i < 6; ++i) {
    frame = game.frame_and_read(kPresent, PresentWith::present);
  }
  ok = SUCCEEDED(result) && panel_drawn(frame, kPresent, why);
  state = grace.state();
  check(ok && state.queue_method == GRACEUI_TEST_QUEUE_RESIZE1, "named queue",
        "after ResizeBuffers1 named the present queue (0x%08lx): %s; present queue: %s",
        static_cast<unsigned long>(result), why.c_str(), method_name(state.queue_method));
  grace.print_log();
}

// ---- The input half (D77, D125): a game that reads its keyboard and mouse through DirectInput and its pads through
// XInput, and whose window gets the messages Windows sends it. Injected input (SendInput) reaches DirectInput and the
// window alike; posted and sent messages reach the window only. The test panel stays on, so that Dear ImGui's frames run
// with and without a holder.

constexpr Colour kInputColour{30, 30, 30};
constexpr std::uint32_t kPanelKind = GRACEUI_TEST_KIND_PANEL;
constexpr UINT kScanA = 0x1E;
constexpr UINT kScanK = 0x25;
constexpr UINT kScanEscape = 0x01;
constexpr UINT kScanF5 = 0x3F;

class GameInput {
 public:
  void open(HWND window) {
    require(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                               reinterpret_cast<void**>(&input_), nullptr),
            "DirectInput8Create");
    require(input_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr), "a DirectInput keyboard");
    require(keyboard_->SetDataFormat(&c_dfDIKeyboard), "c_dfDIKeyboard");
    require(keyboard_->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND), "the keyboard's level");
    require(input_->CreateDevice(GUID_SysMouse, &mouse_, nullptr), "a DirectInput mouse");
    require(mouse_->SetDataFormat(&c_dfDIMouse2), "c_dfDIMouse2");
    require(mouse_->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND), "the mouse's level");
    keyboard_->Acquire();
    mouse_->Acquire();
  }

  std::array<std::uint8_t, 256> keys() {
    std::array<std::uint8_t, 256> state{};
    if (FAILED(keyboard_->GetDeviceState(256, state.data()))) {
      keyboard_->Acquire();
      keyboard_->GetDeviceState(256, state.data());
    }
    return state;
  }

  DIMOUSESTATE2 mouse() {
    DIMOUSESTATE2 state{};
    if (FAILED(mouse_->GetDeviceState(sizeof state, &state))) {
      mouse_->Acquire();
      mouse_->GetDeviceState(sizeof state, &state);
    }
    return state;
  }

 private:
  IDirectInput8W* input_ = nullptr;
  IDirectInputDevice8W* keyboard_ = nullptr;
  IDirectInputDevice8W* mouse_ = nullptr;
};

bool none_down(const std::array<std::uint8_t, 256>& keys) {
  return std::all_of(keys.begin(), keys.end(), [](std::uint8_t key) { return key == 0; });
}

// A key as a keyboard sends it, with its virtual key and its scan code: Wine 9.0 leaves a key injected by scan code
// alone without a virtual key in its window messages, and one injected by virtual key alone without a DirectInput code.
void inject_key(UINT vk, UINT scan, bool down) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = static_cast<WORD>(vk);
  input.ki.wScan = static_cast<WORD>(scan);
  input.ki.dwFlags = down ? 0u : static_cast<DWORD>(KEYEVENTF_KEYUP);
  SendInput(1, &input, sizeof input);
}

void inject_mouse(LONG dx, LONG dy, DWORD flags) {
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dx = dx;
  input.mi.dy = dy;
  input.mi.dwFlags = flags;
  SendInput(1, &input, sizeof input);
}

// Wine hands injected input to DirectInput from a thread of its own: a moment for it, and the window its messages.
void settle() {
  Sleep(60);
  pump_messages();
}

// The cursor in the middle of the game window's client area, with the window in front and holding the keyboard focus:
// a click elsewhere would hand the focus to whatever is under the cursor (the desktop, under Xvfb). Wine brings its key
// state into line with the X server's on X focus and pointer-grab events, releasing any key injected before them (the
// X server never saw it), so those events are let pass before any key is injected.
void cursor_into(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  POINT middle{(client.left + client.right) / 2, (client.top + client.bottom) / 2};
  ClientToScreen(window, &middle);
  SetCursorPos(middle.x, middle.y);
  if (GetForegroundWindow() != window) {
    SetForegroundWindow(window);
  }
  if (GetFocus() != window) {
    SetFocus(window);
  }
  Sleep(100);
  settle();
}

// A key message's lParam: the scan code, a repeat count of 1, and whether the key was down before (a repeat, or a
// release).
LPARAM key_lparam(UINT scan, bool was_down, bool releasing) {
  LPARAM value = 1 | (static_cast<LPARAM>(scan) << 16);
  if (was_down) {
    value |= LPARAM{1} << 30;
  }
  if (releasing) {
    value |= LPARAM{1} << 31;
  }
  return value;
}

void post_key(HWND window, UINT vk, UINT scan, bool down, bool repeat) {
  PostMessageW(window, down ? WM_KEYDOWN : WM_KEYUP, vk, key_lparam(scan, down ? repeat : true, !down));
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

// Every pad's answer through every XInput DLL loaded, and the states they filled.
std::vector<DWORD> read_pads(const std::vector<XInputGetStateFn>& pads) {
  std::vector<DWORD> answers;
  for (const XInputGetStateFn get_state : pads) {
    for (DWORD user = 0; user < 4; ++user) {
      XINPUT_STATE state{};
      answers.push_back(get_state(user, &state));
    }
  }
  return answers;
}

void frames(Game& game, int count) {
  for (int i = 0; i < count; ++i) {
    game.frame(kInputColour, PresentWith::present);
  }
}

std::vector<std::uint32_t> chars_of(const GraceUi& grace) {
  std::vector<std::uint32_t> chars(1024);
  chars.resize(grace.get_chars(chars.data(), static_cast<std::uint32_t>(chars.size())));
  return chars;
}

std::string code_points(const std::vector<std::uint32_t>& points) {
  std::string text;
  for (const std::uint32_t point : points) {
    char one[16];
    std::snprintf(one, sizeof one, "%sU+%04X", text.empty() ? "" : " ", point);
    text += one;
  }
  return text;
}

const char* reason_name(std::uint32_t reason) {
  switch (reason) {
    case GRACEUI_TEST_CLOSED_RELEASED: return "released";
    case GRACEUI_TEST_CLOSED_MENU_KEY: return "the menu key";
    case GRACEUI_TEST_CLOSED_DANGER: return "danger";
    case GRACEUI_TEST_CLOSED_LOADING: return "loading";
    case GRACEUI_TEST_CLOSED_FOCUS_LOST: return "focus lost";
    case GRACEUI_TEST_CLOSED_NO_RENDER: return "no render";
    case GRACEUI_TEST_CLOSED_FAULT: return "a fault";
    case GRACEUI_TEST_CLOSED_PLAYER: return "the player's exit";
    default: return "none";
  }
}

// A holder that takes everything; its cursor confinement's X events are let pass before anything is injected.
bool take(const GraceUi& grace, Game& game, std::uint32_t owner, std::uint32_t kind) {
  const GraceUiTestAnswer answer = grace.request(owner, kind, GRACEUI_TEST_WANT_ALL);
  frames(game, 2);
  Sleep(100);
  settle();
  return answer.answer == GRACEUI_TEST_GRANTED;
}

Spread spread_ns(std::vector<std::uint32_t> ns) { return spread_of(std::move(ns)); }

std::vector<std::uint32_t> input_costs_of(const GraceUi& grace, std::uint32_t kind) {
  std::vector<std::uint32_t> ns(4096);
  ns.resize(grace.input_costs(kind, ns.data(), static_cast<std::uint32_t>(ns.size())));
  return ns;
}

void run_input(const std::wstring& dll) {
  Game game(false);
  const HWND window = game.window();
  cursor_into(window);
  pump_messages();
  GameInput reads;
  reads.open(window);
  // Three XInput DLLs, loaded before GraceUI starts: each is hooked, found by name (D69).
  std::vector<XInputGetStateFn> pads;
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    if (const HMODULE module = LoadLibraryW(name)) {
      if (const XInputGetStateFn get_state = function_of<XInputGetStateFn>(module, "XInputGetState")) {
        pads.push_back(get_state);
      }
    }
  }
  const std::vector<DWORD> pads_before = read_pads(pads);

  // The game's own reads, before GraceUI: an injected key and mouse movement reach DirectInput.
  inject_key('A', kScanA, true);
  settle();
  const bool sees_key = (reads.keys()[DIK_A] & 0x80) != 0;
  inject_key('A', kScanA, false);
  settle();
  reads.keys();
  reads.mouse();
  inject_mouse(30, 20, MOUSEEVENTF_MOVE);
  settle();
  const DIMOUSESTATE2 moved = reads.mouse();
  cursor_into(window);
  check(sees_key && moved.lX != 0 && !pads.empty(), "input harness",
        "before GraceUI, DirectInput reads an injected key (%s) and mouse movement (%ld,%ld); %zu XInput DLLs loaded, "
        "pad 0 answering %lu (Wine has no pad)",
        sees_key ? "yes" : "no", moved.lX, moved.lY, pads.size(), pads_before.empty() ? 0ul : pads_before[0]);

  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  grace.set_panel(1);
  frames(game, 30);
  std::string why;
  bool ok = hooks_as_expected(grace, static_cast<std::uint32_t>(pads.size()), why);
  check(ok, "input hooks", "%s", why.c_str());

  // 1. No holder: the game's reads pass untouched, but for the menu key, which never reaches the game (§5.1).
  g_seen = Seen{};
  inject_key('A', kScanA, true);
  settle();
  std::array<std::uint8_t, 256> keys = reads.keys();
  const bool key_passes = (keys[DIK_A] & 0x80) != 0;
  inject_key(VK_F5, kScanF5, true);
  settle();
  keys = reads.keys();
  const bool menu_key_hidden = keys[DIK_F5] == 0 && (keys[DIK_A] & 0x80) != 0;
  inject_key(VK_F5, kScanF5, false);
  inject_key('A', kScanA, false);
  settle();
  const bool released = none_down(reads.keys());
  cursor_into(window);
  reads.mouse();
  inject_mouse(25, 10, MOUSEEVENTF_MOVE);
  settle();
  const DIMOUSESTATE2 move = reads.mouse();
  inject_mouse(0, 0, MOUSEEVENTF_LEFTDOWN);
  settle();
  const DIMOUSESTATE2 button = reads.mouse();
  inject_mouse(0, 0, MOUSEEVENTF_LEFTUP);
  settle();
  reads.mouse();
  const bool pads_untouched = read_pads(pads) == pads_before;
  check(key_passes && menu_key_hidden && released && move.lX != 0 && button.rgbButtons[0] == 0x80 && pads_untouched &&
            g_seen.key_down['A'] == 1 && g_seen.key_down[VK_F5] == 0 && g_seen.key_up[VK_F5] == 0,
        "pass-through",
        "with no holder: A read %s, F5 read %s and its messages kept from the game (%d down, %d up), A's messages "
        "the game's (%d down); mouse moved %ld,%ld and its button read 0x%02x; XInputGetState %s",
        key_passes ? "down" : "UP", menu_key_hidden ? "up" : "DOWN", g_seen.key_down[VK_F5], g_seen.key_up[VK_F5],
        g_seen.key_down['A'], move.lX, move.lY, button.rgbButtons[0],
        pads_untouched ? "answered as before GraceUI" : "ANSWERED OTHERWISE");

  // 2. request_input: granted; refused naming the holder; the holder asking again is told it holds input.
  const GraceUiTestAnswer granted = grace.request(1, kPanelKind, GRACEUI_TEST_WANT_ALL);
  const GraceUiTestAnswer refused = grace.request(2, GRACEUI_TEST_KIND_MODAL, GRACEUI_TEST_WANT_ALL);
  const GraceUiTestAnswer again = grace.request(1, kPanelKind, GRACEUI_TEST_WANT_ALL);
  const bool other_released = grace.release_input(2) != 0;
  Sleep(100);
  settle();
  check(granted.answer == GRACEUI_TEST_GRANTED && refused.answer == GRACEUI_TEST_REFUSED && refused.owner == 1 &&
            refused.kind == kPanelKind && again.answer == GRACEUI_TEST_GRANTED && again.generation == granted.generation &&
            !other_released && grace.input().holder_owner == 1,
        "request_input",
        "answered granted (%u), then refused (%u) naming owner %u, kind %u; the holder asking again: %u, generation "
        "%u; a release by another: %s",
        granted.answer, refused.answer, refused.owner, refused.kind, again.answer, again.generation,
        other_released ? "TAKEN" : "ignored");

  // 3. While the holder has input: the game reads nothing, and the holder's view gets the keys. The button goes down
  // before the key: under Xvfb, Wine releases a key injected before a click (see cursor_into).
  frames(game, 3);
  cursor_into(window);
  g_seen = Seen{};
  reads.mouse();
  inject_mouse(25, 10, MOUSEEVENTF_MOVE);
  inject_mouse(0, 0, MOUSEEVENTF_LEFTDOWN);
  Sleep(100);
  settle();
  const DIMOUSESTATE2 held_mouse = reads.mouse();
  inject_key('A', kScanA, true);
  settle();
  keys = reads.keys();
  const bool a_really_down = (GetAsyncKeyState('A') & 0x8000) != 0;
  const bool keys_neutral = none_down(keys) && a_really_down;
  const bool pads_still = read_pads(pads) == pads_before;
  frames(game, 3);
  GraceUiTestInput input = grace.input();
  check(keys_neutral && held_mouse.lX == 0 && held_mouse.lY == 0 && held_mouse.rgbButtons[0] == 0 && pads_still &&
            g_seen.key_down['A'] == 0 && input.imgui_keys_down >= 1 && input.imgui_draws_cursor == 1,
        "neutral",
        "while a panel holds input, with A (down: %s) and the left button held: the keyboard reads %s, the mouse "
        "%ld,%ld with its button 0x%02x; XInputGetState %s; A's messages kept from the game (%d); Dear ImGui sees %u "
        "key down and draws its own cursor (%u)",
        a_really_down ? "yes" : "NO", none_down(keys) ? "nothing" : "KEYS", held_mouse.lX, held_mouse.lY,
        held_mouse.rgbButtons[0],
        pads_still ? "untouched" : "CHANGED", g_seen.key_down['A'], input.imgui_keys_down, input.imgui_draws_cursor);

  // 4. The closing press (invariant 8): the holder ends with A and the left button still held; the game reads neither
  // until each is released, and never sees the press that began while the holder had input.
  const bool closed = grace.release_input(1) != 0;
  frames(game, 2);
  keys = reads.keys();
  const DIMOUSESTATE2 after_close = reads.mouse();
  const bool held = (GetAsyncKeyState('A') & 0x8000) != 0 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool masked = keys[DIK_A] == 0 && after_close.rgbButtons[0] == 0 && held;
  Sleep(300);
  const bool still_masked = reads.keys()[DIK_A] == 0 && (GetAsyncKeyState('A') & 0x8000) != 0;
  inject_key('A', kScanA, false);
  inject_mouse(0, 0, MOUSEEVENTF_LEFTUP);
  settle();
  reads.keys();
  reads.mouse();
  inject_key('A', kScanA, true);
  inject_mouse(0, 0, MOUSEEVENTF_LEFTDOWN);
  settle();
  keys = reads.keys();
  const DIMOUSESTATE2 pressed_again = reads.mouse();
  const bool games_again = (keys[DIK_A] & 0x80) != 0 && pressed_again.rgbButtons[0] == 0x80;
  inject_key('A', kScanA, false);
  inject_mouse(0, 0, MOUSEEVENTF_LEFTUP);
  settle();
  reads.keys();
  reads.mouse();
  input = grace.input();
  check(closed && masked && still_masked && games_again && g_seen.key_down['A'] == 1 && g_seen.key_up['A'] == 1 &&
            input.last_close_reason == GRACEUI_TEST_CLOSED_RELEASED && input.holder_owner == 0,
        "closing press",
        "released (%s) with A and the button held: read %s, and A still %s 300 ms later; once released and pressed "
        "again, the game's (%s); A's messages reaching the game: %d down and %d up, those of the second press only",
        reason_name(input.last_close_reason), masked ? "up" : "DOWN", still_masked ? "up" : "DOWN",
        games_again ? "yes" : "NO", g_seen.key_down['A'], g_seen.key_up['A']);

  // 5. Characters: posted WM_CHAR units, a pair, a lone high half, a lone low half and swapped halves among them, and
  // WM_UNICHAR, reach Dear ImGui as code points, and the game none of them.
  take(grace, game, 1, kPanelKind);
  grace.reset_counters();
  g_seen = Seen{};
  const std::uint16_t units[] = {'G', 'r', 0xE2, 0xD83D, 0xDE00, 0xD83D, 'x', 0xDE00, 'z', 0xDE00, 0xD83D, 'y'};
  for (const std::uint16_t unit : units) {
    PostMessageW(window, WM_CHAR, unit, 1);
  }
  PostMessageW(window, WM_UNICHAR, 0x1F642, 0);
  const LRESULT takes_unichar = SendMessageW(window, WM_UNICHAR, UNICODE_NOCHAR, 0);
  frames(game, 6);
  const std::vector<std::uint32_t> got = chars_of(grace);
  const std::vector<std::uint32_t> want = {'G', 'r', 0xE2, 0x1F600, 0xFFFD, 'x', 0xFFFD, 'z', 0xFFFD, 0xFFFD, 'y',
                                           0x1F642};
  check(got == want && takes_unichar == TRUE && g_seen.chars == 0, "characters",
        "Dear ImGui received %s (wanted %s); WM_UNICHAR taken (%s); character messages reaching the game: %d",
        code_points(got).c_str(), code_points(want).c_str(), takes_unichar == TRUE ? "yes" : "NO", g_seen.chars);

  // 6. The IME's composition window at the holder's caret, and its messages kept from the game.
  grace.set_caret(120, 80, 18);
  SendMessageW(window, WM_IME_STARTCOMPOSITION, 0, 0);
  COMPOSITIONFORM composition{};
  const HIMC context = ImmGetContext(window);
  const bool read_form = context != nullptr && ImmGetCompositionWindow(context, &composition) != FALSE;
  if (context != nullptr) {
    ImmReleaseContext(window, context);
  }
  SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
  check(read_form && composition.dwStyle == CFS_FORCE_POSITION && composition.ptCurrentPos.x == 120 &&
            composition.ptCurrentPos.y == 80 && g_seen.ime == 0,
        "IME", "the composition window placed at %ld,%ld (style 0x%lx) for a caret at 120,80; IME messages reaching the "
        "game: %d",
        composition.ptCurrentPos.x, composition.ptCurrentPos.y, static_cast<unsigned long>(composition.dwStyle),
        g_seen.ime);

  // 7. The player's exit: Escape held 1 s ends any holder, and neither its press, its repeats nor its release reach
  // the game (its menu would open).
  g_seen = Seen{};
  post_key(window, VK_ESCAPE, kScanEscape, true, false);
  const DWORD escape_start = GetTickCount();
  while (GetTickCount() - escape_start < 1500 && grace.input().holder_owner != 0) {
    post_key(window, VK_ESCAPE, kScanEscape, true, true);
    game.frame(kInputColour, PresentWith::present);
    Sleep(30);
  }
  const DWORD held_for = GetTickCount() - escape_start;
  input = grace.input();
  post_key(window, VK_ESCAPE, kScanEscape, true, true);
  post_key(window, VK_ESCAPE, kScanEscape, false, false);
  frames(game, 2);
  const int escape_messages = g_seen.key_down[VK_ESCAPE] + g_seen.key_up[VK_ESCAPE];
  post_key(window, VK_ESCAPE, kScanEscape, true, false);
  post_key(window, VK_ESCAPE, kScanEscape, false, false);
  frames(game, 2);
  check(input.holder_owner == 0 && input.last_close_reason == GRACEUI_TEST_CLOSED_PLAYER && held_for >= 950 &&
            escape_messages == 0 && g_seen.key_down[VK_ESCAPE] == 1 && g_seen.key_up[VK_ESCAPE] == 1,
        "Escape held",
        "the holder ended (%s) after %lu ms of Escape held; Escape's messages reaching the game meanwhile: %d; the next "
        "press: %d down, %d up",
        reason_name(input.last_close_reason), static_cast<unsigned long>(held_for), escape_messages,
        g_seen.key_down[VK_ESCAPE], g_seen.key_up[VK_ESCAPE]);

  // 8. The menu key: with a panel holding input, the first press says "‹Mod› has the controls" and the second within
  // 2 s is the player's exit; with the menu holding it, one press closes it. The game never sees it.
  take(grace, game, 1, kPanelKind);
  g_seen = Seen{};
  const std::uint32_t notices = grace.input().notices;
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  frames(game, 2);
  input = grace.input();
  const bool warned = input.notices == notices + 1 && input.holder_owner == 1;
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  frames(game, 2);
  const GraceUiTestInput exited = grace.input();
  take(grace, game, 3, GRACEUI_TEST_KIND_MENU);
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  frames(game, 2);
  input = grace.input();
  check(warned && exited.holder_owner == 0 && exited.last_close_reason == GRACEUI_TEST_CLOSED_PLAYER &&
            input.holder_owner == 0 && input.last_close_reason == GRACEUI_TEST_CLOSED_MENU_KEY &&
            g_seen.key_down[VK_F5] == 0 && g_seen.key_up[VK_F5] == 0,
        "menu key",
        "a panel: the first press %s, the second ended it (%s); the menu: one press ended it (%s); F5's messages "
        "reaching the game: %d",
        warned ? "warned" : "DID NOT WARN", reason_name(exited.last_close_reason), reason_name(input.last_close_reason),
        g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5]);

  // 9. The opening press: K held as the holder takes input does nothing in it until released.
  inject_key('K', kScanK, true);
  settle();
  reads.keys();
  take(grace, game, 1, kPanelKind);
  frames(game, 2);
  const std::uint32_t inert = grace.input().imgui_keys_down;
  inject_key('K', kScanK, false);
  settle();
  frames(game, 2);
  inject_key('K', kScanK, true);
  settle();
  frames(game, 3);
  const std::uint32_t fresh = grace.input().imgui_keys_down;
  check(inert == 0 && fresh >= 1, "opening press",
        "K held as the panel took input: Dear ImGui saw %u key down; released and pressed again: %u", inert, fresh);

  // 10. Focus loss ends the holder and releases every key Dear ImGui saw held (K is still down).
  const HWND other = CreateWindowExW(0, L"GraceHarness", L"other", WS_OVERLAPPEDWINDOW, 700, 400, 200, 120, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
  const std::uint32_t keys_before = grace.input().imgui_keys_down;
  const bool focused_before = GetFocus() == window && grace.input().holder_owner == 1;
  g_seen = Seen{};
  ShowWindow(other, SW_SHOW);  // a new window shown in front takes the activation and the focus
  SetForegroundWindow(other);
  SetFocus(other);
  pump_messages();
  frames(game, 3);
  input = grace.input();
  check(focused_before && keys_before >= 1 && input.holder_owner == 0 &&
            input.last_close_reason == GRACEUI_TEST_CLOSED_FOCUS_LOST && input.imgui_keys_down == 0 &&
            g_seen.kill_focus >= 1,
        "focus loss",
        "with a panel holding input and %u key down in Dear ImGui, the window lost the focus (WM_KILLFOCUS reached the "
        "game: %d): the holder ended (%s), and Dear ImGui sees %u key down",
        keys_before, g_seen.kill_focus, reason_name(input.last_close_reason), input.imgui_keys_down);
  inject_key('K', kScanK, false);
  settle();
  cursor_into(window);
  if (other != nullptr) {
    DestroyWindow(other);
  }
  pump_messages();
  frames(game, 3);

  // 11. A provider's danger: request_input answers not_now, and a holder ends (invariant 9).
  grace.set_world(GRACEUI_TEST_WORLD_DANGER);
  const GraceUiTestAnswer in_danger = grace.request(1, kPanelKind, GRACEUI_TEST_WANT_ALL);
  grace.set_world(0);
  take(grace, game, 1, kPanelKind);
  grace.set_world(GRACEUI_TEST_WORLD_DANGER);
  input = grace.input();
  grace.set_world(0);
  check(in_danger.answer == GRACEUI_TEST_NOT_NOW && in_danger.why == GRACEUI_TEST_NOT_NOW_DANGER &&
            input.holder_owner == 0 && input.last_close_reason == GRACEUI_TEST_CLOSED_DANGER,
        "danger", "request_input in danger: answer %u, reason %u; a holder when danger came: ended (%s)",
        in_danger.answer, in_danger.why, reason_name(input.last_close_reason));

  // 12. 2 s without an overlay frame ends the holder (the game reads input on, presenting nothing).
  frames(game, 3);
  take(grace, game, 1, kPanelKind);
  const DWORD quiet_start = GetTickCount();
  while (GetTickCount() - quiet_start < 4000 && grace.input().holder_owner != 0) {
    reads.keys();
    pump_messages();
    Sleep(100);
  }
  const DWORD quiet_for = GetTickCount() - quiet_start;
  input = grace.input();
  frames(game, 5);
  check(input.holder_owner == 0 && input.last_close_reason == GRACEUI_TEST_CLOSED_NO_RENDER && quiet_for >= 1900,
        "no render", "with no frame presented, the holder ended (%s) after %lu ms", reason_name(input.last_close_reason),
        static_cast<unsigned long>(quiet_for));

  // 13. The input half's own cost per message and per read, and no allocation in it or on the present thread, with a
  // holder's frames fed every frame.
  grace.reset_counters();
  for (int i = 0; i < 1000; ++i) {
    reads.keys();
    reads.mouse();
    read_pads(pads);
    PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(i % 600, 100));
    if (i % 20 == 0) {
      pump_messages();
    }
  }
  pump_messages();
  const Spread idle_window = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_WINDOW));
  const Spread idle_dinput = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_DINPUT));
  const Spread idle_xinput = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_XINPUT));
  const GraceUiTestState idle_state = grace.state();

  // A holder's frames as a player types and moves the mouse: a move every frame, a key pressed or released and a
  // character every fourth (15 characters a second at 60 frames a second), the GPU idle before each frame as for
  // the render half's frame cost.
  take(grace, game, 1, kPanelKind);
  const auto busy_frame = [&](int i) {
    PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(100 + i % 300, 120));
    if (i % 4 == 0) {
      PostMessageW(window, WM_CHAR, 'a' + i % 26, 1);
      post_key(window, 'J', 0x24, i % 8 == 0, false);
    }
    reads.keys();
    reads.mouse();
    read_pads(pads);
    game.frame(kInputColour, PresentWith::present);
    game.wait_idle();
  };
  for (int i = 0; i < 60; ++i) {
    busy_frame(i);
  }
  grace.reset_counters();
  const GraceUiTestState busy_before = grace.state();
  for (int i = 0; i < 300; ++i) {
    busy_frame(i);
  }
  const GraceUiTestState busy = grace.state();
  input = grace.input();
  const Spread held_window = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_WINDOW));
  const Spread held_dinput = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_DINPUT));
  const Spread held_xinput = spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_XINPUT));
  const Costs held_frames = frame_costs(grace);
  grace.release_input(1);
  frames(game, 2);
  check(idle_state.allocations == 0 && idle_state.frees == 0 && busy.allocations == 0 && busy.frees == 0 &&
            input.events_dropped == 0,
        "input allocations",
        "in the filter, the detours and the present thread: %llu allocations and %llu frees over 1000 reads and "
        "messages with no holder; %llu and %llu over %llu frames with a holder fed every frame; %u events dropped",
        static_cast<unsigned long long>(idle_state.allocations), static_cast<unsigned long long>(idle_state.frees),
        static_cast<unsigned long long>(busy.allocations), static_cast<unsigned long long>(busy.frees),
        static_cast<unsigned long long>(busy.frames_drawn - busy_before.frames_drawn), input.events_dropped);
  check(idle_window.p99_us <= 50 && idle_dinput.p99_us <= 50 && idle_xinput.p99_us <= 50 && held_window.p99_us <= 50 &&
            held_dinput.p99_us <= 50 && held_xinput.p99_us <= 50,
        "input cost",
        "GraceUI's own work (budget: p99 50 us each, EST), median / p99 in us: with no holder, the window filter "
        "%.2f / %.2f per message, GetDeviceState %.2f / %.2f per read, XInputGetState %.2f / %.2f; with a holder, "
        "%.2f / %.2f, %.2f / %.2f, %.2f / %.2f",
        idle_window.median_us, idle_window.p99_us, idle_dinput.median_us, idle_dinput.p99_us, idle_xinput.median_us,
        idle_xinput.p99_us, held_window.median_us, held_window.p99_us, held_dinput.median_us, held_dinput.p99_us,
        held_xinput.median_us, held_xinput.p99_us);
  check(held_frames.count >= 300 && held_frames.own.p99_us <= 1000.0, "holder frame cost",
        "with a holder fed every frame and its cursor drawn, over %u frames: GraceUI's own work median %.1f us, p99 "
        "%.1f us (budget: p99 1000 us); the whole detour median %.1f us, p99 %.1f us",
        held_frames.count, held_frames.own.median_us, held_frames.own.p99_us, held_frames.total.median_us,
        held_frames.total.p99_us);

  const GraceUiTestState state = grace.state();
  check(state.render_off == 0 && grace.input().input_off == 0 && state.hook_install_runs == 1, "input path",
        "the render path and input still on after every check; %llu presents, %llu frames drawn, %llu fed by a holder",
        static_cast<unsigned long long>(state.presents), static_cast<unsigned long long>(state.frames_drawn),
        static_cast<unsigned long long>(grace.input().frames_fed));
  grace.print_log();
}

// ---- An ANSI game window: its character messages carry bytes in the keyboard's code page, a double-byte character in
// two of them, or whole in WM_IME_CHAR; the filter joins and decodes them before Dear ImGui sees them.

void run_input_ansi(const std::wstring& dll) {
  g_ansi = true;
  Game game(false);
  const HWND window = game.window();
  SetForegroundWindow(window);
  SetFocus(window);
  pump_messages();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  grace.set_panel(1);
  frames(game, 30);
  GraceUiTestInput input = grace.input();
  const bool ansi = input.window_filter == 1 && input.window_unicode == 0 && IsWindowUnicode(window) == FALSE;
  const bool granted = take(grace, game, 1, kPanelKind);
  grace.reset_counters();
  g_seen = Seen{};
  grace.set_code_page(1252);
  PostMessageA(window, WM_CHAR, 'a', 1);
  PostMessageA(window, WM_CHAR, 0xE9, 1);  // é in Windows-1252
  frames(game, 4);
  grace.set_code_page(932);
  PostMessageA(window, WM_CHAR, 0x82, 1);  // あ in Shift-JIS, a lead byte and a trail byte in two messages
  PostMessageA(window, WM_CHAR, 0xA0, 1);
  frames(game, 4);
  SendMessageA(window, WM_IME_CHAR, 0x82A2, 1);  // い, whole, as an IME sends it (the lead byte high)
  SendMessageA(window, WM_UNICHAR, 0x1F600, 0);
  frames(game, 4);
  grace.set_code_page(0);
  const std::vector<std::uint32_t> got = chars_of(grace);
  const std::vector<std::uint32_t> want = {'a', 0xE9, 0x3042, 0x3044, 0x1F600};
  input = grace.input();
  check(ansi && granted && got == want && g_seen.chars == 0, "ANSI characters",
        "an ANSI window, filtered as one (%s); Dear ImGui received %s (wanted %s); character messages reaching the "
        "game: %d",
        ansi ? "yes" : "NO", code_points(got).c_str(), code_points(want).c_str(), g_seen.chars);
  grace.release_input(1);
  grace.print_log();
}

int run_child(const std::wstring& dll, const wchar_t* scenario) {
  wchar_t program[MAX_PATH];
  GetModuleFileNameW(nullptr, program, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(program) + L"\" --dll \"" + dll + L"\" --scenario " + scenario;
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(program, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
    return -1;
  }
  WaitForSingleObject(process.hProcess, 300000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(code);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring dll = folder_of_this_program() + L"graceui.dll";
  std::wstring scenario;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (std::wcscmp(argv[i], L"--dll") == 0) {
      dll = argv[i + 1];
    } else if (std::wcscmp(argv[i], L"--scenario") == 0) {
      scenario = argv[i + 1];
    }
  }
  if (scenario == L"two-queues") {
    std::printf("-- scenario: two direct queues\n");
    run_two_queues(dll);
  } else if (scenario == L"input") {
    std::printf("-- scenario: the input half\n");
    run_input(dll);
  } else if (scenario == L"input-ansi") {
    std::printf("-- scenario: the input half on an ANSI window\n");
    run_input_ansi(dll);
  } else {
    std::printf("-- scenario: one direct queue\n");
    run_main(dll);
    if (scenario.empty()) {
      for (const wchar_t* child_scenario : {L"two-queues", L"input", L"input-ansi"}) {
        std::fflush(stdout);
        const int child = run_child(dll, child_scenario);
        char name[64];
        std::snprintf(name, sizeof name, "%ls scenario", child_scenario);
        check(child == 0, name, "its process ended with %d", child);
      }
    }
  }
  std::printf("-- %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
