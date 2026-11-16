// grace-harness: graceui.dll without a game (§2.2, D85). A Direct3D 12 window program that plays the game's part: it
// presents frames of known clear colours, reads the keyboard and mouse through DirectInput and pads through XInput,
// loads a test build of graceui.dll, calls GraceUI_RequestApi(1), reads its frames back to check what GraceUI drew on
// them, and drives the input half through injected input, posted messages and the test exports. Every check prints PASS
// or FAIL with what it saw, and the exit code is 0 only if all passed. BUILD.md runs it under Wine on Xvfb.
//
//   grace-harness.exe [--dll <path to graceui.dll>] [--scenario <name>]
//
// With no scenario it runs "main", then itself again in a new process for each of the others: "two-queues" needs a
// process in which the host has not found a present queue yet, "input" one whose XInput DLLs are loaded before GraceUI
// starts, and "input-ansi" one whose game window is an ANSI window; "abi" (ABI 1's table, connect and notices, and the
// example mod finding GraceUI by name), "copies" (two copies asked at once), "found-by-scan" and "found-by-path" (the
// example finding a renamed copy, and GraceUI's own folder), "anti-cheat" and "anti-cheat-parent" (a simulated hit by
// each clause) each need a process whose first GraceUI_RequestApi is theirs.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>

#include <d3d12.h>
#include <dinput.h>
#include <dxgi1_4.h>
#include <imm.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "graceui_sys.hpp"
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

// ---- Notices on a frame (D134): boxes of a known width at the top right, each with its accent at its left.

constexpr Colour colour_of(std::uint32_t rgb) {
  return Colour{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                static_cast<std::uint8_t>(rgb)};
}
constexpr Colour kNoticeGround = colour_of(GRACEUI_TEST_NOTICE_GROUND);
constexpr Colour kNoticeAccents[] = {colour_of(GRACEUI_TEST_NOTICE_GOLD), colour_of(GRACEUI_TEST_NOTICE_EMBER),
                                     colour_of(GRACEUI_TEST_NOTICE_ROT)};
constexpr const char* kToneNames[] = {"gold", "ember", "rot"};

int notice_left(const Frame& frame) {
  return static_cast<int>(frame.width) - GRACEUI_TEST_NOTICE_MARGIN - GRACEUI_TEST_NOTICE_WIDTH;
}

bool in_notice_column(const Frame& frame, UINT x) {
  return static_cast<int>(x) >= notice_left(frame) && x < frame.width - GRACEUI_TEST_NOTICE_MARGIN;
}

struct NoticeBox {
  UINT top = 0;
  UINT bottom = 0;
  int tone = 0;  // GRACEUI_TONE_*: which accent
  bool ground = false;
  int ink = 0;  // ink.hi pixels of its text
};

// The notice boxes on a frame, top to bottom: runs of an accent's colour down the accent's column.
std::vector<NoticeBox> notice_boxes(const Frame& frame) {
  std::vector<NoticeBox> boxes;
  const int left = notice_left(frame);
  if (left < 0) {
    return boxes;
  }
  const UINT x = static_cast<UINT>(left) + 1;
  int open = -1;
  UINT top = 0;
  for (UINT y = GRACEUI_TEST_NOTICE_MARGIN; y <= frame.height; ++y) {
    int tone = -1;
    for (int i = 0; y < frame.height && i < 3 && tone < 0; ++i) {
      tone = close_to(frame.at(x, y), kNoticeAccents[i], 3) ? i : -1;
    }
    if (tone == open) {
      continue;
    }
    if (open >= 0) {
      NoticeBox box;
      box.top = top;
      box.bottom = y;
      box.tone = open;
      box.ground = close_to(frame.at(static_cast<UINT>(left) + 20, top + 2), kNoticeGround, 3);
      for (UINT row = top + 8; row + 8 < y; ++row) {
        for (UINT column = static_cast<UINT>(left) + 11; column + 8 < frame.width - GRACEUI_TEST_NOTICE_MARGIN;
             ++column) {
          const std::uint32_t pixel = frame.at(column, row);
          box.ink += channel(pixel, 0) >= 200 && channel(pixel, 8) >= 190 && channel(pixel, 16) >= 170 ? 1 : 0;
        }
      }
      boxes.push_back(box);
    }
    open = tone;
    top = y;
  }
  return boxes;
}

std::string boxes_text(const std::vector<NoticeBox>& boxes) {
  std::string text = std::to_string(boxes.size()) + (boxes.size() == 1 ? " notice drawn" : " notices drawn");
  for (const NoticeBox& box : boxes) {
    char one[96];
    std::snprintf(one, sizeof one, "%s %s, rows %u to %u, %s, %d pixels of ink", text.back() == 'n' ? ":" : ";",
                  kToneNames[box.tone], box.top, box.bottom, box.ground ? "on ground.2" : "NOT ON GROUND.2", box.ink);
    text += one;
  }
  return text;
}

// The notices drawn are `tones`, top to bottom, each a whole box with its text; and outside the notices' column every
// pixel is the clear colour, but for the test panel's if `panel` (checked by panel_drawn).
bool notices_drawn(const Frame& frame, Colour clear, const std::vector<int>& tones, bool panel, std::string& why) {
  const std::vector<NoticeBox> boxes = notice_boxes(frame);
  why = boxes_text(boxes);
  bool ok = boxes.size() == tones.size();
  for (std::size_t i = 0; ok && i < boxes.size(); ++i) {
    ok = boxes[i].tone == tones[i] && boxes[i].ground && boxes[i].ink >= 20;
  }
  const std::uint32_t want = packed(clear);
  for (UINT y = 0; ok && y < frame.height; ++y) {
    for (UINT x = 0; ok && x < frame.width; ++x) {
      if (!in_notice_column(frame, x) && !(panel && in_panel(x, y)) && frame.at(x, y) != want) {
        char text[120];
        std::snprintf(text, sizeof text, "; pixel (%u,%u) outside the notices is %s", x, y,
                      describe(frame.at(x, y)).c_str());
        why += text;
        ok = false;
      }
    }
  }
  return ok;
}

// The test panel is there, and nothing else: every pixel outside its rectangle is the clear colour (but in the notices'
// column, if `notices` allows them); inside it, the panel's colour fills most of it and the text's white ink is where
// the text is.
bool panel_drawn(const Frame& frame, Colour clear, std::string& why, bool notices = false) {
  const std::uint32_t want = packed(clear);
  int panel_pixels = 0;
  int ink_pixels = 0;
  int other_pixels = 0;
  for (UINT y = 0; y < frame.height; ++y) {
    for (UINT x = 0; x < frame.width; ++x) {
      const std::uint32_t pixel = frame.at(x, y);
      if (notices && in_notice_column(frame, x)) {
        continue;
      }
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
  decltype(&GraceUI_Test_SetAntiCheatNames) set_anti_cheat_names = nullptr;
  decltype(&GraceUI_Test_GetNotice) get_notice = nullptr;
  decltype(&GraceUI_Test_ClearNotices) clear_notices = nullptr;
  decltype(&GraceUI_Test_GetConsumer) get_consumer = nullptr;
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
    return module != nullptr && bind();
  }

  // A copy that something else loaded (a mod, through graceui-sys).
  bool attach(HMODULE loaded) {
    module = loaded;
    return module != nullptr && bind();
  }

  bool bind() {
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
    set_anti_cheat_names = function_of<decltype(set_anti_cheat_names)>(module, "GraceUI_Test_SetAntiCheatNames");
    get_notice = function_of<decltype(get_notice)>(module, "GraceUI_Test_GetNotice");
    clear_notices = function_of<decltype(clear_notices)>(module, "GraceUI_Test_ClearNotices");
    get_consumer = function_of<decltype(get_consumer)>(module, "GraceUI_Test_GetConsumer");
    return version != nullptr && request_api != nullptr && set_panel != nullptr && get_state != nullptr &&
           reset_counters != nullptr && frame_costs != nullptr && log_line != nullptr && hook_name != nullptr &&
           request_input != nullptr && release_input != nullptr && set_world != nullptr && set_caret != nullptr &&
           set_code_page != nullptr && get_input != nullptr && get_chars != nullptr && input_costs != nullptr &&
           set_anti_cheat_names != nullptr && get_notice != nullptr && clear_notices != nullptr &&
           get_consumer != nullptr;
  }

  // ABI 1's table, as a mod asks for it.
  const GraceUiApi1* table() const { return static_cast<const GraceUiApi1*>(request_api(GRACEUI_ABI_MAJOR)); }

  std::vector<GraceUiTestNotice> notices() const {
    std::vector<GraceUiTestNotice> out;
    for (std::uint32_t i = 0;; ++i) {
      GraceUiTestNotice notice{};
      notice.size = sizeof notice;
      if (get_notice(i, &notice) == 0) {
        return out;
      }
      out.push_back(notice);
    }
  }

  std::vector<GraceUiTestConsumer> consumers() const {
    std::vector<GraceUiTestConsumer> out;
    for (std::uint32_t i = 0;; ++i) {
      GraceUiTestConsumer consumer{};
      consumer.size = sizeof consumer;
      if (get_consumer(i, &consumer) == 0) {
        return out;
      }
      out.push_back(consumer);
    }
  }

  // Whether GraceUI's log holds a line that contains `text`.
  bool logged(const char* text) const {
    char line[200];
    for (std::uint32_t i = 0; log_line(i, line, sizeof line) > 0; ++i) {
      if (std::strstr(line, text) != nullptr) {
        return true;
      }
    }
    return false;
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

// Starts graceui.dll's host as a mod would, and waits (presenting) until its hooks are installed; answers ABI 1's table.
const GraceUiApi1* start_host(Game& game, const GraceUi& grace) {
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    grace.print_log();
    fatal("GraceUI_RequestApi(1) answered null: no table");
  }
  for (int i = 0; i < 1500; ++i) {  // 30 s
    const GraceUiTestState state = grace.state();
    if (state.host == GRACEUI_TEST_HOST_HOOKED) {
      return table;
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

  const GraceUiApi1* table = start_host(game, grace);
  GraceUiTestState state = grace.state();
  std::string hooks_why;
  ok = hooks_as_expected(grace, 0, hooks_why);
  const std::uint32_t hooks = state.hooks_installed;
  check(ok && state.init_threads_started == 1 && table->size == sizeof(GraceUiApi1) &&
            table->abi_minor == GRACEUI_ABI1_MINOR && table->graceui_version == version && state.notice_only == 0 &&
            state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_NONE,
        "start",
        "GraceUI_RequestApi(1) answered ABI 1's table (%u bytes, ABI 1.%u, GraceUI %u.%u.%u); the anti-cheat check "
        "found %s; %u init thread started, which installed %s",
        table->size, table->abi_minor, GRACEUI_VERSION_MAJOR(table->graceui_version),
        GRACEUI_VERSION_MINOR(table->graceui_version), GRACEUI_VERSION_PATCH(table->graceui_version),
        state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_NONE ? "nothing" : "A HIT", state.init_threads_started,
        hooks_why.c_str());

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

  // 7. A second GraceUI_RequestApi starts nothing, and answers the same table.
  const GraceUiApi1* table_again = grace.table();
  state = grace.state();
  check(table_again == table && state.init_threads_started == 1 && state.hook_install_runs == 1 &&
            state.hooks_installed == hooks,
        "second request", "answered %s; init threads %u, install runs %u, hooks %u",
        table_again == table ? "the same table" : "ANOTHER TABLE", state.init_threads_started, state.hook_install_runs,
        state.hooks_installed);

  // 8. The same DLL loaded again is the same module: nothing new.
  const HMODULE again = LoadLibraryW(dll.c_str());
  const GraceUiApi1* table_loaded_again = grace.table();
  state = grace.state();
  check(again == grace.module && table_loaded_again == table && state.init_threads_started == 1 &&
            state.hook_install_runs == 1 && state.hooks_installed == hooks,
        "second load",
        "LoadLibrary of the same file gave %s module and %s; init threads %u, install runs %u, hooks %u",
        again == grace.module ? "the same" : "another", table_loaded_again == table ? "the same table" : "ANOTHER",
        state.init_threads_started, state.hook_install_runs, state.hooks_installed);
  if (again != nullptr) {
    FreeLibrary(again);
  }

  // 9. A second copy of GraceUI from another folder idles (D72, invariant 18); the active one names it once, by its
  // name relative to the game's folder, in a notice it draws, and draws on.
  const std::wstring copy_folder = folder_of_this_program() + L"second-copy";
  CreateDirectoryW(copy_folder.c_str(), nullptr);
  const std::wstring copy = copy_folder + L"\\graceui.dll";
  GraceUi second;
  if (!CopyFileW(dll.c_str(), copy.c_str(), FALSE) || !second.load(copy)) {
    fatal("the second copy of graceui.dll did not load");
  }
  const void* second_table = second.request_api(1);
  const void* second_again = second.request_api(1);
  const GraceUiTestState second_state = second.state();
  for (int i = 0; i < 5; ++i) {
    frame = game.frame_and_read(kSecondCopy, PresentWith::present);
  }
  ok = panel_drawn(frame, kSecondCopy, why, true);
  std::string notice_why;
  const bool notice_shown = notices_drawn(frame, kSecondCopy, {GRACEUI_TONE_WARNING}, true, notice_why);
  const std::vector<GraceUiTestNotice> named = grace.notices();
  const bool named_once = named.size() == 1 && named[0].owner == 0 &&
                          std::strstr(named[0].text, "second-copy\\graceui.dll") != nullptr &&
                          std::strstr(named[0].text, ":\\") == nullptr;
  state = grace.state();
  check(second.module != grace.module && second_table == nullptr && second_again == nullptr &&
            second_state.host == GRACEUI_TEST_HOST_IDLE_COPY && second_state.init_threads_started == 0 &&
            second_state.hooks_installed == 0 && state.hooks_installed == hooks && state.hook_install_runs == 1 && ok &&
            named_once && state.copies_named == 1 && notice_shown,
        "second copy",
        "another module (%s) answered null twice, idles (state %u) with %u init threads and %u hooks; the first keeps "
        "its %u hooks and %s; it named %u copy in %zu notice: \"%s\"; %s",
        second.module != grace.module ? "yes" : "no", second_state.host, second_state.init_threads_started,
        second_state.hooks_installed, state.hooks_installed, why.c_str(), state.copies_named, named.size(),
        named.empty() ? "" : named[0].text, notice_why.c_str());
  second.print_log();
  grace.clear_notices();  // the frames read from here on hold the test panel alone

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

// ---- S10.4.b: ABI 1's table, connect and notices; the example mod; two copies; the anti-cheat check (D131 to D136).

constexpr Colour kAbiColour{24, 48, 72};
constexpr Colour kAntiCheatColour{64, 24, 48};

// A connect request as a mod fills it.
GraceUiConnectRequest request_for(const char* id, std::uint32_t floor = GRACEUI_VERSION_PACK(0, 1, 0),
                                  const char* name = "An invented mod", const char* caps = "notices") {
  GraceUiConnectRequest request{};
  request.size = sizeof request;
  request.abi_minor = GRACEUI_ABI1_MINOR;
  request.version = GRACEUI_VERSION_PACK(2, 3, 4);
  request.floor = floor;
  request.id = id;
  request.id_length = std::strlen(id);
  request.name = name;
  request.name_length = std::strlen(name);
  request.caps = caps;
  request.caps_length = std::strlen(caps);
  return request;
}

struct Connected {
  GraceUiStatus status = GRACEUI_FAULT;
  GraceUiConnectAnswer answer{};
};

Connected connect(const GraceUiApi1* table, const GraceUiConnectRequest& request) {
  Connected out;
  out.answer.size = sizeof out.answer;
  out.status = table->connect(&request, &out.answer);
  return out;
}

GraceUiStatus post(const GraceUiApi1* table, GraceUiConsumer consumer, const char* text,
                   std::uint32_t tone = GRACEUI_TONE_NORMAL, std::uint32_t key = 0, std::uint32_t duration_ms = 0) {
  GraceUiNotice notice{};
  notice.size = sizeof notice;
  notice.tone = tone;
  notice.key = key;
  notice.duration_ms = duration_ms;
  notice.text = text;
  notice.text_length = std::strlen(text);
  return table->notice(consumer, &notice);
}

const char* status_name(GraceUiStatus status) {
  switch (status) {
    case GRACEUI_OK: return "ok";
    case GRACEUI_TOO_OLD: return "too_old";
    case GRACEUI_DUPLICATE_ID: return "duplicate_id";
    case GRACEUI_INACTIVE: return "inactive";
    case GRACEUI_REFUSED: return "refused";
    case GRACEUI_NOT_CONNECTED: return "not_connected";
    case GRACEUI_NOT_YET: return "not_yet";
    case GRACEUI_FULL: return "full";
    case GRACEUI_FAULT: return "fault";
    default: return "UNKNOWN";
  }
}

// What every slot that GraceUI 0.1 declares but does not provide answers for `consumer`, in the table's order.
std::vector<GraceUiStatus> unbuilt_answers(const GraceUiApi1* table, GraceUiConsumer consumer) {
  std::uint64_t id64 = 0;
  std::uint32_t id32 = 0;
  GraceUiTextureDesc texture{};
  texture.size = sizeof texture;
  GraceUiFaceDesc face{};
  face.size = sizeof face;
  GraceUiGlyphTable glyphs{};
  glyphs.size = sizeof glyphs;
  GraceUiPanelDesc panel{};
  panel.size = sizeof panel;
  GraceUiDrawList list{};
  list.size = sizeof list;
  GraceUiInputRequest input{};
  input.size = sizeof input;
  GraceUiInputAnswer input_answer{};
  input_answer.size = sizeof input_answer;
  GraceUiHotkeyDesc hotkey{};
  hotkey.size = sizeof hotkey;
  GraceUiWorldState world{};
  world.size = sizeof world;
  return {table->register_pages(consumer, nullptr),
          table->publish(consumer, nullptr),
          table->poll(consumer, nullptr),
          table->answer(consumer, nullptr),
          table->register_texture(consumer, &texture, &id64),
          table->register_face(consumer, &face, &id32),
          table->glyph_table(consumer, 1, &glyphs),
          table->create_panel(consumer, &panel, &id64),
          table->submit(consumer, 1, &list),
          table->show_panel(consumer, 1),
          table->hide_panel(consumer, 1),
          table->request_input(consumer, &input, &input_answer),
          table->release_input(consumer),
          table->register_hotkey(consumer, &hotkey, &id32),
          table->set_world_state(consumer, &world),
          table->log(consumer, GRACEUI_LOG_INFO, "invented", 8)};
}

// "16 slots: not_yet", or each slot's answer where they differ.
std::string answers_text(const std::vector<GraceUiStatus>& answers) {
  bool same = true;
  for (const GraceUiStatus answer : answers) {
    same = same && answer == answers.front();
  }
  if (same && !answers.empty()) {
    return std::to_string(answers.size()) + " slots: " + status_name(answers.front());
  }
  std::string text;
  for (const GraceUiStatus answer : answers) {
    text += (text.empty() ? "" : ",") + std::string(status_name(answer));
  }
  return text;
}

bool all_answer(const std::vector<GraceUiStatus>& answers, GraceUiStatus want) {
  return !answers.empty() && std::all_of(answers.begin(), answers.end(), [&](GraceUiStatus s) { return s == want; });
}

// Presents frames (every 20 ms) until `done` holds, for at most `ms`; answers whether it held.
template <class Done>
bool present_until(Game* game, Colour clear, int ms, Done done) {
  for (int waited = 0; waited <= ms; waited += 20) {
    if (done()) {
      return true;
    }
    if (game != nullptr) {
      game->frame(clear, PresentWith::present);
    }
    Sleep(20);
  }
  return done();
}

// The example mod's connection and notice as GraceUI holds them: its id, and the notice's text ("" if none).
bool example_connected(const GraceUi& grace, std::string& notice_text, std::uint32_t& owner) {
  for (const GraceUiTestConsumer& consumer : grace.consumers()) {
    if (std::string_view(consumer.id) != "hello-notice") {
      continue;
    }
    owner = consumer.owner;
    for (const GraceUiTestNotice& notice : grace.notices()) {
      if (notice.owner == consumer.owner) {
        notice_text = notice.text;
        return true;
      }
    }
  }
  return false;
}

// The look at which the example found GraceUI, from its notice ("... at look N."); 0 if it does not say.
unsigned look_of(const std::string& text) {
  const std::size_t at = text.rfind("at look ");
  return at == std::string::npos ? 0u : static_cast<unsigned>(std::strtoul(text.c_str() + at + 8, nullptr, 10));
}

std::wstring example_path() { return folder_of_this_program() + L"hello-notice.dll"; }

void run_abi(const std::wstring& dll) {
  Game game(false);
  std::string why;

  // 1. The example mod, loaded before GraceUI: its own thread looks for GraceUI every 250 ms (graceui-sys), finds it by
  // name once it is loaded, and its request is the first, so it starts the host.
  if (LoadLibraryW(example_path().c_str()) == nullptr) {
    fatal("hello-notice.dll did not load");
  }
  present_until(&game, kAbiColour, 800, [] { return false; });  // three of its looks, or more, find nothing
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  std::string example_text;
  std::uint32_t example_owner = 0;
  const bool example_seen =
      present_until(&game, kAbiColour, 10000, [&] { return example_connected(grace, example_text, example_owner); });
  const GraceUiTestState started = grace.state();
  const GraceUiApi1* table = start_host(game, grace);
  const std::vector<GraceUiTestConsumer> consumers = grace.consumers();
  const bool example_as_named = consumers.size() == 1 && std::string_view(consumers[0].name) == "Hello notice" &&
                                consumers[0].version == GRACEUI_VERSION_PACK(1, 0, 0) &&
                                consumers[0].floor == GRACEUI_VERSION_PACK(0, 1, 0) &&
                                std::string_view(consumers[0].caps) == "notices";
  check(example_seen && example_as_named && started.init_threads_started == 1 &&
            example_text.find("GraceUI 0.1.0, found by name") != std::string::npos && look_of(example_text) >= 2,
        "example mod",
        "hello-notice.dll, loaded before GraceUI, %s (%zu mod connected, as it names itself: %s), and its request "
        "started the host (%u init thread before ours); its notice: \"%s\"",
        example_seen ? "connected through graceui-sys" : "DID NOT CONNECT", consumers.size(),
        example_as_named ? "yes" : "NO", started.init_threads_started, example_text.c_str());

  // The hooks are in, but the renderer warms up over the frames that follow (D116, D117): the notice is drawn once it is.
  Frame frame;
  bool ok = false;
  int frames_until_seen = 0;
  for (int i = 1; i <= 60 && !ok; ++i) {
    frame = game.frame_and_read(kAbiColour, PresentWith::present);
    ok = notices_drawn(frame, kAbiColour, {GRACEUI_TONE_NORMAL}, false, why);
    frames_until_seen = i;
  }
  check(ok, "example's notice", "on the game's frame, in frame %d after the hooks, with nothing else drawn: %s",
        frames_until_seen, why.c_str());

  // 2. The table: ABI 1 only, the size this header gives it, and the one cap GraceUI 0.1 provides.
  const std::uint32_t version = grace.version();
  const bool only_major_1 = grace.request_api(0) == nullptr && grace.request_api(2) == nullptr &&
                            grace.request_api(0xFFFFFFFFu) == nullptr && grace.table() == table;
  const graceui::Api api(table);
  const bool caps_ok = api.has_cap(GRACEUI_CAP_NOTICES) && !api.has_cap("panels") && !api.has_cap("") &&
                       !api.has_cap("Notices") && table->has_cap(nullptr, 0) == 0 && table->has_cap(nullptr, 7) == 0;
  bool slots_present = true;
  for (std::size_t offset = offsetof(GraceUiApi1, has_cap); offset < sizeof(GraceUiApi1); offset += sizeof(void*)) {
    void* slot = nullptr;
    std::memcpy(&slot, reinterpret_cast<const unsigned char*>(table) + offset, sizeof slot);
    slots_present = slots_present && slot != nullptr;
  }
  check(table->size == sizeof(GraceUiApi1) && table->abi_minor == GRACEUI_ABI1_MINOR &&
            table->graceui_version == version && table->reserved == 0 && only_major_1 && caps_ok && slots_present &&
            GRACEUI_API1_COVERS(table, log),
        "table",
        "%u bytes (this header's %zu), ABI 1.%u, GraceUI %u.%u.%u; every one of the 20 slots %s; majors 0, 2 and "
        "0xFFFFFFFF %s; has_cap: notices yes, panels, \"\", \"Notices\" and a null name %s",
        table->size, sizeof(GraceUiApi1), table->abi_minor, GRACEUI_VERSION_MAJOR(table->graceui_version),
        GRACEUI_VERSION_MINOR(table->graceui_version), GRACEUI_VERSION_PATCH(table->graceui_version),
        slots_present ? "set" : "NOT SET", only_major_1 ? "answered null" : "ANSWERED A TABLE",
        caps_ok ? "no" : "NOT AS EXPECTED");

  // 3. connect's answers, across versions and ids: ok, duplicate_id, too_old naming both versions, refused naming why.
  struct Case {
    const char* what;
    GraceUiConnectRequest request;
    GraceUiStatus status;
    std::uint32_t reason;
  };
  const std::string long_id(33, 'a');
  const std::string long_name(65, 'n');
  const std::vector<Case> cases = {
      {"a new id", request_for("abi-alpha"), GRACEUI_OK, 0},
      {"the same id again", request_for("abi-alpha"), GRACEUI_DUPLICATE_ID, 0},
      {"the example's id", request_for("hello-notice"), GRACEUI_DUPLICATE_ID, 0},
      {"floor 0 (any)", request_for("abi-any", 0), GRACEUI_OK, 0},
      {"floor 0.0.1", request_for("abi-older", GRACEUI_VERSION_PACK(0, 0, 1)), GRACEUI_OK, 0},
      {"floor 0.1.0", request_for("abi-same"), GRACEUI_OK, 0},
      {"floor 0.1.1", request_for("abi-patch", GRACEUI_VERSION_PACK(0, 1, 1)), GRACEUI_TOO_OLD, 0},
      {"floor 0.2.0", request_for("abi-minor", GRACEUI_VERSION_PACK(0, 2, 0)), GRACEUI_TOO_OLD, 0},
      {"floor 1.0.0", request_for("abi-major", GRACEUI_VERSION_PACK(1, 0, 0)), GRACEUI_TOO_OLD, 0},
      {"id \"ab\"", request_for("ab"), GRACEUI_REFUSED, GRACEUI_REFUSED_ID},
      {"id \"Abi-Upper\"", request_for("Abi-Upper"), GRACEUI_REFUSED, GRACEUI_REFUSED_ID},
      {"an id of 33 bytes", request_for(long_id.c_str()), GRACEUI_REFUSED, GRACEUI_REFUSED_ID},
      {"id \"abi_under\"", request_for("abi_under"), GRACEUI_REFUSED, GRACEUI_REFUSED_ID},
      {"an empty name", request_for("abi-name", 0, ""), GRACEUI_REFUSED, GRACEUI_REFUSED_NAME},
      {"a name not UTF-8", request_for("abi-name", 0, "bad \xC3"), GRACEUI_REFUSED, GRACEUI_REFUSED_NAME},
      {"a name of 65 bytes", request_for("abi-name", 0, long_name.c_str()), GRACEUI_REFUSED, GRACEUI_REFUSED_NAME},
      {"caps with two spaces", request_for("abi-caps", 0, "Invented", "notices  panels"), GRACEUI_REFUSED,
       GRACEUI_REFUSED_CAPS},
  };
  std::string wrong;
  std::vector<GraceUiConsumer> handles;
  GraceUiConnectAnswer too_old{};
  for (const Case& c : cases) {
    const Connected got = connect(table, c.request);
    const bool handle_ok = (got.status == GRACEUI_OK) == (got.answer.consumer != 0);
    if (got.status != c.status || got.answer.status != c.status || got.answer.reason != c.reason || !handle_ok ||
        got.answer.graceui_version != version || got.answer.abi_minor != GRACEUI_ABI1_MINOR) {
      wrong += std::string(wrong.empty() ? "" : "; ") + c.what + " answered " + status_name(got.status) + " (reason " +
               std::to_string(got.answer.reason) + ")";
    }
    if (got.status == GRACEUI_OK) {
      handles.push_back(got.answer.consumer);
    }
    if (c.status == GRACEUI_TOO_OLD && c.request.floor == GRACEUI_VERSION_PACK(0, 2, 0)) {
      too_old = got.answer;
    }
  }
  // The arguments themselves: a null request, one below its first size, and an answer GraceUI cannot write.
  GraceUiConnectRequest undersized = request_for("abi-small");
  undersized.size = 60;
  Connected null_request;
  null_request.answer.size = sizeof null_request.answer;
  null_request.status = table->connect(nullptr, &null_request.answer);
  const Connected small_request = connect(table, undersized);
  const GraceUiConnectRequest unanswered = request_for("abi-unanswered");
  const GraceUiStatus null_answer = table->connect(&unanswered, nullptr);
  GraceUiConnectAnswer short_answer{};
  short_answer.size = 16;
  short_answer.reason = 0xABCDu;
  const GraceUiStatus short_status = table->connect(&unanswered, &short_answer);
  const bool arguments_ok = null_request.status == GRACEUI_REFUSED &&
                            null_request.answer.reason == GRACEUI_REFUSED_ARGUMENT &&
                            small_request.status == GRACEUI_REFUSED &&
                            small_request.answer.reason == GRACEUI_REFUSED_ARGUMENT && null_answer == GRACEUI_REFUSED &&
                            short_status == GRACEUI_REFUSED && short_answer.reason == 0xABCDu;
  std::string too_old_notice;
  for (const GraceUiTestNotice& notice : grace.notices()) {
    if (notice.owner == 0 && std::strstr(notice.text, "needs GraceUI 0.2.0 or newer; this is GraceUI 0.1.0")) {
      too_old_notice = notice.text;
    }
  }
  check(wrong.empty() && arguments_ok && too_old.graceui_version == version &&
            too_old.floor == GRACEUI_VERSION_PACK(0, 2, 0) &&
            too_old_notice.rfind("An invented mod needs", 0) == 0 &&
            grace.logged("abi-minor needs GraceUI 0.2.0 or newer; this is GraceUI 0.1.0"),
        "connect answers",
        "%zu requests answered as they should%s%s; a null request, one of 60 bytes, a null answer and one of 16 bytes "
        "%s; too_old named GraceUI %u.%u.%u and the floor %u.%u.%u, GraceUI's notice \"%s\" and its log line %s",
        cases.size(), wrong.empty() ? "" : ", but: ", wrong.c_str(), arguments_ok ? "refused, writing nothing they "
        "should not" : "NOT AS EXPECTED", GRACEUI_VERSION_MAJOR(too_old.graceui_version),
        GRACEUI_VERSION_MINOR(too_old.graceui_version), GRACEUI_VERSION_PATCH(too_old.graceui_version),
        GRACEUI_VERSION_MAJOR(too_old.floor), GRACEUI_VERSION_MINOR(too_old.floor),
        GRACEUI_VERSION_PATCH(too_old.floor), too_old_notice.c_str(),
        grace.logged("abi-minor needs GraceUI 0.2.0") ? "written" : "MISSING");
  for (const GraceUiConsumer handle : handles) {
    table->disconnect(handle);
  }
  grace.clear_notices();

  // 4. Struct sizes, each way round (D73): a newer mod's larger request and answer, read and written as far as this
  // GraceUI knows them and no further.
  struct NewerRequest {
    GraceUiConnectRequest base;
    unsigned char later[16];
  };
  struct NewerAnswer {
    GraceUiConnectAnswer base;
    unsigned char later[16];
  };
  NewerRequest newer{};
  newer.base = request_for("abi-newer");
  newer.base.size = sizeof newer;
  std::memset(newer.later, 0xAB, sizeof newer.later);
  NewerAnswer bigger{};
  bigger.base.size = sizeof bigger;
  std::memset(bigger.later, 0xCD, sizeof bigger.later);
  const GraceUiStatus newer_status = table->connect(&newer.base, &bigger.base);
  const bool later_untouched =
      std::all_of(std::begin(bigger.later), std::end(bigger.later), [](unsigned char b) { return b == 0xCD; });
  check(newer_status == GRACEUI_OK && bigger.base.status == GRACEUI_OK && bigger.base.consumer != 0 &&
            bigger.base.size == sizeof bigger && later_untouched,
        "struct sizes",
        "a request of %zu bytes (this header's 64, then 16 a later one adds) answered %s; an answer of %zu bytes was "
        "written to its 32 bytes and %s past them",
        sizeof newer, status_name(newer_status), sizeof bigger, later_untouched ? "not" : "ALSO");
  table->disconnect(bigger.base.consumer);

  // 5. Two mods connecting in either order: each is connected with its own handle, and each one's notice shows.
  std::string orders;
  bool orders_ok = true;
  for (const bool alpha_first : {true, false}) {
    const char* first_id = alpha_first ? "abi-alpha" : "abi-beta";
    const char* second_id = alpha_first ? "abi-beta" : "abi-alpha";
    const Connected first = connect(table, request_for(first_id));
    const Connected second = connect(table, request_for(second_id));
    const GraceUiStatus first_notice = post(table, first.answer.consumer, "The first mod's notice.", GRACEUI_TONE_DANGER);
    const GraceUiStatus second_notice =
        post(table, second.answer.consumer, "The second mod's notice.", GRACEUI_TONE_WARNING);
    for (int i = 0; i < 3; ++i) {
      frame = game.frame_and_read(kAbiColour, PresentWith::present);
    }
    std::string drawn;
    const bool both_drawn = notices_drawn(frame, kAbiColour, {GRACEUI_TONE_DANGER, GRACEUI_TONE_WARNING}, false, drawn);
    const GraceUiStatus first_gone = table->disconnect(first.answer.consumer);
    const GraceUiStatus second_gone = table->disconnect(second.answer.consumer);
    for (int i = 0; i < 3; ++i) {
      frame = game.frame_and_read(kAbiColour, PresentWith::present);
    }
    const bool withdrawn = notice_boxes(frame).empty();
    const bool order_ok = first.status == GRACEUI_OK && second.status == GRACEUI_OK &&
                          first.answer.consumer != second.answer.consumer && first_notice == GRACEUI_OK &&
                          second_notice == GRACEUI_OK && both_drawn && first_gone == GRACEUI_OK &&
                          second_gone == GRACEUI_OK && withdrawn;
    orders_ok = orders_ok && order_ok;
    orders += std::string(orders.empty() ? "" : "; ") + first_id + " then " + second_id + ": " +
              status_name(first.status) + ", " + status_name(second.status) + ", " + drawn +
              (withdrawn ? ", gone once both disconnected" : ", NOT GONE after the disconnects");
  }
  check(orders_ok, "two consumers", "%s", orders.c_str());

  // 6. A handle after its disconnect names nobody; the id is free again, under a new handle.
  const Connected gone = connect(table, request_for("abi-gone"));
  const GraceUiStatus disconnected = table->disconnect(gone.answer.consumer);
  const GraceUiStatus twice = table->disconnect(gone.answer.consumer);
  const GraceUiStatus stale_notice = post(table, gone.answer.consumer, "Too late.");
  const std::vector<GraceUiStatus> stale_slots = unbuilt_answers(table, gone.answer.consumer);
  const Connected back = connect(table, request_for("abi-gone"));
  const GraceUiStatus still_stale = post(table, gone.answer.consumer, "Still too late.");
  check(gone.status == GRACEUI_OK && disconnected == GRACEUI_OK && twice == GRACEUI_NOT_CONNECTED &&
            stale_notice == GRACEUI_NOT_CONNECTED && all_answer(stale_slots, GRACEUI_NOT_CONNECTED) &&
            back.status == GRACEUI_OK && back.answer.consumer != gone.answer.consumer &&
            still_stale == GRACEUI_NOT_CONNECTED,
        "disconnect",
        "disconnected (%s), then again: %s; the old handle's notice: %s, its other slots: %s; the id again: %s, "
        "under %s handle, and the old one still %s",
        status_name(disconnected), status_name(twice), status_name(stale_notice), answers_text(stale_slots).c_str(),
        status_name(back.status), back.answer.consumer != gone.answer.consumer ? "a new" : "THE SAME",
        status_name(still_stale));
  table->disconnect(back.answer.consumer);

  // 7. The slots the next increments build: "not yet" to a connected mod, "not connected" to anyone else.
  const Connected waiting = connect(table, request_for("abi-waiting"));
  const std::vector<GraceUiStatus> unbuilt = unbuilt_answers(table, waiting.answer.consumer);
  const std::vector<GraceUiStatus> nobody = unbuilt_answers(table, 0);
  check(all_answer(unbuilt, GRACEUI_NOT_YET) && all_answer(nobody, GRACEUI_NOT_CONNECTED) && unbuilt.size() == 16,
        "not yet", "a connected mod: %s; a handle of 0: %s", answers_text(unbuilt).c_str(),
        answers_text(nobody).c_str());

  // 8. Notices: replaced in place by key, withdrawn by an empty text, timed, bounded, and refused when they should be.
  const GraceUiConsumer mod = waiting.answer.consumer;
  std::string steps;
  bool notices_ok = true;
  const auto step = [&](const char* what, bool passed) {
    notices_ok = notices_ok && passed;
    steps += std::string(steps.empty() ? "" : "; ") + what + (passed ? "" : " FAILED");
  };
  step("shown", post(table, mod, "Kept, by key 3.", GRACEUI_TONE_NORMAL, 3) == GRACEUI_OK && grace.notices().size() == 1);
  step("replaced in place", post(table, mod, "Kept, changed.", GRACEUI_TONE_DANGER, 3) == GRACEUI_OK &&
                                grace.notices().size() == 1 &&
                                std::string_view(grace.notices()[0].text) == "Kept, changed." &&
                                grace.notices()[0].tone == GRACEUI_TONE_DANGER);
  for (int i = 0; i < 3; ++i) {
    frame = game.frame_and_read(kAbiColour, PresentWith::present);
  }
  step("drawn in rot", notices_drawn(frame, kAbiColour, {GRACEUI_TONE_DANGER}, false, why));
  GraceUiNotice withdraw{};
  withdraw.size = sizeof withdraw;
  withdraw.key = 3;
  step("withdrawn", table->notice(mod, &withdraw) == GRACEUI_OK && grace.notices().empty());
  step("timed, shown", post(table, mod, "Brief.", GRACEUI_TONE_WARNING, 0, 400) == GRACEUI_OK &&
                           grace.notices().size() == 1 && grace.notices()[0].timed == 1);
  Sleep(600);
  for (int i = 0; i < 3; ++i) {
    frame = game.frame_and_read(kAbiColour, PresentWith::present);
  }
  step("timed, gone after its time", grace.notices().empty() && notice_boxes(frame).empty());
  bool four = true;
  for (int i = 0; i < 4; ++i) {
    four = four && post(table, mod, "One of four.") == GRACEUI_OK;
  }
  step("four a mod", four);
  step("a fifth: full", post(table, mod, "The fifth.") == GRACEUI_FULL);
  const std::string too_long(GRACEUI_NOTICE_TEXT_MAX + 1, 'x');
  GraceUiNotice bad{};
  bad.size = sizeof bad;
  bad.text = too_long.c_str();
  bad.text_length = too_long.size();
  const GraceUiStatus long_status = table->notice(mod, &bad);
  bad.text = nullptr;
  bad.text_length = 5;
  const GraceUiStatus null_text = table->notice(mod, &bad);
  bad.size = 16;
  bad.text = "short";
  const GraceUiStatus small_notice = table->notice(mod, &bad);
  step("refused: not UTF-8, 513 bytes, empty without a key, an unknown tone, a null text, a notice of 16 bytes, none",
       table->notice(mod, nullptr) == GRACEUI_REFUSED && post(table, mod, "bad \xC3", 0, 9) == GRACEUI_REFUSED &&
           long_status == GRACEUI_REFUSED && post(table, mod, "", GRACEUI_TONE_NORMAL, 0) == GRACEUI_REFUSED &&
           post(table, mod, "toned", 7, 9) == GRACEUI_REFUSED && null_text == GRACEUI_REFUSED &&
           small_notice == GRACEUI_REFUSED);
  step("a handle of 0: not connected", post(table, 0, "Nobody's.") == GRACEUI_NOT_CONNECTED);
  table->disconnect(mod);
  step("its notices go with it", grace.notices().empty());
  const std::string many_lines(500, 'w');
  const Connected wordy = connect(table, request_for("abi-wordy"));
  post(table, wordy.answer.consumer, (many_lines.substr(0, 60) + " " + many_lines.substr(0, 400)).c_str());
  for (int i = 0; i < 3; ++i) {
    frame = game.frame_and_read(kAbiColour, PresentWith::present);
  }
  const std::vector<NoticeBox> wordy_boxes = notice_boxes(frame);
  step("a long text: three lines at most", wordy_boxes.size() == 1 && wordy_boxes[0].bottom - wordy_boxes[0].top <= 56 &&
                                               wordy_boxes[0].bottom - wordy_boxes[0].top >= 40);
  table->disconnect(wordy.answer.consumer);
  check(notices_ok, "notices", "%s", steps.c_str());

  // 9. No allocation and no free on the present thread as notices come, change and go during play, after warm-up; and
  // what a frame of three notices costs GraceUI, the GPU idle before each frame.
  const Connected busy = connect(table, request_for("abi-busy"));
  for (int i = 0; i < 60; ++i) {
    game.frame(kAbiColour, PresentWith::present);  // nothing showing: nothing drawn
  }
  grace.reset_counters();
  GraceUiTestState before = grace.state();
  const std::string long_text = "A notice that runs on for three lines or more, so that the box holds as much text as "
                                "it ever shows, and the frame draws as many glyphs as a notice ever draws, which is "
                                "the most room the foreground list reserved at warm-up must hold for it.";
  for (int round = 0; round < 6; ++round) {
    post(table, busy.answer.consumer, long_text.c_str(), static_cast<std::uint32_t>(round % 3), 1);
    post(table, busy.answer.consumer, round % 2 == 0 ? "Short." : "A little longer, this one.", GRACEUI_TONE_NORMAL, 2);
    post(table, busy.answer.consumer, "Brief, and gone soon.", GRACEUI_TONE_WARNING, 3, 150);
    for (int i = 0; i < 50; ++i) {
      game.frame(kAbiColour, PresentWith::present);
      game.wait_idle();
    }
  }
  GraceUiTestState after = grace.state();
  const Costs costs = frame_costs(grace);
  check(after.allocations == 0 && after.frees == 0 && after.frames_drawn - before.frames_drawn >= 200 &&
            costs.own.p99_us <= 1000.0,
        "notice allocations",
        "over %llu frames drawn as notices came, changed and went: %llu allocations and %llu frees on the present "
        "thread; GraceUI's own work median %.1f us, p99 %.1f us (budget: p99 1000 us)",
        static_cast<unsigned long long>(after.frames_drawn - before.frames_drawn),
        static_cast<unsigned long long>(after.allocations), static_cast<unsigned long long>(after.frees),
        costs.own.median_us, costs.own.p99_us);
  table->disconnect(busy.answer.consumer);
  grace.print_log();
}

// Two copies of GraceUI from two folders, each asked for ABI 1's table at the same moment from a thread of its own:
// exactly one answers (D72), and it names the other, once.
void run_copies(const std::wstring& dll) {
  const std::wstring folder = folder_of_this_program() + L"copies";
  CreateDirectoryW(folder.c_str(), nullptr);
  const std::wstring copy = folder + L"\\graceui.dll";
  CopyFileW(dll.c_str(), copy.c_str(), FALSE);
  GraceUi first;
  GraceUi second;
  if (!first.load(dll) || !second.load(copy) || first.module == second.module) {
    fatal("the two copies of graceui.dll did not load as two modules");
  }
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  const void* tables[2] = {};
  const auto ask = [&](const GraceUi& grace, const void*& out) {
    ready.fetch_add(1);
    while (!go.load()) {
      std::this_thread::yield();
    }
    out = grace.request_api(1);
  };
  std::thread a([&] { ask(first, tables[0]); });
  std::thread b([&] { ask(second, tables[1]); });
  while (ready.load() < 2) {
    std::this_thread::yield();
  }
  go.store(true);
  a.join();
  b.join();
  const bool one = (tables[0] != nullptr) != (tables[1] != nullptr);
  const GraceUi& active = tables[0] != nullptr ? first : second;
  const GraceUi& idle = tables[0] != nullptr ? second : first;
  const char* idle_name = tables[0] != nullptr ? "copies\\graceui.dll" : "graceui.dll";
  // Asked again, each answers as before, and nobody is named twice.
  const void* active_again = active.request_api(1);
  const void* idle_again = idle.request_api(1);
  const GraceUiTestState active_state = active.state();
  const GraceUiTestState idle_state = idle.state();
  const std::vector<GraceUiTestNotice> notices = active.notices();
  const bool named = notices.size() == 1 && notices[0].owner == 0 &&
                     std::string_view(notices[0].text).find(std::string(": ") + idle_name) != std::string_view::npos;
  check(one && active_again == tables[tables[0] != nullptr ? 0 : 1] && idle_again == nullptr &&
            idle_state.host == GRACEUI_TEST_HOST_IDLE_COPY && idle_state.init_threads_started == 0 &&
            active_state.init_threads_started == 1 && active_state.copies_named == 1 && named,
        "two copies at once",
        "asked at once from two threads, %s answered a table (%s), the other idles (state %u, %u init threads); the "
        "active one named %u copy in %zu notice, once: \"%s\"",
        one ? "exactly one" : "NOT EXACTLY ONE", tables[0] != nullptr ? "the first folder's" : "the second folder's",
        idle_state.host, idle_state.init_threads_started, active_state.copies_named, notices.size(),
        notices.empty() ? "" : notices[0].text);
  active.print_log();
}

// The example finds a copy of GraceUI under another file name by its scan of the loaded modules (D70).
void run_found_by_scan(const std::wstring& dll) {
  const std::wstring folder = folder_of_this_program() + L"renamed";
  CreateDirectoryW(folder.c_str(), nullptr);
  const std::wstring renamed = folder + L"\\grace-renamed.dll";
  CopyFileW(dll.c_str(), renamed.c_str(), FALSE);
  GraceUi grace;
  if (!grace.load(renamed)) {
    fatal("the renamed copy of graceui.dll did not load");
  }
  if (LoadLibraryW(example_path().c_str()) == nullptr) {
    fatal("hello-notice.dll did not load");
  }
  std::string text;
  std::uint32_t owner = 0;
  const bool seen = present_until(nullptr, kAbiColour, 10000, [&] { return example_connected(grace, text, owner); });
  check(seen && GetModuleHandleW(L"graceui.dll") == nullptr &&
            text.find("found by a scan of the loaded modules at look 1") != std::string::npos &&
            grace.state().init_threads_started == 1,
        "found by scan",
        "with no module named graceui.dll, the example %s through a copy named grace-renamed.dll; its notice: \"%s\"",
        seen ? "connected" : "DID NOT CONNECT", text.c_str());
  grace.print_log();
}

// The example loads GraceUI from GraceUI\graceui.dll in the folder of the game's executable (D70). This process runs
// from a folder of its own, which the main process made: the harness, the example and that copy.
void run_found_by_path() {
  if (LoadLibraryW(example_path().c_str()) == nullptr) {
    fatal("hello-notice.dll did not load");
  }
  HMODULE loaded = nullptr;
  present_until(nullptr, kAbiColour, 10000, [&] { return (loaded = GetModuleHandleW(L"graceui.dll")) != nullptr; });
  GraceUi grace;
  if (loaded == nullptr || !grace.attach(loaded)) {
    fatal("the example did not load GraceUI from its folder");
  }
  std::string text;
  std::uint32_t owner = 0;
  const bool seen = present_until(nullptr, kAbiColour, 10000, [&] { return example_connected(grace, text, owner); });
  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(loaded, path, MAX_PATH);
  const std::wstring expected = folder_of_this_program() + L"GraceUI\\graceui.dll";
  const bool from_folder = length != 0 && CompareStringOrdinal(path, static_cast<int>(length), expected.c_str(),
                                                               static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
  check(seen && from_folder && text.find("found in its own folder at look 1") != std::string::npos,
        "found by path",
        "with no copy loaded, the example loaded GraceUI\\graceui.dll from the game's folder (%s) and %s; its notice: "
        "\"%s\"",
        from_folder ? "yes" : "NO", seen ? "connected" : "DID NOT CONNECT", text.c_str());
  grace.print_log();
}

// A simulated anti-cheat hit by the module clause (D88, D133): a module of the harness's own, whose name the test build
// is told to look for, is loaded before GraceUI's first request.
void run_anti_cheat(const std::wstring& dll) {
  Game game(false);
  const HWND window = game.window();
  cursor_into(window);
  pump_messages();
  GameInput reads;
  reads.open(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_anti_cheat_names(L"grace-harness-shield", nullptr);
  if (LoadLibraryW((folder_of_this_program() + L"grace-harness-shield.dll").c_str()) == nullptr ||
      LoadLibraryW(example_path().c_str()) == nullptr) {
    fatal("grace-harness-shield.dll or hello-notice.dll did not load");
  }
  const GraceUiApi1* table = start_host(game, grace);
  std::string example_text;
  std::uint32_t example_owner = 0;
  const bool example_seen = present_until(&game, kAntiCheatColour, 10000,
                                          [&] { return example_connected(grace, example_text, example_owner); });
  GraceUiTestState state = grace.state();
  GraceUiTestInput input = grace.input();
  std::string hooks_why;
  const bool hooks_ok = hooks_as_expected(grace, 0, hooks_why);
  const std::vector<GraceUiTestNotice> notices = grace.notices();
  const bool own_notice = !notices.empty() && notices[0].owner == 0 && notices[0].tone == GRACEUI_TONE_WARNING &&
                          std::strstr(notices[0].text, "Anti-cheat is running") != nullptr;
  check(state.notice_only == 1 && state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_MODULE && hooks_ok &&
            input.input_off == 1 && own_notice && grace.logged("GraceUI shows notices only this session"),
        "anti-cheat hit",
        "a module named as the check was told: notice-only %u, by the %s clause; input off %u; the hooks as usual: %s; "
        "GraceUI's own notice: \"%s\"",
        state.notice_only, state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_MODULE ? "module" : "WRONG", input.input_off,
        hooks_why.c_str(), notices.empty() ? "" : notices[0].text);

  // connect answers inactive (anti-cheat) with a handle; notice works; nothing else does.
  const Connected banner = connect(table, request_for("abi-banner"));
  const GraceUiStatus banner_notice =
      post(table, banner.answer.consumer, "An invented banner: notices still show.", GRACEUI_TONE_DANGER, 1);
  const std::vector<GraceUiStatus> others = unbuilt_answers(table, banner.answer.consumer);
  check(banner.status == GRACEUI_INACTIVE && banner.answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT &&
            banner.answer.consumer != 0 && banner_notice == GRACEUI_OK && all_answer(others, GRACEUI_INACTIVE) &&
            example_seen && graceui::Api(table).has_cap(GRACEUI_CAP_NOTICES),
        "inactive (anti-cheat)",
        "connect answered %s (reason %u) with %s handle; notice: %s; the other slots: %s; the example connected and "
        "posted \"%s\"",
        status_name(banner.status), banner.answer.reason, banner.answer.consumer != 0 ? "a" : "NO",
        status_name(banner_notice), answers_text(others).c_str(), example_text.c_str());

  // Notices only: the test panel is on, and only the notices are drawn, GraceUI's own first.
  grace.set_panel(1);
  Frame frame;
  std::string why;
  bool only = false;
  int frames_until_seen = 0;
  for (int i = 1; i <= 60 && !only; ++i) {
    frame = game.frame_and_read(kAntiCheatColour, PresentWith::present);
    only = notices_drawn(frame, kAntiCheatColour, {GRACEUI_TONE_WARNING, GRACEUI_TONE_NORMAL, GRACEUI_TONE_DANGER},
                         false, why);
    frames_until_seen = i;
  }
  for (int i = 0; i < 5 && only; ++i) {  // and so it stays: the panel never comes
    frame = game.frame_and_read(kAntiCheatColour, PresentWith::present);
    only = notices_drawn(frame, kAntiCheatColour, {GRACEUI_TONE_WARNING, GRACEUI_TONE_NORMAL, GRACEUI_TONE_DANGER},
                         false, why);
  }
  check(only, "notices only", "the test panel on, in frame %d and the five after it: %s", frames_until_seen,
        why.c_str());

  // Pure pass-through: F5 and every other key reach the game, and no holder can take input.
  g_seen = Seen{};
  cursor_into(window);
  inject_key(VK_F5, kScanF5, true);
  inject_key('A', kScanA, true);
  settle();
  const std::array<std::uint8_t, 256> keys = reads.keys();
  const bool both_read = (keys[DIK_F5] & 0x80) != 0 && (keys[DIK_A] & 0x80) != 0;
  inject_key('A', kScanA, false);
  inject_key(VK_F5, kScanF5, false);
  settle();
  const GraceUiTestAnswer refused = grace.request(1, kPanelKind, GRACEUI_TEST_WANT_ALL);
  frames(game, 3);
  input = grace.input();
  check(both_read && g_seen.key_down[VK_F5] == 1 && g_seen.key_down['A'] == 1 &&
            refused.answer == GRACEUI_TEST_NOT_NOW && refused.why == GRACEUI_TEST_NOT_NOW_NOT_READY &&
            input.holder_owner == 0,
        "pure pass-through",
        "DirectInput read F5 and A %s; the window got F5 %d and A %d times; a holder's request answered %u (why %u), "
        "and nobody holds input",
        both_read ? "down" : "NOT DOWN", g_seen.key_down[VK_F5], g_seen.key_down['A'], refused.answer, refused.why);
  state = grace.state();
  check(state.render_off == 0 && state.frames_drawn > 0, "notice-only render path",
        "on after every check; %llu frames drawn", static_cast<unsigned long long>(state.frames_drawn));
  grace.print_log();
}

// A simulated anti-cheat hit by the parent clause: this process's parent is the harness's main process, whose image
// name the test build is told to look for.
void run_anti_cheat_parent(const std::wstring& dll) {
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_anti_cheat_names(nullptr, L"grace-harness.exe");
  const GraceUiApi1* table = grace.table();
  const GraceUiTestState state = grace.state();
  const Connected banner = table != nullptr ? connect(table, request_for("abi-banner")) : Connected{};
  const GraceUiStatus noticed =
      table != nullptr ? post(table, banner.answer.consumer, "Notices still show.", GRACEUI_TONE_DANGER, 1)
                       : GRACEUI_FAULT;
  check(table != nullptr && state.notice_only == 1 && state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_PARENT &&
            banner.status == GRACEUI_INACTIVE && banner.answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT &&
            noticed == GRACEUI_OK,
        "anti-cheat parent",
        "started by a process named as the check was told: notice-only %u, by the %s clause; connect answered %s "
        "(reason %u); notice: %s",
        state.notice_only, state.anti_cheat == GRACEUI_TEST_ANTI_CHEAT_PARENT ? "parent" : "WRONG",
        status_name(banner.status), banner.answer.reason, status_name(noticed));
  grace.print_log();
}

std::wstring this_program() {
  wchar_t program[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, program, MAX_PATH);
  return std::wstring(program, length);
}

// Runs `program` (this one, unless named) for one scenario, in a process of its own; answers its exit code.
int run_child(const std::wstring& dll, const wchar_t* scenario, std::wstring program = this_program()) {
  std::wstring command = L"\"" + program + L"\" --dll \"" + dll + L"\" --scenario " + scenario;
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                      &process)) {
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
  } else if (scenario == L"abi") {
    std::printf("-- scenario: ABI 1's table, connect, notices and the example mod\n");
    run_abi(dll);
  } else if (scenario == L"copies") {
    std::printf("-- scenario: two copies asked at once\n");
    run_copies(dll);
  } else if (scenario == L"found-by-scan") {
    std::printf("-- scenario: the example finds a renamed copy\n");
    run_found_by_scan(dll);
  } else if (scenario == L"found-by-path") {
    std::printf("-- scenario: the example loads GraceUI from the game's folder\n");
    run_found_by_path();
  } else if (scenario == L"anti-cheat") {
    std::printf("-- scenario: an anti-cheat hit by the module clause\n");
    run_anti_cheat(dll);
  } else if (scenario == L"anti-cheat-parent") {
    std::printf("-- scenario: an anti-cheat hit by the parent clause\n");
    run_anti_cheat_parent(dll);
  } else {
    std::printf("-- scenario: one direct queue\n");
    run_main(dll);
    if (scenario.empty()) {
      const auto run = [&](const wchar_t* child_scenario, const std::wstring& child_dll, const std::wstring& program) {
        std::fflush(stdout);
        const int child = run_child(child_dll, child_scenario, program);
        char name[64];
        std::snprintf(name, sizeof name, "%ls scenario", child_scenario);
        check(child == 0, name, "its process ended with %d", child);
      };
      for (const wchar_t* child_scenario : {L"two-queues", L"input", L"input-ansi", L"abi", L"copies",
                                            L"found-by-scan", L"anti-cheat", L"anti-cheat-parent"}) {
        run(child_scenario, dll, this_program());
      }
      // found-by-path runs from a folder of its own, as a game's folder: the harness, the example, and GraceUI in
      // GraceUI\graceui.dll, which nothing else loads.
      const std::wstring folder = folder_of_this_program() + L"by-path";
      CreateDirectoryW(folder.c_str(), nullptr);
      CreateDirectoryW((folder + L"\\GraceUI").c_str(), nullptr);
      const std::wstring program = folder + L"\\grace-harness.exe";
      const std::wstring copy = folder + L"\\GraceUI\\graceui.dll";
      if (!CopyFileW(this_program().c_str(), program.c_str(), FALSE) ||
          !CopyFileW(example_path().c_str(), (folder + L"\\hello-notice.dll").c_str(), FALSE) ||
          !CopyFileW(dll.c_str(), copy.c_str(), FALSE)) {
        check(false, "found-by-path scenario", "its folder could not be made (error %lu)",
              static_cast<unsigned long>(GetLastError()));
      } else {
        run(L"found-by-path", copy, program);
      }
    }
  }
  std::printf("-- %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
