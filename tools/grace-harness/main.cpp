// grace-harness: graceui.dll's render half without a game (§2.2, D85). A Direct3D 12 window program that plays the
// game's part: it presents frames of known clear colours, loads a test build of graceui.dll, calls
// GraceUI_RequestApi(1), and reads its frames back to check what GraceUI drew on them. Every check prints PASS or FAIL
// with what it saw, and the exit code is 0 only if all passed. BUILD.md runs it under Wine on Xvfb.
//
//   grace-harness.exe [--dll <path to graceui.dll>] [--scenario main|two-queues]
//
// With no scenario it runs "main", then itself again with "two-queues" in a new process: that check needs a process in
// which the host has not found a present queue yet.
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
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

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

void pump_messages() {
  MSG message;
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
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof window_class;
    window_class.lpfnWndProc = &window_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"GraceHarness";
    RegisterClassExW(&window_class);
    RECT rect{0, 0, 640, 360};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowExW(0, L"GraceHarness", L"GraceUI harness", WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 0,
                              rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                              window_class.hInstance, nullptr);
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
    return version != nullptr && request_api != nullptr && set_panel != nullptr && get_state != nullptr &&
           reset_counters != nullptr && frame_costs != nullptr && log_line != nullptr;
  }

  GraceUiTestState state() const {
    GraceUiTestState out{};
    out.size = sizeof out;
    if (get_state(&out) == 0) {
      fatal("GraceUI_Test_GetState refused");
    }
    return out;
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
  check(state.init_threads_started == 1 && state.hook_install_runs == 1 && state.hooks_installed == 5, "start",
        "GraceUI_RequestApi(1) answered null and started %u init thread, which installed %u hooks in %u run",
        state.init_threads_started, state.hooks_installed, state.hook_install_runs);

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
            state.hooks_installed == 5,
        "second request", "answered %s; init threads %u, install runs %u, hooks %u",
        table == nullptr ? "null" : "a table", state.init_threads_started, state.hook_install_runs,
        state.hooks_installed);

  // 8. The same DLL loaded again is the same module: nothing new.
  const HMODULE again = LoadLibraryW(dll.c_str());
  const void* table_again = grace.request_api(1);
  state = grace.state();
  check(again == grace.module && table_again == nullptr && state.init_threads_started == 1 &&
            state.hook_install_runs == 1 && state.hooks_installed == 5,
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
            second_state.hooks_installed == 0 && state.hooks_installed == 5 && state.hook_install_runs == 1 && ok,
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
  } else {
    std::printf("-- scenario: one direct queue\n");
    run_main(dll);
    if (scenario.empty()) {
      std::fflush(stdout);
      const int child = run_child(dll, L"two-queues");
      check(child == 0, "two-queues scenario", "its process ended with %d", child);
    }
  }
  std::printf("-- %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
