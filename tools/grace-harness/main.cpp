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
// each clause) each need a process whose first GraceUI_RequestApi is theirs; "panels" (S10.4.c: panels and draw lists,
// textures, faces and glyph tables, input through ABI 1 and providers, with the second example mod) one whose
// warm-up comes after its mods have registered their faces.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>

#include <d3d12.h>
#include <dinput.h>
#include <dxgi1_4.h>
#include <imm.h>
#include <xinput.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "client.h"  // the server suite's test client: the "pages" scenario is a browser too
#include "grace/pages/builtin.h"
#include "grace/serve/codec.h"
#include "graceui_sys.hpp"
#include "graceui_test.h"
#include "hello_debug_view.h"
#include "grace_spike.h"
#include "hello_panel.h"

namespace {

// The GraceUI this build is (the project's version), for the floors and the words that name it.
constexpr std::uint32_t kBuildVersion = GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR, GRACE_BUILD_PATCH);
constexpr std::uint32_t kNextPatch = GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR, GRACE_BUILD_PATCH + 1);
constexpr std::uint32_t kNextMinor = GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR + 1, 0);
constexpr std::uint32_t kNextMajor = GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR + 1, 0, 0);

std::string version_words(std::uint32_t packed) {
  return std::to_string(GRACEUI_VERSION_MAJOR(packed)) + "." + std::to_string(GRACEUI_VERSION_MINOR(packed)) + "." +
         std::to_string(GRACEUI_VERSION_PATCH(packed));
}

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
bool g_keep_settings = false;  // the settings-read scenarios: GraceUI starts with what an earlier session kept (D179)

// GraceUI's settings file beside `dll`: user\settings.json in its folder (D84, D179).
std::wstring settings_file_of(const std::wstring& dll) {
  const std::size_t slash = dll.find_last_of(L"\\/");
  return (slash == std::wstring::npos ? std::wstring() : dll.substr(0, slash + 1)) + L"user\\settings.json";
}

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
  // `width` by `height`: the window's client area and the swap chain's buffers (640 by 360 unless a scenario asks).
  explicit Game(bool second_queue, UINT width = 640, UINT height = 360) : width_(width), height_(height) {
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
  explicit Game(Game& shared)
      : factory_(shared.factory_), device_(shared.device_), queue_(shared.queue_), width_(640), height_(360) {
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
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
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
    desc.Width = width_;
    desc.Height = height_;
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
    if (recentre_) {
      POINT centre{static_cast<LONG>(width_ / 2), static_cast<LONG>(height_ / 2)};
      ClientToScreen(window_, &centre);
      SetCursorPos(centre.x, centre.y);
    }
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

  // A game that turns its camera by the mouse (D232's case): it reads the mouse as raw input, and puts the cursor back
  // at its window's centre before every frame, whatever holds input.
  void recentre() {
    RAWINPUTDEVICE mouse{};
    mouse.usUsagePage = 0x01;  // generic desktop
    mouse.usUsage = 0x02;      // mouse
    mouse.hwndTarget = window_;
    if (!RegisterRawInputDevices(&mouse, 1, sizeof mouse)) {
      fatal("RegisterRawInputDevices for the mouse");
    }
    recentre_ = true;
  }
  ID3D12Device* device() const { return device_; }

 private:
  static constexpr UINT64 kReadbackBytes = 1920ull * 1088 * 4;  // a 1920 x 1080 frame, its rows aligned

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
  bool recentre_ = false;
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
  decltype(&GraceUI_Test_Pad) pad = nullptr;
  decltype(&GraceUI_Test_PadRead) pad_read = nullptr;
  decltype(&GraceUI_Test_GetInput) get_input = nullptr;
  decltype(&GraceUI_Test_GetChars) get_chars = nullptr;
  decltype(&GraceUI_Test_GetInputCosts) input_costs = nullptr;
  decltype(&GraceUI_Test_SetFirstRequestDelay) set_first_request_delay = nullptr;
  decltype(&GraceUI_Test_TurnOffRender) turn_off_render = nullptr;
  decltype(&GraceUI_Test_SetAtlasLimit) set_atlas_limit = nullptr;
  decltype(&GraceUI_Test_GetPanelPublished) panel_published = nullptr;
  decltype(&GraceUI_Test_GetMenu) get_menu = nullptr;
  decltype(&GraceUI_Test_GetSetting) get_setting = nullptr;
  decltype(&GraceUI_Test_GetTopic) get_topic = nullptr;
  decltype(&GraceUI_Test_GetMenuBounds) get_menu_bounds = nullptr;
  decltype(&GraceUI_Test_SetAntiCheatRecheck) set_anti_cheat_recheck = nullptr;
  decltype(&GraceUI_Test_MenuShows) menu_shows = nullptr;
  decltype(&GraceUI_Test_InitStep) init_step = nullptr;
  decltype(&GraceUI_Test_GetServer) get_server = nullptr;
  decltype(&GraceUI_Test_GetHeapGuard) get_heap_guard = nullptr;      // D235
  decltype(&GraceUI_Test_HeapGuardProbe) heap_guard_probe = nullptr;
  decltype(&GraceUI_Test_GetPairingCode) get_pairing_code = nullptr;
  decltype(&GraceUI_Test_SetNetworkAddresses) set_network_addresses = nullptr;

  bool load(const std::wstring& path) {
    if (!g_keep_settings) {  // every scenario starts from GraceUI's defaults, unless it reads what another kept (D179)
      DeleteFileW(settings_file_of(path).c_str());
    }
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
    pad = function_of<decltype(pad)>(module, "GraceUI_Test_Pad");
    pad_read = function_of<decltype(pad_read)>(module, "GraceUI_Test_PadRead");
    get_input = function_of<decltype(get_input)>(module, "GraceUI_Test_GetInput");
    get_chars = function_of<decltype(get_chars)>(module, "GraceUI_Test_GetChars");
    input_costs = function_of<decltype(input_costs)>(module, "GraceUI_Test_GetInputCosts");
    set_anti_cheat_names = function_of<decltype(set_anti_cheat_names)>(module, "GraceUI_Test_SetAntiCheatNames");
    get_notice = function_of<decltype(get_notice)>(module, "GraceUI_Test_GetNotice");
    clear_notices = function_of<decltype(clear_notices)>(module, "GraceUI_Test_ClearNotices");
    get_consumer = function_of<decltype(get_consumer)>(module, "GraceUI_Test_GetConsumer");
    set_first_request_delay =
        function_of<decltype(set_first_request_delay)>(module, "GraceUI_Test_SetFirstRequestDelay");
    turn_off_render = function_of<decltype(turn_off_render)>(module, "GraceUI_Test_TurnOffRender");
    set_atlas_limit = function_of<decltype(set_atlas_limit)>(module, "GraceUI_Test_SetAtlasLimit");
    panel_published = function_of<decltype(panel_published)>(module, "GraceUI_Test_GetPanelPublished");
    get_menu = function_of<decltype(get_menu)>(module, "GraceUI_Test_GetMenu");
    get_setting = function_of<decltype(get_setting)>(module, "GraceUI_Test_GetSetting");
    get_topic = function_of<decltype(get_topic)>(module, "GraceUI_Test_GetTopic");
    get_menu_bounds = function_of<decltype(get_menu_bounds)>(module, "GraceUI_Test_GetMenuBounds");
    set_anti_cheat_recheck = function_of<decltype(set_anti_cheat_recheck)>(module, "GraceUI_Test_SetAntiCheatRecheck");
    menu_shows = function_of<decltype(menu_shows)>(module, "GraceUI_Test_MenuShows");
    init_step = function_of<decltype(init_step)>(module, "GraceUI_Test_InitStep");
    get_server = function_of<decltype(get_server)>(module, "GraceUI_Test_GetServer");
    get_heap_guard = function_of<decltype(get_heap_guard)>(module, "GraceUI_Test_GetHeapGuard");
    heap_guard_probe = function_of<decltype(heap_guard_probe)>(module, "GraceUI_Test_HeapGuardProbe");
    get_pairing_code = function_of<decltype(get_pairing_code)>(module, "GraceUI_Test_GetPairingCode");
    set_network_addresses = function_of<decltype(set_network_addresses)>(module, "GraceUI_Test_SetNetworkAddresses");
    return get_server != nullptr && get_heap_guard != nullptr && heap_guard_probe != nullptr && get_pairing_code != nullptr && set_network_addresses != nullptr && get_menu != nullptr && get_setting != nullptr && get_topic != nullptr &&
           get_menu_bounds != nullptr && set_anti_cheat_recheck != nullptr && menu_shows != nullptr && init_step != nullptr &&
           panel_published != nullptr && set_first_request_delay != nullptr && turn_off_render != nullptr && set_atlas_limit != nullptr &&
           version != nullptr && request_api != nullptr && set_panel != nullptr && get_state != nullptr &&
           reset_counters != nullptr && frame_costs != nullptr && log_line != nullptr && hook_name != nullptr &&
           request_input != nullptr && release_input != nullptr && set_world != nullptr && set_caret != nullptr &&
           set_code_page != nullptr && pad != nullptr && pad_read != nullptr && get_input != nullptr && get_chars != nullptr && input_costs != nullptr &&
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

  GraceUiTestServer server() const {
    GraceUiTestServer out{};
    out.size = sizeof out;
    if (get_server(&out) == 0) {
      fatal("GraceUI_Test_GetServer refused");
    }
    return out;
  }

  GraceUiTestHeapGuard heap_guard() const {
    GraceUiTestHeapGuard out{};
    out.size = sizeof out;
    if (get_heap_guard(&out) == 0) {
      fatal("GraceUI_Test_GetHeapGuard refused");
    }
    return out;
  }

  GraceUiTestMenu menu() const {
    GraceUiTestMenu out{};
    out.size = sizeof out;
    if (get_menu(&out) == 0) {
      fatal("GraceUI_Test_GetMenu refused");
    }
    return out;
  }

  // A setting of GraceUI's own space, as JSON text, or "" if the menu has not opened yet.
  std::string setting(const char* key) const {
    char text[256];
    return get_setting(key, text, sizeof text) != 0 ? std::string(text) : std::string();
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

  // The code the pairing sheet shows (G9), or "" while none shows.
  std::string pairing_code() const {
    char code[16] = {};
    return get_pairing_code(code, sizeof code) > 0 ? std::string(code) : std::string();
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
                "DLLs loaded), the window procedure %u; %s: %s; the init's step %u",
                state.hooks_installed, state.hook_install_runs, input.render_hooks, input.dinput_hooks,
                input.xinput_hooks, xinput, input.window_filter, published ? "all in D102's list" : "NOT all in D102's list",
                names.c_str(), grace.init_step());
  why = text;
  return published && state.hook_install_runs == 1 && grace.init_step() == 7 && input.render_hooks == 5 &&
         input.dinput_hooks >= 1 &&
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

// ---- Frame-cost windows (D154). A frame's cost is wall time: under Wine no clock measures the present thread's own
// time per frame (QueryThreadCycleTime fails, GetThreadTimes counts in 10 ms ticks, and the kernel's per-thread
// schedstat updates its runtime a tick late and cannot see a virtual machine's stolen time). Interference, from other
// processes or from the host, can only lengthen a frame's wall time, never shorten it: so a budget is held against the
// least p99 of up to three windows of the same frames, each as long as the one window was. A cost of GraceUI's own shows
// in every window, a busy spell of the machine's in one; the budget and the percentile are unchanged, every window's
// figures are printed, and what a window must do besides (frames drawn, no allocation) holds in every one.
constexpr int kCostWindows = 3;

struct CostWindow {
  Costs costs;
  GraceUiTestState before{};
  GraceUiTestState after{};
};

struct CostWindows {
  std::vector<CostWindow> runs;

  const CostWindow& best() const {
    return *std::min_element(runs.begin(), runs.end(), [](const CostWindow& a, const CostWindow& b) {
      return a.costs.own.p99_us < b.costs.own.p99_us;
    });
  }
  // Allocations and frees on the present thread, in all of them.
  std::uint64_t allocations() const {
    std::uint64_t total = 0;
    for (const CostWindow& run : runs) {
      total += run.after.allocations + run.after.frees;
    }
    return total;
  }
  template <class Each>
  bool every(Each&& each) const {
    return std::all_of(runs.begin(), runs.end(), each);
  }
  // Each window's own work, median / p99 in us.
  std::string text() const {
    std::string out;
    char part[64];
    for (const CostWindow& run : runs) {
      std::snprintf(part, sizeof part, "%s%.1f / %.1f", out.empty() ? "" : ", ", run.costs.own.median_us,
                    run.costs.own.p99_us);
      out += part;
    }
    return out;
  }
};

// Runs `run` (one window's frames) with the counters reset before it, until a window's own work is within
// `budget_us` at p99, at most kCostWindows times.
template <class Run>
CostWindows cost_windows(const GraceUi& grace, double budget_us, Run&& run) {
  CostWindows windows;
  for (int i = 0; i < kCostWindows; ++i) {
    grace.reset_counters();
    CostWindow window;
    window.before = grace.state();
    run();
    window.after = grace.state();
    window.costs = frame_costs(grace);
    windows.runs.push_back(window);
    if (window.costs.own.p99_us <= budget_us) {
      break;
    }
  }
  return windows;
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
  check(version == kBuildVersion, "version",
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
  // The atlas is baked on GraceUI's own thread, and the menu's faces make it larger (D167): wait for it, at most 3 s.
  for (int i = 0; i < 150 && (state.renderer_ready == 0 || state.atlas_ready == 0); ++i) {
    game.frame(kUntouched, PresentWith::present);
    Sleep(20);
    state = grace.state();
  }
  check(state.queue_method != GRACEUI_TEST_QUEUE_NONE && state.renderer_ready == 1 && state.atlas_ready == 1,
        "warm-up", "present queue: %s; renderer ready %u; atlas %ux%u uploaded %u",
        method_name(state.queue_method), state.renderer_ready, state.atlas_width, state.atlas_height,
        state.atlas_ready);
  check(state.present_prologue == GRACEUI_TEST_PROLOGUE_PLAIN, "present prologue",
        "with no other overlay, Present's first bytes held no jump at install (D86's cue: %u, 1 plain)",
        state.present_prologue);

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

  const CostWindows drawing_windows = cost_windows(grace, 1000.0, [&] {
    for (int i = 0; i < 600; ++i) {
      game.frame(kPresent, PresentWith::present);
      game.wait_idle();
    }
  });
  const Costs& drawing = drawing_windows.best().costs;
  const bool all_drawn = drawing_windows.every([](const CostWindow& w) {
    return w.costs.count >= 600 && w.after.frames_drawn - w.before.frames_drawn == 600;
  });
  check(all_drawn && drawing.own.p99_us <= 1000.0, "frame cost",
        "drawing the panel, the GPU idle before each frame, in %zu window(s) of 600 frames, all drawn: %s; GraceUI's "
        "own work median / p99 in us: %s (budget: p99 1000 us, the least window's); in that window the whole detour "
        "median %.1f us, p99 %.1f us, of which the runtime's submission calls median %.1f us, p99 %.1f us",
        drawing_windows.runs.size(), all_drawn ? "yes" : "NO", drawing_windows.text().c_str(), drawing.total.median_us,
        drawing.total.p99_us, drawing.submit.median_us, drawing.submit.p99_us);
  check(pipelined_allocations == 0 && pipelined_frees == 0 && drawing_windows.allocations() == 0, "allocations",
        "on the present thread in the detours, after warm-up: %llu allocations and %llu frees over the 600 pipelined "
        "frames, %llu of either over the %zu x 600 others",
        static_cast<unsigned long long>(pipelined_allocations), static_cast<unsigned long long>(pipelined_frees),
        static_cast<unsigned long long>(drawing_windows.allocations()), drawing_windows.runs.size());

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
  // The queue named, warm-up bakes the atlas on GraceUI's own thread (the menu's faces among its faces, D167): the
  // panel is drawn once it is on the GPU.
  for (int i = 0; i < 150 && grace.state().atlas_ready == 0; ++i) {
    game.frame(kPresent, PresentWith::present);
    Sleep(20);
  }
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

// Frames until the atlas is on the GPU, at most 3 s: a holder is granted, and the menu opens, only once the overlay can
// draw, and warm-up's bake takes its time on GraceUI's own thread (the menu's eleven faces among its faces, D167).
void until_atlas(Game& game, const GraceUi& grace) {
  for (int i = 0; i < 150 && grace.state().atlas_ready == 0; ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
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

const char* status_name(GraceUiStatus status);

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
  until_atlas(game, grace);
  std::string why;
  bool ok = hooks_as_expected(grace, static_cast<std::uint32_t>(pads.size()), why);
  check(ok, "input hooks", "%s", why.c_str());

  // 1. No holder: the game's reads pass untouched, but for the menu key, which never reaches the game (§5.1): it opens
  // GraceUI's menu, and a second press closes it (D167).
  g_seen = Seen{};
  inject_key('A', kScanA, true);
  settle();
  std::array<std::uint8_t, 256> keys = reads.keys();
  const bool key_passes = (keys[DIK_A] & 0x80) != 0;
  inject_key('A', kScanA, false);
  settle();
  reads.keys();
  inject_key(VK_F5, kScanF5, true);
  settle();
  frames(game, 2);
  keys = reads.keys();
  const bool menu_opened = grace.menu().open == 1;
  bool menu_key_hidden = keys[DIK_F5] == 0;
  inject_key(VK_F5, kScanF5, false);
  settle();
  inject_key(VK_F5, kScanF5, true);
  settle();
  menu_key_hidden = menu_key_hidden && reads.keys()[DIK_F5] == 0;
  inject_key(VK_F5, kScanF5, false);
  settle();
  frames(game, 2);
  const bool menu_closed = grace.menu().open == 0;
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
  check(key_passes && menu_key_hidden && menu_opened && menu_closed && released && move.lX != 0 &&
            button.rgbButtons[0] == 0x80 && pads_untouched && g_seen.key_down['A'] == 1 &&
            g_seen.key_down[VK_F5] == 0 && g_seen.key_up[VK_F5] == 0,
        "pass-through",
        "with no holder: A read %s; F5 %s the menu and again %s it, read %s and its messages kept from the game (%d "
        "down, %d up), A's messages the game's (%d down); mouse moved %ld,%ld and its button read 0x%02x; "
        "XInputGetState %s",
        key_passes ? "down" : "UP", menu_opened ? "opened" : "DID NOT OPEN", menu_closed ? "closed" : "DID NOT CLOSE",
        menu_key_hidden ? "up" : "DOWN", g_seen.key_down[VK_F5], g_seen.key_up[VK_F5],
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
  // The spikes' counters (D175) say the same: nothing passed to the game while held, and DirectInput's reads masked.
  check(input.passed_presses == 0 && input.passed_characters == 0 && input.passed_mouse == 0 &&
            input.passed_raw == 0 && input.masked_keyboard_held >= 1 && input.masked_mouse_held >= 1,
        "spike counters: held",
        "passed to the game while held: %u presses, %u characters, %u mouse, %u raw; DirectInput reads masked: %u of "
        "the keyboard, %u of the mouse",
        input.passed_presses, input.passed_characters, input.passed_mouse, input.passed_raw, input.masked_keyboard_held,
        input.masked_mouse_held);
  const GraceUiTestInput before_close = input;

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
  check(input.masked_keyboard_free > before_close.masked_keyboard_free &&
            input.masked_mouse_free > before_close.masked_mouse_free && input.passed_presses == 0,
        "spike counters: closing press",
        "after the close, DirectInput reads masked until the press was released: %u of the keyboard, %u of the mouse",
        input.masked_keyboard_free - before_close.masked_keyboard_free,
        input.masked_mouse_free - before_close.masked_mouse_free);
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
  // The held figures in windows (D154): each kept at its least p99 across the windows run, the frames' and the input's.
  Spread held_window{1e9, 1e9};
  Spread held_dinput{1e9, 1e9};
  Spread held_xinput{1e9, 1e9};
  const auto least = [](Spread& kept, const Spread& now) {
    if (now.p99_us < kept.p99_us) {
      kept = now;
    }
  };
  bool held_events_kept = true;
  const CostWindows held_windows = cost_windows(grace, 1000.0, [&] {
    for (int i = 0; i < 300; ++i) {
      busy_frame(i);
    }
    least(held_window, spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_WINDOW)));
    least(held_dinput, spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_DINPUT)));
    least(held_xinput, spread_ns(input_costs_of(grace, GRACEUI_TEST_COST_XINPUT)));
    held_events_kept = held_events_kept && grace.input().events_dropped == 0;
  });
  const GraceUiTestState busy = held_windows.runs.back().after;
  const GraceUiTestState busy_before = held_windows.runs.back().before;
  input = grace.input();
  const Costs& held_frames = held_windows.best().costs;
  grace.release_input(1);
  frames(game, 2);
  check(idle_state.allocations == 0 && idle_state.frees == 0 && held_windows.allocations() == 0 && held_events_kept,
        "input allocations",
        "in the filter, the detours and the present thread: %llu allocations and %llu frees over 1000 reads and "
        "messages with no holder; %llu of either over %zu x %llu frames with a holder fed every frame; events "
        "dropped: %s",
        static_cast<unsigned long long>(idle_state.allocations), static_cast<unsigned long long>(idle_state.frees),
        static_cast<unsigned long long>(held_windows.allocations()), held_windows.runs.size(),
        static_cast<unsigned long long>(busy.frames_drawn - busy_before.frames_drawn),
        held_events_kept ? "none" : "SOME");
  check(idle_window.p99_us <= 50 && idle_dinput.p99_us <= 50 && idle_xinput.p99_us <= 50 && held_window.p99_us <= 50 &&
            held_dinput.p99_us <= 50 && held_xinput.p99_us <= 50,
        "input cost",
        "GraceUI's own work (budget: p99 50 us each, EST), median / p99 in us: with no holder, the window filter "
        "%.2f / %.2f per message, GetDeviceState %.2f / %.2f per read, XInputGetState %.2f / %.2f; with a holder, "
        "%.2f / %.2f, %.2f / %.2f, %.2f / %.2f",
        idle_window.median_us, idle_window.p99_us, idle_dinput.median_us, idle_dinput.p99_us, idle_xinput.median_us,
        idle_xinput.p99_us, held_window.median_us, held_window.p99_us, held_dinput.median_us, held_dinput.p99_us,
        held_xinput.median_us, held_xinput.p99_us);
  check(held_windows.every([](const CostWindow& w) { return w.costs.count >= 300; }) &&
            held_frames.own.p99_us <= 1000.0,
        "holder frame cost",
        "with a holder fed every frame and its cursor drawn, in %zu window(s) of 300 frames: GraceUI's own work median "
        "/ p99 in us: %s (budget: p99 1000 us, the least window's); in that window the whole detour median %.1f us, "
        "p99 %.1f us",
        held_windows.runs.size(), held_windows.text().c_str(), held_frames.total.median_us, held_frames.total.p99_us);

  // Hotkeys through ABI 1 (D155): two mods; a bare F11 and Ctrl+K fire once each, through poll, and never reach the
  // game, by DirectInput or as messages; K alone does; a taken default registers unbound; nothing fires while a holder
  // has input; a mod's hotkeys go with it.
  {
    const graceui::Api api(grace.table());
    GraceUiConnectAnswer first_answer{};
    GraceUiConnectAnswer second_answer{};
    api.connect(graceui::Mod{"harness-keys", "Harness keys", GRACEUI_VERSION_PACK(1, 0, 0), 0, "hotkeys"},
                first_answer);
    api.connect(graceui::Mod{"harness-keys-two", "Harness keys two", GRACEUI_VERSION_PACK(1, 0, 0), 0, "hotkeys"},
                second_answer);
    const GraceUiConsumer first = first_answer.consumer;
    const GraceUiConsumer second = second_answer.consumer;
    std::uint32_t f11 = 0;
    std::uint32_t chat = 0;
    std::uint32_t taken = 0;
    std::uint32_t unused = 0;
    const GraceUiStatus f11_status = api.register_hotkey(first, "debug-view", "Debug view", VK_F11, 0, f11);
    const GraceUiStatus chat_status = api.register_hotkey(first, "chat", "Chat", 'K', GRACEUI_MOD_CTRL, chat);
    const GraceUiStatus taken_status = api.register_hotkey(second, "debug-view", "Debug view", VK_F11, 0, taken);
    const GraceUiStatus escape_status = api.register_hotkey(first, "escape", "Escape", VK_ESCAPE, 0, unused);
    const GraceUiStatus again_status = api.register_hotkey(first, "chat", "Chat", 'L', 0, unused);
    const GraceUiStatus bad_action = api.register_hotkey(first, "Chat Two", "Chat", 'L', 0, unused);
    GraceUiHotkeyDesc chord{};
    chord.size = sizeof chord;
    chord.pad_chord = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START;
    chord.action = "pad";
    chord.action_length = 3;
    chord.name = "Pad";
    chord.name_length = 3;
    const GraceUiStatus chord_status = grace.table()->register_hotkey(first, &chord, &unused);
    const bool unbound_logged = grace.logged("hotkey debug-view is unbound");
    const auto poll_all = [&](GraceUiConsumer mod, std::uint32_t& id, std::uint32_t& count) {
      std::uint32_t events = 0;
      GraceUiEvent event{};
      while (api.poll(mod, event) == GRACEUI_OK && event.kind == GRACEUI_EVENT_HOTKEY && events < 16) {
        id = event.hotkey_id;
        count += event.count;
        ++events;
      }
      return events;
    };
    std::uint32_t id = 0;
    std::uint32_t count = 0;
    std::uint32_t second_id = 0;
    std::uint32_t second_count = 0;
    const std::uint32_t nothing_yet = poll_all(first, id, count);

    // F11, bound with no modifier.
    cursor_into(window);
    reads.keys();
    g_seen = Seen{};
    inject_key(VK_F11, 0x57, true);
    settle();
    const bool f11_read = (reads.keys()[DIK_F11] & 0x80) != 0;
    inject_key(VK_F11, 0x57, false);
    settle();
    reads.keys();
    id = 0;
    count = 0;
    const std::uint32_t f11_events = poll_all(first, id, count);
    const bool f11_fired = f11_events == 1 && id == f11 && count == 1;
    const std::uint32_t second_events = poll_all(second, second_id, second_count);
    const int f11_messages = g_seen.key_down[VK_F11] + g_seen.key_up[VK_F11];

    // Ctrl+K: the hotkey's; K alone: the game's.
    g_seen = Seen{};
    inject_key(VK_CONTROL, 0x1D, true);
    settle();
    inject_key('K', kScanK, true);
    settle();
    const std::array<std::uint8_t, 256> chord_keys = reads.keys();
    const bool k_hidden = chord_keys[DIK_K] == 0 && (chord_keys[DIK_LCONTROL] & 0x80) != 0;
    inject_key('K', kScanK, false);
    settle();
    inject_key(VK_CONTROL, 0x1D, false);
    settle();
    reads.keys();
    id = 0;
    count = 0;
    const bool chat_fired = poll_all(first, id, count) == 1 && id == chat && count == 1;
    const int k_with_ctrl = g_seen.key_down['K'];
    g_seen = Seen{};
    inject_key('K', kScanK, true);
    settle();
    const bool k_read = (reads.keys()[DIK_K] & 0x80) != 0;
    inject_key('K', kScanK, false);
    settle();
    reads.keys();
    const int k_alone = g_seen.key_down['K'];
    id = 0;
    count = 0;
    const std::uint32_t after_k = poll_all(first, id, count);

    // A holder has input: hotkeys stand aside.
    const bool held = take(grace, game, 7, kPanelKind);
    inject_key(VK_F11, 0x57, true);
    settle();
    inject_key(VK_F11, 0x57, false);
    settle();
    reads.keys();
    grace.release_input(7);
    frames(game, 2);
    id = 0;
    count = 0;
    const std::uint32_t while_held = poll_all(first, id, count);

    // The first mod disconnects: its F11 goes, and F11 is the game's again (the second's stays unbound).
    api.disconnect(first);
    cursor_into(window);
    reads.keys();
    g_seen = Seen{};
    inject_key(VK_F11, 0x57, true);
    settle();
    const bool f11_free = (reads.keys()[DIK_F11] & 0x80) != 0;
    inject_key(VK_F11, 0x57, false);
    settle();
    reads.keys();
    const int f11_free_messages = g_seen.key_down[VK_F11];
    GraceUiEvent gone{};
    const GraceUiStatus gone_status = api.poll(first, gone);
    second_count = 0;
    const std::uint32_t second_after = poll_all(second, second_id, second_count);
    api.disconnect(second);
    check(first_answer.status == GRACEUI_OK && second_answer.status == GRACEUI_OK && api.has_cap("hotkeys") &&
              f11_status == GRACEUI_OK && chat_status == GRACEUI_OK && taken_status == GRACEUI_UNBOUND && taken != 0 &&
              unbound_logged && escape_status == GRACEUI_REFUSED && again_status == GRACEUI_REFUSED &&
              bad_action == GRACEUI_REFUSED && chord_status == GRACEUI_REFUSED && nothing_yet == 0 && !f11_read &&
              f11_fired && second_events == 0 && f11_messages == 0 && k_hidden && chat_fired && k_with_ctrl == 0 &&
              k_read && k_alone >= 1 && after_k == 0 && held && while_held == 0 && f11_free &&
              f11_free_messages >= 1 && gone_status == GRACEUI_NOT_CONNECTED && second_after == 0,
          "hotkeys",
          "two mods connected (%s, %s); F11 %s, Ctrl+K %s, F11 again by the second %s (unbound, logged: %s), Escape "
          "%s, an action again %s, an action not an id %s, a pad chord as a mask %s; F11 pressed: DirectInput %s, %u event(s) "
          "(%s), the second mod %u, its messages reaching the game %d; Ctrl+K: K %s from DirectInput, the hotkey %s, "
          "K's messages reaching the game %d; K alone: read %s, messages %d, events %u; with a holder (%s): %u "
          "event(s); after the first's disconnect: F11 read %s, messages %d, its poll %s, the second's events %u",
          status_name(first_answer.status), status_name(second_answer.status), status_name(f11_status),
          status_name(chat_status), status_name(taken_status), unbound_logged ? "yes" : "NO",
          status_name(escape_status), status_name(again_status), status_name(bad_action), status_name(chord_status),
          f11_read ? "SAW IT" : "did not see it", f11_events, f11_fired ? "fired once" : "NOT AS EXPECTED",
          second_events, f11_messages, k_hidden ? "hidden" : "NOT HIDDEN", chat_fired ? "fired once" : "DID NOT FIRE",
          k_with_ctrl, k_read ? "yes" : "NO", k_alone, after_k, held ? "granted" : "NOT GRANTED", while_held,
          f11_free ? "yes" : "NO", f11_free_messages, status_name(gone_status), second_after);
  }

  const GraceUiTestState state = grace.state();
  check(state.render_off == 0 && grace.input().input_off == 0 && state.hook_install_runs == 1, "input path",
        "the render path and input still on after every check; %llu presents, %llu frames drawn, %llu fed by a holder",
        static_cast<unsigned long long>(state.presents), static_cast<unsigned long long>(state.frames_drawn),
        static_cast<unsigned long long>(grace.input().frames_fed));

  // Last, as it is for the session: the render path turned off, as a fault turns it off (the review's L4). Nothing can
  // open a menu now, so input goes off with it: F5 reaches the game again, through DirectInput and as messages, and a
  // request is not now.
  take(grace, game, 7, kPanelKind);
  grace.turn_off_render();
  frames(game, 3);
  cursor_into(window);
  reads.keys();
  g_seen = Seen{};
  inject_key(VK_F5, kScanF5, true);
  settle();
  const bool f5_read = (reads.keys()[DIK_F5] & 0x80) != 0;
  inject_key(VK_F5, kScanF5, false);
  settle();
  reads.keys();
  const GraceUiTestAnswer after_off = grace.request(1, kPanelKind, GRACEUI_TEST_WANT_ALL);
  const GraceUiTestState off_state = grace.state();
  const GraceUiTestInput off_input = grace.input();
  check(off_state.render_off == 1 && off_input.input_off == 1 && off_input.holder_owner == 0 &&
            off_input.last_close_reason == GRACEUI_TEST_CLOSED_FAULT && f5_read && g_seen.key_down[VK_F5] >= 1 &&
            after_off.answer == GRACEUI_TEST_NOT_NOW,
        "render off",
        "the render path off (%u), input off with it (%u), its holder ended (%s); F5 read by DirectInput: %s, its "
        "messages reaching the game: %d; a request %s",
        off_state.render_off, off_input.input_off, reason_name(off_input.last_close_reason), f5_read ? "yes" : "no",
        g_seen.key_down[VK_F5], after_off.answer == GRACEUI_TEST_NOT_NOW ? "not now" : "answered otherwise");
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
  until_atlas(game, grace);
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
  // UTF-8 as the ANSI code page (Windows' "use Unicode UTF-8" setting, the review's L13): each character's bytes in
  // messages of their own, joined: é in two, € in three, U+1F642 in four, and a sequence cut short by an 'x'.
  grace.set_code_page(65001);
  for (const unsigned byte : {0xC3u, 0xA9u, 0xE2u, 0x82u, 0xACu, 0xF0u, 0x9Fu, 0x99u, 0x82u, 0xE2u, 0x82u, 0x78u}) {
    PostMessageA(window, WM_CHAR, byte, 1);
  }
  frames(game, 4);
  grace.set_code_page(0);
  const std::vector<std::uint32_t> got = chars_of(grace);
  const std::vector<std::uint32_t> want = {'a', 0xE9, 0x3042, 0x3044, 0x1F600, 0xE9, 0x20AC, 0x1F642, 0xFFFD, 'x'};
  input = grace.input();
  check(ansi && granted && got == want && g_seen.chars == 0, "ANSI characters",
        "an ANSI window, filtered as one (%s); in Windows-1252, Shift-JIS and UTF-8, Dear ImGui received %s (wanted "
        "%s); character messages reaching the game: %d",
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
    case GRACEUI_NOT_READY: return "not_ready";
    case GRACEUI_TOO_LATE: return "too_late";
    case GRACEUI_UNBOUND: return "unbound";
    default: return "UNKNOWN";
  }
}

// What the sixteen slots after connect, disconnect and notice answer for `consumer`, in the table's order, each called
// with empty arguments: for a handle that names nobody, not connected; for a mod while GraceUI is inactive, inactive.
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
  GraceUiEvent event{};
  event.size = sizeof event;
  return {table->register_pages(consumer, nullptr),
          table->publish(consumer, nullptr, 0),
          table->poll(consumer, &event),
          table->answer(consumer, 0, nullptr, 0),
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

// The one slot GraceUI 0.2 still only declares, log: not yet (the bridge protocol's three are built, D169).
std::vector<GraceUiStatus> declared_answers(const GraceUiApi1* table, GraceUiConsumer consumer) {
  return {table->log(consumer, GRACEUI_LOG_INFO, "invented", 8)};
}

// The bridge protocol's three with nothing to read: refused, before anything else (D169).
std::vector<GraceUiStatus> protocol_nulls(const GraceUiApi1* table, GraceUiConsumer consumer) {
  return {table->register_pages(consumer, nullptr), table->publish(consumer, nullptr, 0),
          table->answer(consumer, 0, nullptr, 0)};
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
            example_text.find("GraceUI " + version_words(kBuildVersion) + ", found by name") != std::string::npos &&
            look_of(example_text) >= 2,
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
  const bool caps_ok = api.has_cap(GRACEUI_CAP_NOTICES) && api.has_cap(GRACEUI_CAP_PANELS) &&
                       api.has_cap(GRACEUI_CAP_TEXTURES) && api.has_cap(GRACEUI_CAP_FACES) &&
                       api.has_cap(GRACEUI_CAP_INPUT) && api.has_cap(GRACEUI_CAP_WORLD_STATE) &&
                       api.has_cap(GRACEUI_CAP_HOTKEYS) && api.has_cap(GRACEUI_CAP_PAGES) &&
                       api.has_cap(GRACEUI_CAP_HOLDER_EVENTS) && api.has_cap(GRACEUI_CAP_DISPLAY) &&
                       !api.has_cap("log") &&
                       !api.has_cap("Panels") &&
                       !api.has_cap("") &&
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
        "0xFFFFFFFF %s; has_cap: notices, panels, textures, faces, input, world-state, hotkeys, pages, holder-events and display yes, log, "
        "\"Panels\", \"\", \"Notices\" and a null name %s",
        table->size, sizeof(GraceUiApi1), table->abi_minor, GRACEUI_VERSION_MAJOR(table->graceui_version),
        GRACEUI_VERSION_MINOR(table->graceui_version), GRACEUI_VERSION_PATCH(table->graceui_version),
        slots_present ? "set" : "NOT SET", only_major_1 ? "answered null" : "ANSWERED A TABLE",
        caps_ok ? "no" : "NOT AS EXPECTED");

  // graceui-sys finds the loaded graceui.dll by its name, and calls it installed (D164; EldenNet's part 12 found the
  // by-name branch left installed at 0).
  GraceUiSysFound by_name{};
  by_name.size = sizeof by_name;
  const GraceUiApi1* named = graceui_sys_find(0, 0, &by_name);
  check(named == table && by_name.how == GRACEUI_SYS_BY_NAME && by_name.installed == 1 && by_name.attempts == 1,
        "found by name", "graceui-sys: %s, how %u, installed %u, after %u look(s)",
        named == table ? "this table" : "ANOTHER ANSWER", by_name.how, by_name.installed, by_name.attempts);

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
      {"floor 0.1.0", request_for("abi-low"), GRACEUI_OK, 0},
      {"floor this version", request_for("abi-same", kBuildVersion), GRACEUI_OK, 0},
      {"floor the next patch", request_for("abi-patch", kNextPatch), GRACEUI_TOO_OLD, 0},
      {"floor the next minor", request_for("abi-minor", kNextMinor), GRACEUI_TOO_OLD, 0},
      {"floor the next major", request_for("abi-major", kNextMajor), GRACEUI_TOO_OLD, 0},
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
    if (c.status == GRACEUI_TOO_OLD && c.request.floor == kNextMinor) {
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
  const std::string needs =
      "needs GraceUI " + version_words(kNextMinor) + " or newer; this is GraceUI " + version_words(kBuildVersion);
  for (const GraceUiTestNotice& notice : grace.notices()) {
    if (notice.owner == 0 && std::strstr(notice.text, needs.c_str())) {
      too_old_notice = notice.text;
    }
  }
  check(wrong.empty() && arguments_ok && too_old.graceui_version == version &&
            too_old.floor == kNextMinor && too_old_notice.rfind("An invented mod needs", 0) == 0 &&
            grace.logged(("abi-minor " + needs).c_str()),
        "connect answers",
        "%zu requests answered as they should%s%s; a null request, one of 60 bytes, a null answer and one of 16 bytes "
        "%s; too_old named GraceUI %u.%u.%u and the floor %u.%u.%u, GraceUI's notice \"%s\" and its log line %s",
        cases.size(), wrong.empty() ? "" : ", but: ", wrong.c_str(), arguments_ok ? "refused, writing nothing they "
        "should not" : "NOT AS EXPECTED", GRACEUI_VERSION_MAJOR(too_old.graceui_version),
        GRACEUI_VERSION_MINOR(too_old.graceui_version), GRACEUI_VERSION_PATCH(too_old.graceui_version),
        GRACEUI_VERSION_MAJOR(too_old.floor), GRACEUI_VERSION_MINOR(too_old.floor),
        GRACEUI_VERSION_PATCH(too_old.floor), too_old_notice.c_str(),
        grace.logged(("abi-minor " + needs).c_str()) ? "written" : "MISSING");
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

  // 7. The slots later increments build: "not yet" to a connected mod; every slot after notice "not connected" to a
  // handle that names nobody.
  const Connected waiting = connect(table, request_for("abi-waiting"));
  const std::vector<GraceUiStatus> unbuilt = declared_answers(table, waiting.answer.consumer);
  const std::vector<GraceUiStatus> nulls = protocol_nulls(table, waiting.answer.consumer);
  const std::vector<GraceUiStatus> nobody = unbuilt_answers(table, 0);
  check(all_answer(unbuilt, GRACEUI_NOT_YET) && all_answer(nulls, GRACEUI_REFUSED) &&
            all_answer(nobody, GRACEUI_NOT_CONNECTED) && unbuilt.size() == 1 && nobody.size() == 16,
        "not yet", "a connected mod: log %s, the protocol's three with nothing to read %s; a handle of 0: %s",
        answers_text(unbuilt).c_str(), answers_text(nulls).c_str(), answers_text(nobody).c_str());

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
  // Three lines of the notices' face (Grace's 22 px lines, or Dear ImGui's own font's where GraceUI's fonts folder
  // lacks Noto Sans), its padding above and below; the accent's rounded corners take a row or two off what is measured.
  const int line = grace.state().notice_face_grace != 0 ? GRACEUI_TEST_NOTICE_LINE : GRACEUI_TEST_NOTICE_LINE_FALLBACK;
  const int three_lines = 2 * GRACEUI_TEST_NOTICE_PAD_Y + 3 * line;
  const int wordy_height = wordy_boxes.empty() ? 0 : static_cast<int>(wordy_boxes[0].bottom - wordy_boxes[0].top);
  char wordy_step[120];
  std::snprintf(wordy_step, sizeof wordy_step, "a long text: three lines at most (%zu box, %d rows high of %d)",
                wordy_boxes.size(), wordy_height, three_lines);
  step(wordy_step, wordy_boxes.size() == 1 && wordy_height <= three_lines && wordy_height >= three_lines - 5);
  table->disconnect(wordy.answer.consumer);
  check(notices_ok, "notices", "%s", steps.c_str());

  // 9. No allocation and no free on the present thread as notices come, change and go during play, after warm-up; and
  // what a frame of three notices costs GraceUI, the GPU idle before each frame.
  const Connected busy = connect(table, request_for("abi-busy"));
  for (int i = 0; i < 60; ++i) {
    game.frame(kAbiColour, PresentWith::present);  // nothing showing: nothing drawn
  }
  const std::string long_text = "A notice that runs on for three lines or more, so that the box holds as much text as "
                                "it ever shows, and the frame draws as many glyphs as a notice ever draws, which is "
                                "the most room the foreground list reserved at warm-up must hold for it.";
  const CostWindows windows = cost_windows(grace, 1000.0, [&] {
    for (int round = 0; round < 6; ++round) {
      post(table, busy.answer.consumer, long_text.c_str(), static_cast<std::uint32_t>(round % 3), 1);
      post(table, busy.answer.consumer, round % 2 == 0 ? "Short." : "A little longer, this one.", GRACEUI_TONE_NORMAL,
           2);
      post(table, busy.answer.consumer, "Brief, and gone soon.", GRACEUI_TONE_WARNING, 3, 150);
      for (int i = 0; i < 50; ++i) {
        game.frame(kAbiColour, PresentWith::present);
        game.wait_idle();
      }
    }
  });
  const bool drawn = windows.every([](const CostWindow& w) { return w.after.frames_drawn - w.before.frames_drawn >= 200; });
  const Costs& costs = windows.best().costs;
  check(windows.allocations() == 0 && drawn && costs.own.p99_us <= 1000.0, "notice allocations",
        "in %zu window(s) of 300 frames as notices came, changed and went, at least 200 drawn in each: %s; "
        "allocations and frees on the present thread: %llu; GraceUI's own work median / p99 in us: %s (budget: p99 "
        "1000 us, the least window's)",
        windows.runs.size(), drawn ? "yes" : "NO", static_cast<unsigned long long>(windows.allocations()),
        windows.text().c_str());
  table->disconnect(busy.answer.consumer);
  grace.print_log();
}

// ---- S10.4.c: panels and their checked draw lists, textures at load, faces and glyph tables, input through ABI 1 and
// the world-state providers (D76 to D80), with the second example mod, hello-panel, and two mods of the harness's own.

constexpr Colour kPanelsColour{36, 36, 44};
constexpr Colour kHelloGround{HELLO_PANEL_GROUND_R, HELLO_PANEL_GROUND_G, HELLO_PANEL_GROUND_B};
constexpr Colour kCheckerA{HELLO_CHECKER_A_R, HELLO_CHECKER_A_G, HELLO_CHECKER_A_B};
constexpr Colour kCheckerB{HELLO_CHECKER_B_R, HELLO_CHECKER_B_G, HELLO_CHECKER_B_B};
constexpr Colour kHelloGold{HELLO_GLYPHS_R, HELLO_GLYPHS_G, HELLO_GLYPHS_B};
constexpr Colour kHud{200, 90, 30};        // the harness mod's hud panel at the top left, under the test panel
constexpr Colour kModal{90, 200, 120};     // its modal over the test panel, which takes input
constexpr Colour kTopModal{120, 60, 200};  // its modal under the notices
constexpr Colour kLowHud{170, 40, 90};     // its hud panel under the example's panel
constexpr Colour kCorner{30, 200, 220};    // its hud panel anchored at the bottom right
constexpr Colour kGlyphPanel{60, 60, 150}; // its panel-layer panel with a glyph of GraceUI's atlas on it
constexpr Colour kLate{240, 120, 200};     // the late mod's texture, uploaded after warm-up

std::wstring hello_panel_path() { return folder_of_this_program() + L"hello-panel.dll"; }

// The colour at (x, y) is `want`, within a tolerance; `why` says what was there if not.
bool pixel_is(const Frame& frame, UINT x, UINT y, Colour want, const char* what, std::string& why, int tolerance = 3) {
  if (x >= frame.width || y >= frame.height) {
    why += std::string(why.empty() ? "" : "; ") + what + ": outside the frame";
    return false;
  }
  const std::uint32_t pixel = frame.at(x, y);
  const bool ok = close_to(pixel, want, tolerance);
  char text[160];
  std::snprintf(text, sizeof text, "%s%s (%u,%u) %s%s", why.empty() ? "" : "; ", what, x, y, describe(pixel).c_str(),
                ok ? "" : " WRONG");
  why += text;
  return ok;
}

// Pixels within a rectangle that are near `want`.
int count_near(const Frame& frame, UINT x0, UINT y0, UINT x1, UINT y1, Colour want, int tolerance) {
  int count = 0;
  for (UINT y = y0; y < y1 && y < frame.height; ++y) {
    for (UINT x = x0; x < x1 && x < frame.width; ++x) {
      count += close_to(frame.at(x, y), want, tolerance) ? 1 : 0;
    }
  }
  return count;
}

// A mod of the harness's own: one solid quad a panel, drawn with its white texture's middle.
struct HarnessMod {
  const GraceUiApi1* table = nullptr;
  GraceUiConsumer me = 0;
  std::uint64_t white = 0;

  GraceUiStatus panel(std::uint32_t layer, std::uint32_t anchor, float x, float y, float width, float height,
                      std::uint32_t flags, std::uint64_t& id) const {
    GraceUiPanelDesc desc{};
    desc.size = sizeof desc;
    desc.layer = layer;
    desc.anchor = anchor;
    desc.flags = flags;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    return table->create_panel(me, &desc, &id);
  }

  GraceUiStatus quad(std::uint64_t panel, float width, float height, Colour colour, std::uint64_t texture = 0) const {
    const std::uint32_t c = packed(colour);
    const GraceUiDrawVert vertices[4] = {{0.0f, 0.0f, 0.5f, 0.5f, c},
                                         {width, 0.0f, 0.5f, 0.5f, c},
                                         {width, height, 0.5f, 0.5f, c},
                                         {0.0f, height, 0.5f, 0.5f, c}};
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    GraceUiDrawCommand command{};
    command.clip_x1 = width;
    command.clip_y1 = height;
    command.texture = texture != 0 ? texture : white;
    command.vertex_count = 4;
    command.index_count = 6;
    GraceUiDrawList list{};
    list.size = sizeof list;
    list.vertex_count = 4;
    list.index_count = 6;
    list.command_count = 1;
    list.command_size = sizeof command;
    list.vertices = vertices;
    list.indices = indices;
    list.commands = &command;
    return table->submit(me, panel, &list);
  }

  GraceUiStatus texture(std::uint32_t width, std::uint32_t height, const void* pixels, std::uint64_t& id,
                        std::uint32_t format = GRACEUI_TEXTURE_RGBA8, std::uint32_t row_bytes = 0) const {
    GraceUiTextureDesc desc{};
    desc.size = sizeof desc;
    desc.format = format;
    desc.width = width;
    desc.height = height;
    desc.row_bytes = row_bytes != 0 ? row_bytes : width * 4;
    desc.pixels = pixels;
    return table->register_texture(me, &desc, &id);
  }

  GraceUiStatus face(const std::vector<unsigned char>& data, std::uint32_t pixels, std::uint32_t& id,
                     const std::vector<std::uint32_t>& ranges = {}) const {
    GraceUiFaceDesc desc{};
    desc.size = sizeof desc;
    desc.pixel_height = pixels;
    desc.data = data.data();
    desc.data_length = data.size();
    desc.ranges = ranges.empty() ? nullptr : ranges.data();
    desc.range_count = ranges.size() / 2;
    return table->register_face(me, &desc, &id);
  }

  GraceUiInputAnswer request(std::uint64_t panel, std::uint32_t kind, GraceUiStatus& status,
                             std::uint32_t wants = GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE,
                             std::uint32_t flags = 0) const {
    GraceUiInputRequest request{};
    request.size = sizeof request;
    request.kind = kind;
    request.wants = wants;
    request.panel = panel;
    request.flags = flags;
    GraceUiInputAnswer answer{};
    answer.size = sizeof answer;
    status = table->request_input(me, &request, &answer);
    return answer;
  }

  GraceUiStatus world(std::uint32_t flags) const {
    GraceUiWorldState state{};
    state.size = sizeof state;
    state.flags = flags;
    return table->set_world_state(me, &state);
  }
};

std::vector<unsigned char> read_file(const std::wstring& path) {
  std::vector<unsigned char> data;
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return data;
  }
  LARGE_INTEGER size{};
  DWORD read = 0;
  if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
    data.resize(static_cast<std::size_t>(size.QuadPart));
    if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size()) {
      data.clear();
    }
  }
  CloseHandle(file);
  return data;
}

const char* answer_name(std::uint32_t answer) {
  switch (answer) {
    case GRACEUI_INPUT_GRANTED: return "granted";
    case GRACEUI_INPUT_REFUSED: return "refused";
    case GRACEUI_INPUT_NOT_NOW: return "not now";
    default: return "UNKNOWN";
  }
}

int count_log_lines(const GraceUi& grace, const char* text) {
  char line[200];
  int count = 0;
  for (std::uint32_t i = 0; grace.log_line(i, line, sizeof line) > 0; ++i) {
    count += std::strstr(line, text) != nullptr ? 1 : 0;
  }
  return count;
}

void run_panels(const std::wstring& dll) {
  Game game(false);
  std::string why;

  // 1. Before warm-up. hello-panel, loaded before GraceUI, finds it, connects and registers its face, its own Dear
  // ImGui's atlas and its checker as textures, and its panel; the harness's mod does the same with two faces, a white
  // texture and its panels. Nothing is presented until both are done, so warm-up comes after them (D80).
  if (LoadLibraryW(hello_panel_path().c_str()) == nullptr) {
    fatal("hello-panel.dll did not load");
  }
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  bool example_ready = false;
  for (int waited = 0; waited < 10000 && !example_ready; waited += 20) {
    const GraceUiTestState state = grace.state();
    example_ready = state.consumers >= 1 && state.faces >= 1 && state.textures >= 2 && state.panels_shown >= 1;
    Sleep(20);
  }
  const Connected connected =
      connect(table, request_for("harness-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Harness mod",
                                 "panels textures faces input world-state"));
  HarnessMod mod{table, connected.answer.consumer, 0};
  const std::uint32_t white_pixels[16] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  const GraceUiStatus white_status = mod.texture(4, 4, white_pixels, mod.white);

  const std::vector<unsigned char> noto = read_file(folder_of_this_program() + L"fonts\\NotoSans-Regular.ttf");
  std::vector<unsigned char> not_a_font(noto.begin(), noto.begin() + std::min<std::size_t>(noto.size(), 4096));
  for (std::size_t i = 0; i < not_a_font.size(); i += 7) {
    not_a_font[i] ^= 0x5Au;
  }
  std::uint32_t bad_face = 0;
  const GraceUiStatus bad_font_status = mod.face(not_a_font, 18, bad_face);  // refused, and out of its budget again
  const GraceUiStatus small_face_status = mod.face(noto, 2, bad_face);
  // Every glyph the font has in the Basic Multilingual Plane at 128 px: over the shared atlas's room (the review's M3),
  // full, and out of the mod's count again.
  const GraceUiStatus huge_face_status = mod.face(noto, 128, bad_face, {0x20, 0xD7FF, 0xE000, 0xFFFF});
  std::uint32_t face_a = 0;
  std::uint32_t face_b = 0;
  std::uint32_t face_c = 0;
  const GraceUiStatus face_a_status = mod.face(noto, 18, face_a);
  const GraceUiStatus face_b_status = mod.face(noto, 24, face_b);
  const GraceUiStatus face_c_status = mod.face(noto, 30, face_c);  // a third: over a mod's two (EST)
  GraceUiGlyphTable early{};
  early.size = sizeof early;
  const GraceUiStatus early_glyphs = table->glyph_table(mod.me, 1, &early);
  check(example_ready && connected.status == GRACEUI_OK && white_status == GRACEUI_OK && face_a_status == GRACEUI_OK &&
            face_b_status == GRACEUI_OK && face_c_status == GRACEUI_FULL && bad_font_status == GRACEUI_REFUSED &&
            small_face_status == GRACEUI_REFUSED && huge_face_status == GRACEUI_FULL &&
            early_glyphs == GRACEUI_NOT_READY && face_a != face_b,
        "before warm-up",
        "hello-panel %s; the harness's mod: a texture %s, faces of 18 and 24 px %s and %s (ids %u, %u), a third %s, a "
        "file that is not a font %s, a size of 2 px %s, the font's whole BMP at 128 px (over the atlas's room) %s; "
        "glyph_table before warm-up: %s",
        example_ready ? "connected and registered its face, two textures and its panel" : "DID NOT REGISTER",
        status_name(white_status), status_name(face_a_status), status_name(face_b_status), face_a, face_b,
        status_name(face_c_status), status_name(bad_font_status), status_name(small_face_status),
        status_name(huge_face_status), status_name(early_glyphs));

  // Its panels: hud at the top left under the test panel; a modal over the test panel, which takes input; a modal under
  // the notices; hud under the example's panel; hud at the bottom right; a panel-layer panel that takes input, with a
  // glyph of GraceUI's atlas. Then more, hidden, up to a mod's bound; the next is full.
  std::uint64_t hud = 0, modal = 0, top_modal = 0, low_hud = 0, corner = 0, glyph_panel = 0, spare = 0, beyond = 0,
                bad_layer = 0;
  const bool made =
      mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_TOP_LEFT, 8, 8, 60, 60, 0, hud) == GRACEUI_OK &&
      mod.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 150, 60, 80, 40, GRACEUI_PANEL_TAKES_INPUT, modal) ==
          GRACEUI_OK &&
      mod.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_RIGHT, 0, 0, 330, 50, 0, top_modal) == GRACEUI_OK &&
      mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_TOP_LEFT, 20, 200, 40, 60, 0, low_hud) == GRACEUI_OK &&
      mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_BOTTOM_RIGHT, 10, 10, 50, 30, 0, corner) == GRACEUI_OK &&
      mod.panel(GRACEUI_LAYER_PANEL, GRACEUI_ANCHOR_TOP_LEFT, 300, 150, 60, 40, GRACEUI_PANEL_TAKES_INPUT,
                glyph_panel) == GRACEUI_OK;
  bool spares = true;
  for (std::uint32_t i = 6; i < GRACEUI_PANELS_PER_MOD; ++i) {
    spares = mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_CENTRE, 0, 0, 10, 10, 0, spare) == GRACEUI_OK && spares;
  }
  const GraceUiStatus beyond_status = mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_CENTRE, 0, 0, 10, 10, 0, beyond);
  const GraceUiStatus bad_layer_status = mod.panel(3, GRACEUI_ANCHOR_CENTRE, 0, 0, 10, 10, 0, bad_layer);
  const std::uint32_t shown_before = grace.state().panels_shown;
  mod.quad(hud, 60, 60, kHud);
  mod.quad(modal, 80, 40, kModal);
  mod.quad(top_modal, 330, 50, kTopModal);
  mod.quad(low_hud, 40, 60, kLowHud);
  mod.quad(corner, 50, 30, kCorner);
  mod.quad(glyph_panel, 60, 40, kGlyphPanel);
  std::uint64_t after_submit = 0;
  const GraceUiStatus late_texture = mod.texture(4, 4, white_pixels, after_submit);
  for (const std::uint64_t id : {hud, modal, top_modal, low_hud, corner, glyph_panel}) {
    table->show_panel(mod.me, id);
  }
  const GraceUiTestState registered = grace.state();
  check(made && spares && beyond_status == GRACEUI_FULL && bad_layer_status == GRACEUI_REFUSED && shown_before == 1 &&
            registered.panels == 1 + GRACEUI_PANELS_PER_MOD && registered.panels_shown == 7 &&
            late_texture == GRACEUI_TOO_LATE,
        "panels made",
        "%u panels of the harness's mod made (%s), the next %s, GraceUI's menu layer %s; hidden at first: %u shown "
        "of %u (the example's own); then %u of %u; a texture after its first submit: %s",
        GRACEUI_PANELS_PER_MOD, made && spares ? "yes" : "NO", status_name(beyond_status), status_name(bad_layer_status),
        shown_before,
        registered.panels, registered.panels_shown, registered.panels, status_name(late_texture));

  // 2. Warm-up: the atlas baked once with GraceUI's notices' face and the three faces of the mods; the four textures in
  // the same upload; glyph tables for every face; faces refused from then on.
  table = start_host(game, grace);
  bool warm = false;
  for (int i = 0; i < 120 && !warm; ++i) {
    game.frame(kPanelsColour, PresentWith::present);
    const GraceUiTestState state = grace.state();
    warm = state.atlas_generation == 1 && state.textures_ready == 3;
  }
  const GraceUiTestState warmed = grace.state();
  GraceUiGlyphTable notices_face{};
  notices_face.size = sizeof notices_face;
  const GraceUiStatus own_glyphs = table->glyph_table(mod.me, 1, &notices_face);
  GraceUiGlyphTable mod_face{};
  mod_face.size = sizeof mod_face;
  const GraceUiStatus mod_glyphs = table->glyph_table(mod.me, face_b, &mod_face);
  GraceUiGlyphTable none{};
  none.size = sizeof none;
  const GraceUiStatus no_glyphs = table->glyph_table(mod.me, 999, &none);
  std::uint32_t too_late_face = 0;
  const GraceUiStatus late_face = mod.face(noto, 18, too_late_face);
  bool has_a = false;
  bool sorted = notices_face.glyph_count > 0;
  for (std::uint32_t i = 0; own_glyphs == GRACEUI_OK && i < notices_face.glyph_count; ++i) {
    has_a = has_a || (notices_face.glyphs[i].code_point == 'A' && notices_face.glyphs[i].advance > 0.0f &&
                      notices_face.glyphs[i].u1 > notices_face.glyphs[i].u0);
    sorted = sorted && (i == 0 || notices_face.glyphs[i - 1].code_point < notices_face.glyphs[i].code_point);
  }
  check(warm && warmed.faces == 3 && warmed.faces_baked == 4 && warmed.notice_face_grace == 1 &&
            warmed.texture_uploads == 1 && warmed.textures_uploaded == 3 && own_glyphs == GRACEUI_OK &&
            notices_face.generation == 1 && notices_face.atlas == 1 && notices_face.glyph_count >= 300 && has_a &&
            sorted && notices_face.pixel_height == 15.0f && mod_glyphs == GRACEUI_OK &&
            mod_face.pixel_height == 24.0f && no_glyphs == GRACEUI_REFUSED && late_face == GRACEUI_TOO_LATE,
        "atlas and faces",
        "atlas generation %u at %ux%u; %u faces in it (the mods' %u, and GraceUI's notices' face from its fonts folder: "
        "%s); one upload with the atlas (%u), %u textures uploaded; face 1: %s, %u glyphs, %.0f px, sorted %s, 'A' %s; "
        "the harness's 24 px face: %s, %.0f px, %u glyphs; face 999: %s; a face after warm-up: %s",
        warmed.atlas_generation, warmed.atlas_width, warmed.atlas_height, warmed.faces_baked, warmed.faces,
        warmed.notice_face_grace ? "yes" : "NO", warmed.texture_uploads, warmed.textures_uploaded,
        status_name(own_glyphs), notices_face.glyph_count, static_cast<double>(notices_face.pixel_height),
        sorted ? "yes" : "NO", has_a ? "there" : "MISSING", status_name(mod_glyphs),
        static_cast<double>(mod_face.pixel_height), mod_face.glyph_count, status_name(no_glyphs),
        status_name(late_face));

  // 3. A list with a glyph of GraceUI's atlas on the panel-layer panel: 'W' of face 1, white, on its ground.
  const GraceUiGlyph* w = nullptr;
  for (std::uint32_t i = 0; i < notices_face.glyph_count; ++i) {
    w = notices_face.glyphs[i].code_point == 'W' ? &notices_face.glyphs[i] : w;
  }
  GraceUiStatus glyph_list = GRACEUI_FAULT;
  GraceUiStatus stale_generation = GRACEUI_FAULT;
  if (w != nullptr) {
    const std::uint32_t ground = packed(kGlyphPanel);
    const std::uint32_t ink = 0xFFFFFFFFu;
    const float x = 20.0f, y = 8.0f;
    const GraceUiDrawVert vertices[8] = {
        {0, 0, 0.5f, 0.5f, ground}, {60, 0, 0.5f, 0.5f, ground}, {60, 40, 0.5f, 0.5f, ground}, {0, 40, 0.5f, 0.5f, ground},
        {x + w->x0, y + w->y0, w->u0, w->v0, ink}, {x + w->x1, y + w->y0, w->u1, w->v0, ink},
        {x + w->x1, y + w->y1, w->u1, w->v1, ink}, {x + w->x0, y + w->y1, w->u0, w->v1, ink}};
    const std::uint16_t indices[12] = {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
    GraceUiDrawCommand commands[2] = {};
    for (GraceUiDrawCommand& c : commands) {
      c.clip_x1 = 60;
      c.clip_y1 = 40;
      c.vertex_count = 4;
      c.index_count = 6;
    }
    commands[0].texture = mod.white;
    commands[1].texture = notices_face.atlas;
    commands[1].vertex_offset = 4;  // the second command's indices are relative to its own vertices (D76)
    commands[1].index_offset = 6;
    GraceUiDrawList list{};
    list.size = sizeof list;
    list.atlas_generation = 2;  // an atlas GraceUI never baked
    list.vertex_count = 8;
    list.index_count = 12;
    list.command_count = 2;
    list.command_size = sizeof(GraceUiDrawCommand);
    list.vertices = vertices;
    list.indices = indices;
    list.commands = commands;
    stale_generation = table->submit(mod.me, glyph_panel, &list);
    list.atlas_generation = notices_face.generation;
    glyph_list = table->submit(mod.me, glyph_panel, &list);
  }

  // 4. The layers (D76), with GraceUI's own test panel on the menu's layer and a notice above everything.
  grace.set_panel(1);
  post(table, mod.me, "A notice above every layer.");
  Frame frame;
  bool drawn = false;
  for (int i = 0; i < 60 && !drawn; ++i) {
    frame = game.frame_and_read(kPanelsColour, PresentWith::present);
    drawn = close_to(frame.at(HELLO_PANEL_X + 200, HELLO_PANEL_Y + 100), kHelloGround, 3) &&
            close_to(frame.at(320, 160), kGlyphPanel, 3) &&
            count_near(frame, 300, 150, 360, 190, Colour{255, 255, 255}, 40) > 0;
  }
  const UINT hx = HELLO_PANEL_X;
  const UINT hy = HELLO_PANEL_Y;
  std::string example_why;
  bool example_ok = pixel_is(frame, hx + 200, hy + 100, kHelloGround, "its ground", example_why);
  example_ok = pixel_is(frame, hx + HELLO_CHECKER_X + 4, hy + HELLO_CHECKER_Y + 4, kCheckerA, "its checker's first square",
                        example_why) && example_ok;
  example_ok = pixel_is(frame, hx + HELLO_CHECKER_X + 12, hy + HELLO_CHECKER_Y + 4, kCheckerB, "the second",
                        example_why) && example_ok;
  example_ok = pixel_is(frame, hx + HELLO_CHECKER_X + 12, hy + HELLO_CHECKER_Y + 12, kCheckerA, "the one below it",
                        example_why) && example_ok;
  const int own_ink = count_near(frame, hx + HELLO_TEXT_X, hy + HELLO_TEXT_Y, hx + 140, hy + HELLO_TEXT_Y + 16,
                                 Colour{255, 255, 255}, 30);
  const int shared_ink = count_near(frame, hx + HELLO_GLYPHS_X, hy + HELLO_GLYPHS_Y, hx + 235,
                                    hy + HELLO_GLYPHS_Y + 22, kHelloGold, 40);
  example_ok = example_ok && own_ink >= 20 && shared_ink >= 20;
  check(drawn && example_ok, "example's panel",
        "hello-panel's list, built by its own headless Dear ImGui and placed by its anchor: %s; its own font's ink "
        "(its own atlas, its texture): %d pixels; its line from GraceUI's shared atlas: %d gold pixels",
        example_why.c_str(), own_ink, shared_ink);

  std::string layers_why;
  bool layers = pixel_is(frame, 12, 12, kHud, "hud alone", layers_why);
  layers = pixel_is(frame, 60, 30, kPanel, "the menu's layer over hud", layers_why) && layers;
  layers = pixel_is(frame, 170, 70, kModal, "modal over the menu's layer", layers_why) && layers;
  layers = pixel_is(frame, 200, 95, kModal, "modal alone", layers_why) && layers;
  layers = pixel_is(frame, 25, 220, kLowHud, "hud alone", layers_why) && layers;
  layers = pixel_is(frame, 50, 220, kHelloGround, "the panel layer over hud", layers_why) && layers;
  layers = pixel_is(frame, 315, 5, kTopModal, "modal alone", layers_why) && layers;
  const int left = notice_left(frame);
  layers = pixel_is(frame, static_cast<UINT>(left) + 1, GRACEUI_TEST_NOTICE_MARGIN + 10,
                    colour_of(GRACEUI_TEST_NOTICE_GOLD), "a notice's accent over modal", layers_why) && layers;
  layers = pixel_is(frame, static_cast<UINT>(left) + 20, GRACEUI_TEST_NOTICE_MARGIN + 3, kNoticeGround,
                    "its ground over modal", layers_why) && layers;
  layers = pixel_is(frame, 605, 335, kCorner, "hud anchored bottom right", layers_why) && layers;
  layers = pixel_is(frame, 310, 160, kGlyphPanel, "the glyph panel's ground", layers_why) && layers;
  const int glyph_ink = count_near(frame, 300, 150, 360, 190, Colour{255, 255, 255}, 60);
  check(drawn && layers && glyph_ink >= 10 && glyph_list == GRACEUI_OK && stale_generation == GRACEUI_REFUSED,
        "layers",
        "hud < panel < GraceUI's menu layer < modal < notices: %s; a 'W' of GraceUI's atlas on a panel: %d pixels of "
        "white ink (the list %s; built against generation 2: %s)",
        layers_why.c_str(), glyph_ink, status_name(glyph_list), status_name(stale_generation));

  // 5. The display changes size: each panel follows its anchor.
  game.resize(800, 450);
  frame = game.frame_and_read(kPanelsColour, PresentWith::present);
  for (int i = 0; i < 5 && !close_to(frame.at(765, 425), kCorner, 3); ++i) {
    frame = game.frame_and_read(kPanelsColour, PresentWith::present);
  }
  std::string resize_why;
  bool followed = pixel_is(frame, 765, 425, kCorner, "bottom-right at 800x450", resize_why);
  followed = !close_to(frame.at(605, 335), kCorner, 3) && followed;
  followed = pixel_is(frame, hx + 200, hy + 100, kHelloGround, "the example, top-left", resize_why) && followed;
  game.resize(640, 360);
  check(followed, "anchors", "after ResizeBuffers to 800x450: %s; nothing left where it was", resize_why.c_str());

  // 6. The checker's refusals through submit (each a unit test too): the panel keeps its last list, and the mod's
  // refusals are logged once.
  // What the refusals may not move: the hud panel's own count of lists published. (The session's count does not serve:
  // the example's thread publishes every 10 ms, and so, now and then, while the refusals are made.)
  const GraceUiTestState before_refusals = grace.state();
  const std::uint64_t hud_published_before = grace.panel_published(hud);
  const auto submit_with = [&](auto&& edit) {
    GraceUiDrawVert vertices[4] = {{0, 0, 0.5f, 0.5f, packed(kHud)}, {60, 0, 0.5f, 0.5f, packed(kHud)},
                                   {60, 60, 0.5f, 0.5f, packed(kHud)}, {0, 60, 0.5f, 0.5f, packed(kHud)}};
    std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    GraceUiDrawCommand commands[65] = {};
    for (GraceUiDrawCommand& c : commands) {
      c.clip_x1 = 60;
      c.clip_y1 = 60;
      c.texture = mod.white;
      c.vertex_count = 4;
      c.index_count = 6;
    }
    GraceUiDrawList list{};
    list.size = sizeof list;
    list.vertex_count = 4;
    list.index_count = 6;
    list.command_count = 1;
    list.command_size = sizeof(GraceUiDrawCommand);
    list.vertices = vertices;
    list.indices = indices;
    list.commands = commands;
    std::uint64_t panel = hud;
    edit(list, vertices, indices, commands, panel);
    return table->submit(mod.me, panel, &list);
  };
  using V = GraceUiDrawVert*;
  using I = std::uint16_t*;
  using C = GraceUiDrawCommand*;
  const GraceUiStatus refusals[] = {
      submit_with([](GraceUiDrawList&, V, I i, C, std::uint64_t&) { i[2] = 4; }),               // an index past 3
      submit_with([](GraceUiDrawList&, V, I, C c, std::uint64_t&) { c[0].clip_x1 = 61; }),      // clip past the panel
      submit_with([](GraceUiDrawList&, V, I, C c, std::uint64_t&) { c[0].texture = 0x12302; }), // nobody's texture
      submit_with([](GraceUiDrawList&, V, I, C c, std::uint64_t&) { c[0].vertex_count = 5; }),  // past the vertices
      submit_with([](GraceUiDrawList& l, V, I, C, std::uint64_t&) { l.command_count = 65; }),   // 65 commands
      submit_with([](GraceUiDrawList& l, V, I, C, std::uint64_t&) { l.command_size = 16; }),    // a stride below 1.0's
      submit_with([](GraceUiDrawList& l, V, I, C, std::uint64_t&) { l.command_size = 0x7FFFFFFF; }),  // no stride (L6)
      submit_with([](GraceUiDrawList& l, V, I, C, std::uint64_t&) { l.vertices = nullptr; }),   // no vertices
      submit_with([](GraceUiDrawList& l, V, I, C, std::uint64_t&) { l.vertex_count = 13108; }), // over 256 KiB
      submit_with([](GraceUiDrawList&, V, I, C, std::uint64_t& p) { p = p + 256; }),            // another generation
  };
  const GraceUiTestState after_refusals = grace.state();
  const std::uint64_t hud_published_after = grace.panel_published(hud);
  frame = game.frame_and_read(kPanelsColour, PresentWith::present);
  std::string kept_why;
  const bool kept = pixel_is(frame, 12, 12, kHud, "the hud panel's last list", kept_why);
  bool all_refused = true;
  std::string refused_text;
  for (const GraceUiStatus status : refusals) {
    all_refused = all_refused && status == GRACEUI_REFUSED;
    refused_text += (refused_text.empty() ? "" : ",") + std::string(status_name(status));
  }
  const int logged = count_log_lines(grace, "a draw list of harness-mod was refused");
  check(all_refused && kept && logged == 1 &&
            after_refusals.lists_refused - before_refusals.lists_refused == 9 && hud_published_before != 0 &&
            hud_published_after == hud_published_before,
        "draw list refusals",
        "an index past its command's vertices, a clip past the panel, nobody's texture, a command's vertices past the "
        "list's, 65 commands, a stride of 16, a stride of 2 GiB, no vertices, 13,108 vertices, a stale panel id: %s; "
        "%llu counted by the "
        "checker, %d log line for the mod; the hud panel's lists published: %llu before, %llu after (the session's: "
        "%llu more meanwhile, the example's); %s",
        refused_text.c_str(),
        static_cast<unsigned long long>(after_refusals.lists_refused - before_refusals.lists_refused), logged,
        static_cast<unsigned long long>(hud_published_before), static_cast<unsigned long long>(hud_published_after),
        static_cast<unsigned long long>(after_refusals.lists_submitted - before_refusals.lists_submitted),
        kept_why.c_str());

  // 7. A late mod: its texture, registered after warm-up within its budget, is uploaded at the next frame, once, and
  // drawn; its budget and its registration window hold.
  const Connected late = connect(table, request_for("harness-late", GRACEUI_VERSION_PACK(0, 1, 0), "Harness late"));
  HarnessMod late_mod{table, late.answer.consumer, 0};
  const std::uint32_t late_pixels[4] = {packed(kLate), packed(kLate), packed(kLate), packed(kLate)};
  std::uint64_t late_texture_id = 0;
  std::uint64_t ignored = 0;
  const GraceUiStatus late_first = late_mod.texture(2, 2, late_pixels, late_texture_id);
  const GraceUiStatus late_bytes = late_mod.texture(2048, 1024, late_pixels, ignored);  // 8 MiB more: over its budget
  const GraceUiStatus late_format = late_mod.texture(2, 2, late_pixels, ignored, 7);
  const GraceUiStatus late_rows = late_mod.texture(2, 2, late_pixels, ignored, GRACEUI_TEXTURE_RGBA8, 4);
  bool seven_more = true;
  for (int i = 0; i < 7; ++i) {
    seven_more = late_mod.texture(1, 1, late_pixels, ignored) == GRACEUI_OK && seven_more;
  }
  const GraceUiStatus late_ninth = late_mod.texture(1, 1, late_pixels, ignored);
  std::uint64_t late_panel = 0;
  late_mod.panel(GRACEUI_LAYER_HUD, GRACEUI_ANCHOR_TOP_LEFT, 400, 280, 40, 30, 0, late_panel);
  const GraceUiTestState before_late = grace.state();
  // White vertices, so that the texture's own colour is what is drawn.
  const GraceUiStatus late_submit = late_mod.quad(late_panel, 40, 30, Colour{255, 255, 255}, late_texture_id);
  table->show_panel(late_mod.me, late_panel);
  const GraceUiStatus late_after = late_mod.texture(1, 1, late_pixels, ignored);
  bool late_drawn = false;
  for (int i = 0; i < 30 && !late_drawn; ++i) {
    frame = game.frame_and_read(kPanelsColour, PresentWith::present);
    late_drawn = close_to(frame.at(420, 295), kLate, 3);
  }
  for (int i = 0; i < 5; ++i) {
    game.frame(kPanelsColour, PresentWith::present);
  }
  const GraceUiTestState after_late = grace.state();
  check(late_first == GRACEUI_OK && late_bytes == GRACEUI_FULL && late_format == GRACEUI_REFUSED &&
            late_rows == GRACEUI_REFUSED && seven_more && late_ninth == GRACEUI_FULL && late_submit == GRACEUI_OK &&
            late_after == GRACEUI_TOO_LATE && late_drawn &&
            after_late.texture_uploads == before_late.texture_uploads + 1 &&
            after_late.textures_uploaded == before_late.textures_uploaded + 8,
        "late textures",
        "a mod connected after warm-up: a 2x2 texture %s, 8 MiB more %s, an unknown format %s, rows shorter than its "
        "width %s, seven more %s, a ninth %s; its first submit %s, a texture after it %s; uploaded in %u upload (the "
        "upload counter), %u textures, and drawn: %s",
        status_name(late_first), status_name(late_bytes), status_name(late_format), status_name(late_rows),
        seven_more ? "ok" : "NOT ALL OK", status_name(late_ninth), status_name(late_submit), status_name(late_after),
        after_late.texture_uploads - before_late.texture_uploads,
        after_late.textures_uploaded - before_late.textures_uploaded, late_drawn ? "yes" : "NO");

  // 8. Input through ABI 1 (D77): granted, refused naming the holder, not now with the reason; the close routine from
  // hiding the panel, the player's exit by the menu key and by Escape held, and a release.
  const HWND window = game.window();
  GraceUiStatus status = GRACEUI_FAULT;
  const GraceUiInputAnswer granted = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  const GraceUiStatus granted_status = status;
  const GraceUiTestInput holding = grace.input();
  std::uint64_t late_input_panel = 0;
  late_mod.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 500, 100, 20, 20, GRACEUI_PANEL_TAKES_INPUT,
                 late_input_panel);
  late_mod.quad(late_input_panel, 20, 20, Colour{255, 255, 255}, late_texture_id);
  table->show_panel(late_mod.me, late_input_panel);
  const GraceUiInputAnswer refused = late_mod.request(late_input_panel, GRACEUI_HOLDER_MODAL, status);
  const GraceUiInputAnswer again = mod.request(modal, GRACEUI_HOLDER_MODAL, status);  // the holder asking again
  GraceUiStatus wrong_kind = GRACEUI_FAULT;
  mod.request(modal, GRACEUI_HOLDER_PANEL, wrong_kind);  // a modal's panel asked for as a panel
  GraceUiStatus no_input_panel = GRACEUI_FAULT;
  mod.request(hud, GRACEUI_HOLDER_PANEL, no_input_panel);  // a panel made without GRACEUI_PANEL_TAKES_INPUT
  GraceUiStatus not_its_own = GRACEUI_FAULT;
  late_mod.request(modal, GRACEUI_HOLDER_MODAL, not_its_own);
  table->hide_panel(mod.me, modal);
  const GraceUiTestInput hidden = grace.input();
  const GraceUiInputAnswer hidden_answer = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  table->show_panel(mod.me, modal);
  frames(game, 2);
  check(granted_status == GRACEUI_OK && granted.answer == GRACEUI_INPUT_GRANTED &&
            holding.holder_kind == GRACEUI_TEST_KIND_MODAL && refused.answer == GRACEUI_INPUT_REFUSED &&
            std::string_view(refused.holder) == "harness-mod" && refused.holder_kind == GRACEUI_HOLDER_MODAL &&
            again.answer == GRACEUI_INPUT_GRANTED && wrong_kind == GRACEUI_REFUSED &&
            no_input_panel == GRACEUI_REFUSED && not_its_own == GRACEUI_REFUSED && hidden.holder_owner == 0 &&
            hidden.last_close_reason == GRACEUI_TEST_CLOSED_RELEASED && hidden_answer.answer == GRACEUI_INPUT_NOT_NOW &&
            hidden_answer.why == GRACEUI_NOT_NOW_NOT_READY,
        "request_input",
        "the modal: %s (the holder a %s); another mod: %s, naming \"%s\" (kind %u); the holder again: %s; the wrong "
        "kind %s; a panel that takes no input %s; another mod's panel %s; hiding the panel ended the holder (%s); a "
        "hidden panel's request: %s (why %u)",
        answer_name(granted.answer), holding.holder_kind == GRACEUI_TEST_KIND_MODAL ? "modal" : "NOT A MODAL",
        answer_name(refused.answer), refused.holder, refused.holder_kind, answer_name(again.answer),
        status_name(wrong_kind), status_name(no_input_panel), status_name(not_its_own),
        reason_name(hidden.last_close_reason), answer_name(hidden_answer.answer), hidden_answer.why);

  // The player's exit: the menu key's first press names the holder in GraceUI's own notice, the second within 2 s ends
  // it; Escape held 1 s ends it too. Neither reaches the game.
  g_seen = Seen{};
  mod.request(glyph_panel, GRACEUI_HOLDER_PANEL, status);
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  frames(game, 2);
  std::string controls;
  for (const GraceUiTestNotice& notice : grace.notices()) {
    if (notice.owner == 0 && std::strstr(notice.text, "has the controls") != nullptr) {
      controls = notice.text;
    }
  }
  const GraceUiTestInput warned = grace.input();
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  frames(game, 2);
  const GraceUiTestInput exited = grace.input();
  mod.request(glyph_panel, GRACEUI_HOLDER_PANEL, status);
  post_key(window, VK_ESCAPE, kScanEscape, true, false);
  const DWORD escape_start = GetTickCount();
  while (GetTickCount() - escape_start < 1500 && grace.input().holder_owner != 0) {
    post_key(window, VK_ESCAPE, kScanEscape, true, true);
    game.frame(kPanelsColour, PresentWith::present);
    Sleep(30);
  }
  const DWORD held_for = GetTickCount() - escape_start;
  const GraceUiTestInput escaped = grace.input();
  post_key(window, VK_ESCAPE, kScanEscape, false, false);
  frames(game, 2);
  const GraceUiInputAnswer released_answer = mod.request(glyph_panel, GRACEUI_HOLDER_PANEL, status);
  const GraceUiStatus release_status = table->release_input(mod.me);
  const GraceUiTestInput released = grace.input();
  check(warned.holder_owner != 0 && controls == "Harness mod has the controls. Press F5 again to take them back." &&
            exited.holder_owner == 0 && exited.last_close_reason == GRACEUI_TEST_CLOSED_PLAYER &&
            escaped.holder_owner == 0 && escaped.last_close_reason == GRACEUI_TEST_CLOSED_PLAYER && held_for >= 950 &&
            g_seen.key_down[VK_F5] == 0 && g_seen.key_down[VK_ESCAPE] == 0 &&
            released_answer.answer == GRACEUI_INPUT_GRANTED && release_status == GRACEUI_OK &&
            released.holder_owner == 0 && released.last_close_reason == GRACEUI_TEST_CLOSED_RELEASED,
        "player's exit",
        "a panel's holder: the first F5 shows \"%s\"; the second ends it (%s); again, Escape held %lu ms ends it (%s); "
        "F5 and Escape reaching the game: %d, %d; a release ends the next (%s)",
        controls.c_str(), reason_name(exited.last_close_reason), static_cast<unsigned long>(held_for),
        reason_name(escaped.last_close_reason), g_seen.key_down[VK_F5], g_seen.key_down[VK_ESCAPE],
        reason_name(released.last_close_reason));

  // 9a. Providers and a request on threads of their own (the review's M1): one provider claims danger and clears it
  // over and over, ending on danger, while another clears its own claims as often and a third thread asks for input
  // all the while. However their calls interleave, the danger that stands at the end ends every holder: none is left,
  // the combined claims say danger, and a request is not now (danger).
  {
    game.frame(kPanelsColour, PresentWith::present);  // the overlay drew just now: requests may be granted
    const std::uint32_t generation_before = grace.input().generation;
    std::atomic<bool> stop{false};
    std::atomic<int> asked_count{0};
    std::thread asker([&] {
      GraceUiStatus asked = GRACEUI_OK;
      while (!stop.load()) {
        mod.request(modal, GRACEUI_HOLDER_MODAL, asked);
        asked_count.fetch_add(1);
      }
    });
    const DWORD asking_since = GetTickCount();
    while (asked_count.load() < 50 && GetTickCount() - asking_since < 2000) {
      Sleep(1);
    }
    std::thread clearer([&] {
      for (int i = 0; i < 3000; ++i) {
        late_mod.world(0);
      }
    });
    for (int i = 0; i < 3000; ++i) {
      mod.world((i & 1) != 0 ? GRACEUI_WORLD_DANGER : 0u);
    }
    mod.world(GRACEUI_WORLD_DANGER);
    clearer.join();
    Sleep(20);
    stop.store(true);
    asker.join();
    const GraceUiTestInput raced = grace.input();
    const GraceUiTestState raced_state = grace.state();
    const GraceUiInputAnswer after_race = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
    check(raced.holder_owner == 0 && raced_state.world_flags == GRACEUI_WORLD_DANGER &&
              after_race.answer == GRACEUI_INPUT_NOT_NOW && after_race.why == GRACEUI_NOT_NOW_DANGER,
          "providers on threads",
          "after 3,000 claims of danger and clears on one thread, 3,000 clears on another and requests on a third "
          "(%u grants meanwhile), ending on danger: holder %u, combined flags %u, a request %s (why %u)",
          raced.generation - generation_before, raced.holder_owner, raced_state.world_flags,
          answer_name(after_race.answer), after_race.why);
    mod.world(0);
  }
  // 9b. The holder's panel hidden on one thread while another asks for input with it (the review's L14): once it ends
  // hidden, no holder is left, however the calls interleave.
  {
    game.frame(kPanelsColour, PresentWith::present);
    const std::uint32_t generation_before = grace.input().generation;
    std::atomic<bool> stop{false};
    std::atomic<int> asked_count{0};
    std::thread asker([&] {
      GraceUiStatus asked = GRACEUI_OK;
      while (!stop.load()) {
        mod.request(modal, GRACEUI_HOLDER_MODAL, asked);
        asked_count.fetch_add(1);
      }
    });
    const DWORD asking_since = GetTickCount();
    while (asked_count.load() < 50 && GetTickCount() - asking_since < 2000) {
      Sleep(1);
    }
    for (int i = 0; i < 3000; ++i) {
      if ((i & 1) != 0) {
        table->show_panel(mod.me, modal);
      } else {
        table->hide_panel(mod.me, modal);
      }
    }
    table->hide_panel(mod.me, modal);
    Sleep(20);
    stop.store(true);
    asker.join();
    const GraceUiTestInput hidden = grace.input();
    check(hidden.holder_owner == 0, "hidden on a thread",
          "after 3,000 shows and hides on one thread while another asked for input with the panel (%u grants "
          "meanwhile), ending hidden: holder %u",
          hidden.generation - generation_before, hidden.holder_owner);
    table->show_panel(mod.me, modal);
  }

  // 9. World-state providers (D78): every claim combined; danger ends the holder and answers not now; a provider's
  // claims go with it; an unknown flag is refused.
  mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  const GraceUiStatus danger_status = mod.world(GRACEUI_WORLD_DANGER);
  const GraceUiTestInput in_danger = grace.input();
  const GraceUiInputAnswer danger_answer = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  late_mod.world(GRACEUI_WORLD_GAME_MENU);
  mod.world(0);
  const GraceUiTestState two_providers = grace.state();
  const GraceUiInputAnswer menu_answer = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  const GraceUiStatus unknown_flag = mod.world(64);
  table->disconnect(late_mod.me);  // its claims, panels and textures go with it
  const GraceUiTestState one_provider = grace.state();
  const GraceUiInputAnswer clear_answer = mod.request(modal, GRACEUI_HOLDER_MODAL, status);
  table->release_input(mod.me);
  check(danger_status == GRACEUI_OK && in_danger.holder_owner == 0 &&
            in_danger.last_close_reason == GRACEUI_TEST_CLOSED_DANGER && danger_answer.answer == GRACEUI_INPUT_NOT_NOW &&
            danger_answer.why == GRACEUI_NOT_NOW_DANGER && two_providers.providers == 2 &&
            two_providers.world_flags == GRACEUI_WORLD_GAME_MENU && menu_answer.answer == GRACEUI_INPUT_NOT_NOW &&
            menu_answer.why == GRACEUI_NOT_NOW_GAME_MENU && unknown_flag == GRACEUI_REFUSED &&
            one_provider.providers == 1 && one_provider.world_flags == 0 && clear_answer.answer == GRACEUI_INPUT_GRANTED,
        "providers",
        "danger from one provider ended the holder (%s) and a request is not now (why %u); a game menu from another, "
        "the first clear: %u providers, flags %u, a request not now (why %u); an unknown flag %s; the second gone: "
        "%u provider, flags %u, a request %s",
        reason_name(in_danger.last_close_reason), danger_answer.why, two_providers.providers,
        two_providers.world_flags, menu_answer.why, status_name(unknown_flag), one_provider.providers,
        one_provider.world_flags, answer_name(clear_answer.answer));

  // 10. The triple buffers under the example's thread, which submits every 10 ms: over 300 frames drawn after warm-up,
  // no allocation and no free on the present thread, and GraceUI's own work within 1 ms at p99, the GPU idle before
  // each frame (frames as a game runs them are printed too).
  grace.set_panel(0);
  for (int i = 0; i < 60; ++i) {
    game.frame(kPanelsColour, PresentWith::present);
  }
  grace.reset_counters();
  GraceUiTestState before = grace.state();
  for (int i = 0; i < 300; ++i) {
    game.frame(kPanelsColour, PresentWith::present);
  }
  GraceUiTestState after = grace.state();
  const Costs pipelined = frame_costs(grace);
  note("as a game runs, over %u frames: GraceUI's own work median %.1f us, p99 %.1f us; %llu lists published meanwhile",
       pipelined.count, pipelined.own.median_us, pipelined.own.p99_us,
       static_cast<unsigned long long>(after.lists_submitted - before.lists_submitted));
  const std::uint64_t pipelined_allocations = after.allocations + after.frees;
  const CostWindows windows = cost_windows(grace, 1000.0, [&] {
    for (int i = 0; i < 300; ++i) {
      game.frame(kPanelsColour, PresentWith::present);
      game.wait_idle();
    }
  });
  const CostWindow& last = windows.runs.back();
  const std::uint64_t panel_frames = last.after.panel_frames - last.before.panel_frames;
  const std::uint64_t published = last.after.lists_submitted - last.before.lists_submitted;
  const bool every_window = windows.every([](const CostWindow& w) {
    return w.after.panel_frames - w.before.panel_frames >= 290 && w.after.lists_submitted - w.before.lists_submitted >= 30;
  });
  check(pipelined_allocations == 0 && windows.allocations() == 0 && every_window &&
            windows.best().costs.own.p99_us <= 1000.0,
        "panel frames",
        "in %zu window(s) of 300 frames with seven panels shown while the example published lists from its thread (%llu "
        "in the last window, %llu frames drawing panels; at least 30 and 290 in each: %s); allocations and frees on the "
        "present thread: %llu (and %llu over the 300 before); GraceUI's own work median / p99 in us: %s (budget: p99 "
        "1000 us, all panels together, the least window's)",
        windows.runs.size(), static_cast<unsigned long long>(published), static_cast<unsigned long long>(panel_frames),
        every_window ? "yes" : "NO", static_cast<unsigned long long>(windows.allocations()),
        static_cast<unsigned long long>(pipelined_allocations), windows.text().c_str());

  // 11. The harness's mod disconnects: its panels go from the frame, its textures are released after their last frame,
  // and its id connects again with a fresh budget.
  const GraceUiTestState before_gone = grace.state();
  table->disconnect(mod.me);
  bool released_all = false;
  for (int i = 0; i < 30 && !released_all; ++i) {
    frame = game.frame_and_read(kPanelsColour, PresentWith::present);
    released_all = grace.state().textures_ready == 2;  // the example's two
  }
  const GraceUiTestState gone = grace.state();
  std::string gone_why;
  bool gone_ok = pixel_is(frame, 12, 12, kPanelsColour, "where its hud was", gone_why);
  gone_ok = pixel_is(frame, 200, 95, kPanelsColour, "where its modal was", gone_why) && gone_ok;
  gone_ok = pixel_is(frame, hx + 200, hy + 100, kHelloGround, "the example's, still", gone_why) && gone_ok;
  const Connected again_mod = connect(table, request_for("harness-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Harness mod"));
  std::uint64_t fresh = 0;
  const GraceUiStatus fresh_texture = HarnessMod{table, again_mod.answer.consumer, 0}.texture(4, 4, white_pixels, fresh);
  check(gone_ok && released_all && gone.panels == 1 && before_gone.panels == 1 + GRACEUI_PANELS_PER_MOD &&
            gone.textures == 2 &&
            again_mod.status == GRACEUI_OK && fresh_texture == GRACEUI_OK,
        "disconnect",
        "panels %u, then %u; textures ready %u, then %u (released once no frame could draw them); %s; connected again "
        "with a new budget and window: a texture %s",
        before_gone.panels, gone.panels, before_gone.textures_ready, gone.textures_ready, gone_why.c_str(),
        status_name(fresh_texture));
  grace.print_log();
}

// ---- The host review's fixes (2026-11-16): the window filter following the swap chain's window (M2), the faces'
// fallback when a bake runs out of room (M3), the first request's bounded wait (L9) and a long copy's name (L12).

int g_decoy_f5 = 0;  // F5's key-downs that reached the decoy's own procedure

LRESULT CALLBACK decoy_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_KEYDOWN && wparam == VK_F5) {
    ++g_decoy_f5;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

// A loader's window (invented), larger than the game's and made before it, is the one GraceUI's init thread finds first
// (the largest visible window of the process, D120); once the game's swap chain is bound on its own window, the filter
// moves there, and the decoy's filter passes everything on.
void run_decoy_window(const std::wstring& dll) {
  WNDCLASSEXW decoy_class{};
  decoy_class.cbSize = sizeof decoy_class;
  decoy_class.lpfnWndProc = &decoy_procedure;
  decoy_class.hInstance = GetModuleHandleW(nullptr);
  decoy_class.lpszClassName = L"GraceHarnessDecoy";
  RegisterClassExW(&decoy_class);
  const HWND decoy = CreateWindowExW(0, L"GraceHarnessDecoy", L"A loader's window", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0,
                                     0, 1000, 640, nullptr, nullptr, decoy_class.hInstance, nullptr);
  if (decoy == nullptr) {
    fatal("no decoy window");
  }
  pump_messages();
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  grace.set_panel(1);
  for (int i = 0; i < 100 && grace.input().window != reinterpret_cast<std::uintptr_t>(game.window()); ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  for (int i = 0; i < 150 && grace.state().atlas_ready == 0; ++i) {  // the menu opens once its faces are baked
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  frames(game, 10);
  const GraceUiTestInput input = grace.input();
  const bool moved = input.window == reinterpret_cast<std::uintptr_t>(game.window()) && input.windows_filtered == 2 &&
                     grace.logged("filter moved to the bound swap chain's window");
  g_seen = Seen{};
  g_decoy_f5 = 0;
  post_key(decoy, VK_F5, kScanF5, true, false);
  post_key(decoy, VK_F5, kScanF5, false, false);
  post_key(game.window(), VK_F5, kScanF5, true, false);
  post_key(game.window(), VK_F5, kScanF5, false, false);
  frames(game, 3);
  const bool menu_opened = grace.menu().open == 1;  // the game's window's F5 opens GraceUI's menu (D167)
  post_key(game.window(), VK_F5, kScanF5, true, false);
  post_key(game.window(), VK_F5, kScanF5, false, false);
  frames(game, 3);
  const GraceUiTestAnswer granted = grace.request(1, GRACEUI_TEST_KIND_PANEL, GRACEUI_TEST_WANT_KEYBOARD);
  grace.release_input(1);
  check(moved && g_decoy_f5 == 1 && g_seen.key_down[VK_F5] == 0 && menu_opened &&
            granted.answer == GRACEUI_TEST_GRANTED,
        "window followed",
        "the decoy found first, then the filter moved to the bound swap chain's window: %s (%u windows filtered); F5 "
        "reaching the decoy's procedure: %d, the game's: %d, where it %s GraceUI's menu; a request %s",
        moved ? "yes" : "NO", input.windows_filtered, g_decoy_f5, g_seen.key_down[VK_F5],
        menu_opened ? "opened" : "DID NOT OPEN", granted.answer == GRACEUI_TEST_GRANTED ? "granted" : "not granted");
  grace.print_log();
}

// The shared atlas held to 1024 pixels square (a test build's limit): a mod's 96 px face, though within the atlas's
// room as it is counted, no longer fits, so the bake on GraceUI's own thread fails, and is made again with the faces
// before it: the render path stays on for every mod, and the face left out is refused its glyph table.
void run_atlas_full(const std::wstring& dll) {
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_atlas_limit(1024);
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  // The faces before anything is presented, so before warm-up.
  const Connected connected = connect(table, request_for("harness-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Harness mod"));
  const HarnessMod mod{table, connected.answer.consumer, 0};
  const std::vector<unsigned char> noto = read_file(folder_of_this_program() + L"fonts\\NotoSans-Regular.ttf");
  std::uint32_t small_face = 0;
  std::uint32_t large_face = 0;
  const GraceUiStatus small_status = mod.face(noto, 18, small_face);
  const GraceUiStatus large_status = mod.face(noto, 96, large_face);
  GraceUiTestState state{};
  for (int i = 0; i < 1000; ++i) {  // 20 s
    game.frame(kPanelsColour, PresentWith::present);
    state = grace.state();
    if ((state.renderer_ready != 0 && state.atlas_ready != 0) || state.render_off != 0 ||
        state.host == GRACEUI_TEST_HOST_FAILED) {
      break;
    }
    Sleep(20);
  }
  GraceUiGlyphTable small_table{};
  small_table.size = sizeof small_table;
  GraceUiGlyphTable large_table{};
  large_table.size = sizeof large_table;
  const GraceUiStatus small_glyphs = table->glyph_table(mod.me, small_face, &small_table);
  const GraceUiStatus large_glyphs = table->glyph_table(mod.me, large_face, &large_table);
  const bool logged = grace.logged("could not be baked with 2 of the mods' faces") &&
                      grace.logged("leaves out the mods' faces from face");
  check(small_status == GRACEUI_OK && large_status == GRACEUI_OK && state.render_off == 0 &&
            state.atlas_ready == 1 && state.atlas_generation == 1 && state.faces_baked == 2 &&
            state.atlas_width <= 1024 && state.atlas_height <= 1024 && small_glyphs == GRACEUI_OK &&
            large_glyphs == GRACEUI_REFUSED && logged,
        "atlas fallback",
        "faces of 18 and 96 px registered (%s, %s); with the atlas held to 1024 px the first bake failed and the second "
        "left the 96 px face out (%s): render path on %s, atlas ready %u at %ux%u, generation %u, %u faces baked; glyph "
        "tables: the 18 px face %s, the 96 px face %s",
        status_name(small_status), status_name(large_status), logged ? "logged" : "NOT LOGGED",
        state.render_off == 0 ? "yes" : "NO", state.atlas_ready, state.atlas_width, state.atlas_height,
        state.atlas_generation, state.faces_baked, status_name(small_glyphs), status_name(large_glyphs));
  grace.print_log();
}

// The first request slowed by 3 s (a test build's delay): a request made meanwhile on another thread waits 2 s at most,
// then answers null; the first answers its table, and so does the next request.
void run_slow_first(const std::wstring& dll) {
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_first_request_delay(3000);
  std::atomic<const void*> first_answer{nullptr};
  std::thread first([&] { first_answer.store(grace.request_api(1)); });
  Sleep(300);
  const ULONGLONG start = GetTickCount64();
  const void* meanwhile = grace.request_api(1);
  const ULONGLONG waited = GetTickCount64() - start;
  first.join();
  const void* after = grace.request_api(1);
  const bool logged = grace.logged("answered null after 2 s");
  check(meanwhile == nullptr && waited >= 1900 && waited < 2900 && first_answer.load() != nullptr &&
            after == first_answer.load() && logged,
        "first request's wait",
        "a request made while the first was running answered %s after %llu ms (%s); the first answered %s; the next %s",
        meanwhile == nullptr ? "null" : "a table", static_cast<unsigned long long>(waited),
        logged ? "logged" : "NOT LOGGED", first_answer.load() != nullptr ? "its table" : "null",
        after == first_answer.load() && after != nullptr ? "the same table" : "otherwise");
}

// The game moves to a new device (a device removed and made again, or another adapter): GraceUI lets go of the old
// one and of everything it made on it (the review's L3), so that nothing of GraceUI's keeps it alive, and its render
// path stays off for the session. The old device's references are counted by the game, which keeps one.
void run_device_change(const std::wstring& dll) {
  Game first(false);
  ID3D12Device* const old_device = first.device();
  old_device->AddRef();  // the game's own, so that the count can be read after the rest is gone
  first.destroy_swap_chain();
  const ULONG without_chain = references(old_device);
  first.create_swap_chain();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(first, grace);
  grace.set_panel(1);
  for (int i = 0; i < 200 && grace.state().atlas_ready == 0; ++i) {
    first.frame(kRecreated, PresentWith::present);
    Sleep(10);
  }
  for (int i = 0; i < 5; ++i) {
    first.frame(kRecreated, PresentWith::present);
  }
  const GraceUiTestState drawing = grace.state();
  const ULONG while_drawing = references(old_device);
  first.destroy_swap_chain();  // the game's first swap chain ends ...
  Game second(false);          // ... and it goes on with a device of its own on another window
  for (int i = 0; i < 20; ++i) {
    second.frame(kSecondCopy, PresentWith::present);
    Sleep(10);
  }
  const ULONG after = references(old_device);
  const GraceUiTestState state = grace.state();
  const bool logged = grace.logged("device changed") && grace.logged("let go of the game's Direct3D 12 device");
  check(drawing.frames_drawn > 0 && while_drawing > without_chain && after == without_chain && state.render_off == 1 &&
            logged,
        "device change",
        "the old device's references besides the swap chain's: %lu before GraceUI, %lu while it drew (%llu frames), "
        "%lu once the game moved to another device; the render path off: %s; %s",
        static_cast<unsigned long>(without_chain), static_cast<unsigned long>(while_drawing),
        static_cast<unsigned long long>(drawing.frames_drawn), static_cast<unsigned long>(after),
        state.render_off == 1 ? "yes" : "NO", logged ? "logged" : "NOT LOGGED");
  grace.print_log();
  old_device->Release();
}

// A second copy whose name relative to the game's folder is longer than a notice gives it (two folders of 60
// characters of three bytes each in UTF-8, as a Linux file system under Wine takes a name of 255 bytes at most): it is
// named all the same, by the end of its name, from a character's start.
void run_long_name(const std::wstring& dll) {
  const std::wstring outer = folder_of_this_program() + std::wstring(60, L'\u3042');
  const std::wstring folder = outer + L"\\" + std::wstring(60, L'\u3042');
  CreateDirectoryW(outer.c_str(), nullptr);
  CreateDirectoryW(folder.c_str(), nullptr);
  const std::wstring copy = folder + L"\\graceui.dll";
  if (!CopyFileW(dll.c_str(), copy.c_str(), FALSE)) {
    check(false, "long name",
          "the copy's folder could not be made (error %lu): under Wine, run the harness in a UTF-8 locale "
          "(LC_ALL=C.UTF-8), or Wine cannot name a folder in Unicode",
          static_cast<unsigned long>(GetLastError()));
    return;
  }
  GraceUi active;
  GraceUi idle;
  if (!active.load(dll) || !idle.load(copy) || active.module == idle.module) {
    fatal("the two copies of graceui.dll did not load as two modules");
  }
  const void* table = active.request_api(1);
  const void* idle_table = idle.request_api(1);
  (void)active.request_api(1);
  const std::vector<GraceUiTestNotice> notices = active.notices();
  std::string text = notices.empty() ? std::string() : std::string(notices[0].text, notices[0].text_length);
  const std::size_t name = text.find(": ...");
  const std::string ending = "\u3042\\graceui.dll";
  const bool named = notices.size() == 1 && name != std::string::npos &&
                     text.size() >= ending.size() && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
  check(table != nullptr && idle_table == nullptr && named, "long name",
        "a copy %zu bytes deep in UTF-8 named in one notice, by its end after \"...\": %s (%zu bytes of text)",
        std::string("\\graceui.dll").size() + 361, named ? "yes" : "NO", text.size());
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
// ---- graceui-sys with GraceUI not installed (D164): nothing answers, no module is named graceui.dll and the folder
// holds no GraceUI\graceui.dll, so a look says so and GRACEUI_SYS_STOP_IF_ABSENT stops at once, not after 30 s.
// ---- The in-game menu (D167): GraceUI › Menu drawn by the host on F5, as the input holder of kind menu, at 1920 by
// 1080; its keys go to GraceUI's own thread, which answers its settings at its one entry point and lays the page out
// again; the present thread draws the newest list, allocating nothing; the one close routine ends it on every path.
// Set GRACE_SHOTS to a folder to keep two screenshots there for the owner's look review (BMP, the frame as presented).

void save_shot(const Frame& frame, const char* name) {
  char folder[1024];
  const DWORD length = GetEnvironmentVariableA("GRACE_SHOTS", folder, sizeof folder);
  if (length == 0 || length >= sizeof folder) {
    return;
  }
  const std::string path = std::string(folder) + "\\" + name;
  const std::uint32_t row = (frame.width * 3 + 3) & ~3u;
  const std::uint32_t bytes = row * frame.height;
  std::vector<unsigned char> out(54 + static_cast<std::size_t>(bytes), 0);
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
      out[at + i] = static_cast<unsigned char>(value >> (8 * i));
    }
  };
  out[0] = 'B';
  out[1] = 'M';
  put32(2, 54 + bytes);
  put32(10, 54);
  put32(14, 40);
  put32(18, frame.width);
  put32(22, frame.height);
  out[26] = 1;
  out[28] = 24;
  put32(34, bytes);
  std::size_t at = 54;
  for (UINT y = frame.height; y-- > 0;) {  // bottom-up, BGR
    for (UINT x = 0; x < frame.width; ++x) {
      const std::uint32_t pixel = frame.at(x, y);
      out[at + x * 3 + 0] = static_cast<unsigned char>(channel(pixel, 16));
      out[at + x * 3 + 1] = static_cast<unsigned char>(channel(pixel, 8));
      out[at + x * 3 + 2] = static_cast<unsigned char>(channel(pixel, 0));
    }
    at += row;
  }
  const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  DWORD written = 0;
  const bool ok = file != INVALID_HANDLE_VALUE &&
                  WriteFile(file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr) && written == out.size();
  if (file != INVALID_HANDLE_VALUE) {
    CloseHandle(file);
  }
  if (!ok) {
    std::printf("   (could not write %s)\n", name);
    return;
  }
  std::printf("   (saved %s)\n", name);
}

// Frames until the menu has published more than `lists` lists and drawn one of them, at most 3 s.
GraceUiTestMenu menu_after(Game& game, const GraceUi& grace, std::uint64_t lists) {
  GraceUiTestMenu menu = grace.menu();
  for (int i = 0; i < 150 && (menu.lists <= lists || menu.frames == 0); ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
    menu = grace.menu();
  }
  frames(game, 2);
  return grace.menu();
}

void press(HWND window, UINT vk, UINT scan) {
  post_key(window, vk, scan, true, false);
  post_key(window, vk, scan, false, false);
}

void run_menu(const std::wstring& dll) {
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  for (int i = 0; i < 150 && grace.state().atlas_ready == 0; ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  const GraceUiTestState warm = grace.state();
  const GraceUiTestMenu faces = grace.menu();
  check(warm.atlas_ready == 1 && warm.faces_baked == 1 && faces.faces == 11 &&
            std::fabs(faces.baked_scale - 1.25f) < 0.001f,
        "menu's faces",
        "the atlas, %ux%u, holds the notices' face and the menu's %u, one a type role, baked at %.2f, the effective "
        "scale (1080 / 1080 x ui.scale's default 1.25)",
        warm.atlas_width, warm.atlas_height, faces.faces, faces.baked_scale);

  // 1. F5 opens the menu: the menu holds input (GraceUI's, owner 0), its first list is laid out on GraceUI's thread and
  // drawn; the Menu page, focus on its first control; the press never reaches the game.
  g_seen = Seen{};
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  GraceUiTestInput input = grace.input();
  const Frame grace_shot = game.frame_and_read(kInputColour, PresentWith::present);
  check(menu.open == 1 && input.holder_kind == GRACEUI_TEST_KIND_MENU && input.holder_owner == 0 && menu.frames > 0 &&
            std::strcmp(menu.page, "graceui.menu") == 0 && std::strcmp(menu.focused, "setting:ui.scale") == 0 &&
            std::fabs(menu.scale - 1.25f) < 0.001f && g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] == 0,
        "menu opens",
        "F5: the menu %s input (holder kind %u, owner %u); %llu lists laid out at %.2f, %llu frames drawn; page %s, "
        "focus %s; "
        "the last layout took %u us on GraceUI's thread, %u vertices, %u indices, %u commands; F5's messages reaching "
        "the game: %d",
        menu.open != 0 ? "holds" : "DOES NOT HOLD", input.holder_kind, input.holder_owner,
        static_cast<unsigned long long>(menu.lists), menu.scale, static_cast<unsigned long long>(menu.frames), menu.page,
        menu.focused, menu.layout_us, menu.vertices, menu.indices, menu.commands,
        g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5]);
  // The page drew: the rail's ground at the left, the page's at the right, neither the game's clear colour.
  const int game_left = count_near(grace_shot, 20, 400, 200, 600, Colour{kInputColour.r, kInputColour.g, kInputColour.b}, 4);
  const int game_right = count_near(grace_shot, 900, 600, 1500, 900, Colour{kInputColour.r, kInputColour.g, kInputColour.b}, 4);
  check(game_left < 20 && game_right < 60, "menu drawn",
        "the rail and the page cover the game (pixels near the game's own colour, text's edges among them: %d of "
        "36000 in the rail, %d of 180000 in the page)",
        game_left, game_right);
  save_shot(grace_shot, "menu-grace-1920x1080.bmp");
  // The Grace pointer (G31, D178) at the mouse: its gold edge in the box it covers at 1.25, and none of Dear ImGui's
  // white arrow.
  {
    const GraceUiTestInput at = grace.input();
    const UINT x = static_cast<UINT>(std::max(at.imgui_mouse_x, 2));
    const UINT y = static_cast<UINT>(std::max(at.imgui_mouse_y, 2));
    const int gold = count_near(grace_shot, x, y, x + 20, y + 26, Colour{0xE3, 0xC5, 0x79}, 24);
    const int white = count_near(grace_shot, x - 2, y - 2, x + 22, y + 28, Colour{255, 255, 255}, 8);
    check(at.imgui_draws_cursor == 1 && gold >= 30 && white == 0, "menu: the Grace pointer",
          "at %u,%u: %d pixels of its gold edge, %d white", x, y, gold, white);
  }

  // 2. → raises ui.scale by its step: GraceUI's store takes it, and the page is laid out again at the new scale.
  press(window, VK_RIGHT, 0x4D);
  menu = menu_after(game, grace, menu.lists);
  const std::string scale = grace.setting("ui.scale");
  check(scale == "1.3" && std::fabs(menu.scale - 1.3f) < 0.001f && menu.refusals == 0, "menu: a slider",
        "→ on Menu size: ui.scale %s, laid out again at %.2f", scale.c_str(), menu.scale);

  // 3. ↓ and → choose Grace Clear: its colours replace Grace's in the next frame; R resets it, and the scale too.
  press(window, VK_DOWN, 0x50);
  press(window, VK_RIGHT, 0x4D);
  menu = menu_after(game, grace, menu.lists);
  const std::string clear = grace.setting("ui.menu.theme");
  const Frame clear_shot = game.frame_and_read(kInputColour, PresentWith::present);
  int changed_pixels = 0;
  for (std::size_t i = 0; i < clear_shot.pixels.size() && i < grace_shot.pixels.size(); ++i) {
    changed_pixels += clear_shot.pixels[i] != grace_shot.pixels[i] ? 1 : 0;
  }
  const bool colours_changed = changed_pixels > 10000;
  save_shot(clear_shot, "menu-clear-1920x1080.bmp");
  press(window, 'R', 0x13);
  press(window, VK_UP, 0x48);
  press(window, 'R', 0x13);
  menu = menu_after(game, grace, menu.lists);
  check(clear == "\"clear\"" && colours_changed && grace.setting("ui.menu.theme") == "\"grace\"" &&
            grace.setting("ui.scale") == "1.25" && std::strcmp(menu.focused, "setting:ui.scale") == 0,
        "menu: a choice and reset",
        "→ on Theme: %s, %d pixels redrawn in Grace Clear; R reset it to %s and, on Menu size, ui.scale to %s",
        clear.c_str(), changed_pixels, grace.setting("ui.menu.theme").c_str(), grace.setting("ui.scale").c_str());

  // 4. Its frames cost the present thread little and allocate nothing (invariant 10): the list is lent, never copied.
  // At 1920 by 1080 Mesa's software renderer fills the screen on the machine's own cores, which the present thread
  // shares, so the budget is held at the other scenarios' 640 by 360; the full size is measured and printed.
  const CostWindows full = cost_windows(grace, 1000.0, [&] { frames(game, 120); });
  const GraceUiTestMenu counted = grace.menu();
  game.resize(640, 360);
  menu = menu_after(game, grace, grace.menu().lists);
  const CostWindows compact = cost_windows(grace, 1000.0, [&] { frames(game, 120); });
  check(compact.best().costs.own.p99_us <= 1000.0 && compact.allocations() == 0 && full.allocations() == 0 &&
            compact.every([](const CostWindow& run) { return run.after.frames_drawn > run.before.frames_drawn; }),
        "menu frame cost",
        "with the menu open, own work median / p99 in us: at 640x360 %s (budget: p99 1000 us, the least window's); at "
        "1920x1080 %s (measured); allocations and frees on the present thread: %llu; the list at 1920x1080: %u "
        "vertices, %u indices, %u commands, laid out in %u us on GraceUI's thread",
        compact.text().c_str(), full.text().c_str(),
        static_cast<unsigned long long>(compact.allocations() + full.allocations()), counted.vertices, counted.indices,
        counted.commands, counted.layout_us);
  game.resize(1920, 1080);
  menu = menu_after(game, grace, grace.menu().lists);

  // 5. Every way out ends it through the one close routine: Esc to the rail and Esc again (released); F5 (the menu
  // key); a provider's danger (invariant 9), after which F5 opens nothing and says why. It reopens where it was.
  press(window, VK_ESCAPE, kScanEscape);
  press(window, VK_ESCAPE, kScanEscape);
  frames(game, 4);
  for (int i = 0; i < 50 && grace.menu().open != 0; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  const GraceUiTestInput escaped = grace.input();
  press(window, VK_F5, kScanF5);
  menu = menu_after(game, grace, grace.menu().lists);
  const std::string reopened_on = menu.page;
  press(window, VK_F5, kScanF5);
  frames(game, 3);
  const GraceUiTestInput by_key = grace.input();
  press(window, VK_F5, kScanF5);
  menu = menu_after(game, grace, grace.menu().lists);
  grace.set_world(GRACEUI_TEST_WORLD_DANGER);
  frames(game, 3);
  const GraceUiTestInput by_danger = grace.input();
  const std::uint32_t notices_before = static_cast<std::uint32_t>(grace.notices().size());
  press(window, VK_F5, kScanF5);
  frames(game, 3);
  std::string why_not;
  for (const GraceUiTestNotice& notice : grace.notices()) {
    if (notice.owner == 0 && std::strstr(notice.text, "Not now") != nullptr) {
      why_not = notice.text;
    }
  }
  const GraceUiTestMenu after_danger = grace.menu();
  grace.set_world(0);
  frames(game, 2);
  check(escaped.holder_kind == 0 && escaped.last_close_reason == GRACEUI_TEST_CLOSED_RELEASED &&
            reopened_on == "graceui.menu" && by_key.holder_kind == 0 &&
            by_key.last_close_reason == GRACEUI_TEST_CLOSED_MENU_KEY && by_danger.holder_kind == 0 &&
            by_danger.last_close_reason == GRACEUI_TEST_CLOSED_DANGER && after_danger.open == 0 &&
            why_not == "Not now: you are in danger." && g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] == 0 &&
            g_seen.key_down[VK_ESCAPE] + g_seen.key_up[VK_ESCAPE] == 0,
        "menu closes",
        "Esc, Esc: %s; F5 reopened it on %s and F5 closed it (%s); danger closed it (%s), and F5 then said \"%s\" "
        "(%u notices before); F5's and Esc's messages reaching the game: %d",
        reason_name(escaped.last_close_reason), reopened_on.c_str(), reason_name(by_key.last_close_reason),
        reason_name(by_danger.last_close_reason), why_not.c_str(), notices_before,
        g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] + g_seen.key_down[VK_ESCAPE] + g_seen.key_up[VK_ESCAPE]);
  check(grace.menu().off == 0 && grace.state().render_off == 0, "menu: still on", "no fault in the menu or the render "
        "path after every check");
  grace.print_log();
}

// ---- The spaces across the ABI (D169), end to end in graceui.dll under Wine: a mod registers the demo space for both
// views, and GraceUI's server starts for it; the harness is a browser too (the server suite's client), taking the
// session's cookie from the GraceUI folder's page, a ticket, and a WebSocket whose welcome names both spaces. A view's
// sub reaches the mod as a frame from poll and its state reaches the view; a command reaches it with a reply handle (a
// buffer too small kept for the next poll), and its answer reaches the one view that asked, once. A setting the browser
// changes is the menu's too (one store). When the mod disconnects, its space goes, and the server stops: no socket.

struct SpaceEvent {
  GraceUiStatus status = GRACEUI_FAULT;
  std::uint32_t kind = GRACEUI_EVENT_NONE;
  std::uint64_t reply = 0;
  std::uint64_t length = 0;
  std::string frame;
};

SpaceEvent poll_space(const GraceUiApi1* table, GraceUiConsumer mod, std::uint64_t capacity = 16384) {
  std::vector<char> buffer(static_cast<std::size_t>(capacity) + 1);
  GraceUiEvent event{};
  event.size = sizeof event;
  event.frame = buffer.data();
  event.frame_capacity = capacity;
  SpaceEvent out;
  out.status = table->poll(mod, &event);
  out.kind = event.kind;
  out.reply = event.reply;
  out.length = event.frame_length;
  if (out.status == GRACEUI_OK) {
    out.frame.assign(buffer.data(), static_cast<std::size_t>(event.frame_length));
  }
  return out;
}

// Polls (with frames between) until the mod has an event of its space, at most 3 s. The display's event (D180), which
// every mod polling at 1.2's size gets after warm-up, is not a space's: it is passed over.
SpaceEvent wait_space(Game& game, const GraceUiApi1* table, GraceUiConsumer mod, std::uint64_t capacity = 16384) {
  SpaceEvent event;
  for (int i = 0; i < 150; ++i) {
    event = poll_space(table, mod, capacity);
    if (event.status == GRACEUI_OK && event.kind == GRACEUI_EVENT_DISPLAY) {
      continue;
    }
    if (event.status != GRACEUI_OK || event.kind != GRACEUI_EVENT_NONE) {
      return event;
    }
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  return event;
}

// A test build's free port (D234): with GRACEUI_TEST_PORT=free the browser view is served on a port the system gave,
// not the settings' 8642, and the page loads there; GraceUI's settings file is not changed.
// ---- D235: the test build's heap guard. GraceUI's own blocks are guarded from the DLL's load and checked on GraceUI's
// thread; each kind of breach a probe makes is found and recorded (not raised), with its block's size and a maker in
// graceui.dll, and GraceUI goes on drawing. A breach that is not a probe's ends its process: a handler first in the
// process's list (as the spike observer's recorder is) sees it first, and without one GraceUI's own ends the run.
bool in_module(std::uint64_t address, HMODULE module) {
  HMODULE found = nullptr;
  // SAFETY: an address given as a number, read by GetModuleHandleExW only to find the module it lies in.
  return address != 0 &&
         GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(static_cast<std::uintptr_t>(address)), &found) &&
         found == module;
}

std::wstring this_program();
int run_child(const std::wstring& dll, const wchar_t* scenario, std::wstring program);

void run_heap_guard(const std::wstring& dll) {
  SetEnvironmentVariableW(L"GRACEUI_TEST_HEAP_CHECK_MS", L"200");
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  frames(game, 5);
  GraceUiTestHeapGuard on = grace.heap_guard();
  for (int i = 0; i < 150 && (on.checks < 2 || on.stats_read == 0); ++i) {
    frames(game, 1);
    Sleep(20);
    on = grace.heap_guard();
  }
  check(on.stats_read == 1 && on.check_period_ms == 200 && on.made > 0 && on.live_blocks > 0 && on.checks >= 2 &&
            on.blocks_checked > 0 && on.breaches == 0 && on.breach.breach == 0 && on.last_check_ms != 0,
        "heap guard: GraceUI's own blocks guarded and checked",
        "%llu blocks made, %llu live (%llu bytes), %llu kept after their free; %llu whole checks every %u ms, over "
        "%llu blocks; %llu breaches",
        static_cast<unsigned long long>(on.made), static_cast<unsigned long long>(on.live_blocks),
        static_cast<unsigned long long>(on.live_bytes), static_cast<unsigned long long>(on.quarantined_blocks),
        static_cast<unsigned long long>(on.checks), on.check_period_ms,
        static_cast<unsigned long long>(on.blocks_checked), static_cast<unsigned long long>(on.breaches));

  struct Kind {
    std::uint32_t kind;
    const char* name;
    std::uint64_t size;
  };
  const Kind kinds[] = {{1, "written before its start", 24}, {2, "written past its end", 24},
                        {3, "written after its free", 24},   {4, "freed twice", 24},
                        {5, "not a block of the guard's", 0}};
  const std::uint64_t presents = grace.state().presents;
  bool found_each = true;
  std::string detail;
  for (const Kind& k : kinds) {
    const std::uint32_t found = grace.heap_guard_probe(k.kind, 0);
    const GraceUiTestHeapGuard after = grace.heap_guard();
    const bool maker = k.kind == 5 ? after.probe.maker[0] == 0 : in_module(after.probe.maker[0], grace.module);
    const bool sound = found == k.kind && after.probe.breach == k.kind && after.probe.size == k.size && maker &&
                       after.breach.breach == 0;
    found_each = found_each && sound;
    char line[160];
    std::snprintf(line, sizeof line, "%s%s: %s (%llu bytes, made %s)", detail.empty() ? "" : "; ", k.name,
                  sound ? "found" : "NOT FOUND AS IT SHOULD BE", static_cast<unsigned long long>(after.probe.size),
                  k.kind == 5 ? "by none" : maker ? "in graceui.dll" : "ELSEWHERE");
    detail += line;
  }
  // GraceUI goes on: the game presents through its detour, and its thread checks every block again.
  const std::uint64_t checks = grace.heap_guard().checks;
  GraceUiTestHeapGuard later = grace.heap_guard();
  for (int i = 0; i < 150 && later.checks < checks + 2; ++i) {
    frames(game, 1);
    Sleep(20);
    later = grace.heap_guard();
  }
  const std::uint64_t presents_after = grace.state().presents;
  check(found_each && presents_after > presents && later.checks >= checks + 2 && later.breach.breach == 0,
        "heap guard: each kind of breach found, with its block",
        "%s; then %llu presents through GraceUI's detour and %llu more whole checks, the session's breach none",
        detail.c_str(), static_cast<unsigned long long>(presents_after - presents),
        static_cast<unsigned long long>(later.checks - checks));
  SetEnvironmentVariableW(L"GRACEUI_TEST_HEAP_CHECK_MS", nullptr);

  for (const wchar_t* child : {L"heap-guard-seen", L"heap-guard-end"}) {
    std::fflush(stdout);
    const auto status = static_cast<DWORD>(run_child(dll, child, this_program()));
    const bool seen = std::wcscmp(child, L"heap-guard-seen") == 0;
    // GraceUI's own handler fails fast; Wine's RaiseFailFastException ends a process with STATUS_FAIL_FAST_EXCEPTION.
    const bool ended = status == GRACEUI_TEST_HEAP_GUARD_BREACH || (!seen && status == 0xC0000602u);
    check(ended, seen ? "heap guard: a breach seen first by a handler before GraceUI's"
                      : "heap guard: a breach ends its process, by GraceUI's own handler",
          "its process ended with %#lx%s", static_cast<unsigned long>(status),
          status == GRACEUI_TEST_HEAP_GUARD_BREACH ? " (the guard's code)"
          : status == 0xC0000602u                  ? " (a fail-fast)"
                                                   : " (NOT A BREACH'S END)");
  }
}

// The first handler in the list, as the spike observer's recorder is: it sees the breach and ends the process with its
// code, so that the harness knows it was seen before GraceUI's own handler.
LONG CALLBACK harness_sees_breach(EXCEPTION_POINTERS* pointers) {
  if (pointers->ExceptionRecord->ExceptionCode == GRACEUI_TEST_HEAP_GUARD_BREACH) {
    TerminateProcess(GetCurrentProcess(), GRACEUI_TEST_HEAP_GUARD_BREACH);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void run_heap_guard_child(const std::wstring& dll, bool seen) {
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  if (grace.request_api(1) == nullptr) {  // the first request: the guard's own handler is added, last
    fatal("GraceUI_RequestApi(1) answered null");
  }
  if (seen) {
    AddVectoredExceptionHandler(1, &harness_sees_breach);
  }
  grace.heap_guard_probe(2, 1);  // a breach raised as a real one: it never returns
  std::printf("FAIL heap guard: a raised breach returned\n");
  std::fflush(stdout);
  ExitProcess(3);
}

void run_free_port(const std::wstring& dll) {
  namespace io = grace::serve::iotest;
  SetEnvironmentVariableW(L"GRACEUI_TEST_PORT", L"free");
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  GraceUiSpaceDesc desc{};
  desc.size = sizeof desc;
  desc.views = GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER;
  desc.version = GRACEUI_VERSION_PACK(1, 0, 0);
  desc.pages = space.pages.data();
  desc.pages_length = space.pages.size();
  desc.settings = space.settings.data();
  desc.settings_length = space.settings.size();
  desc.increment = "0.2";
  desc.increment_length = 3;
  const GraceUiStatus registered = table->register_pages(demo.answer.consumer, &desc);
  GraceUiTestServer server = grace.server();
  for (int i = 0; i < 150 && server.running == 0; ++i) {
    frames(game, 1);
    Sleep(20);
    server = grace.server();
  }
  io::start_client_network();
  const auto port = static_cast<std::uint16_t>(server.port);
  const io::Reply page = server.running != 0 ? io::request(port, io::page_load(port)) : io::Reply{};
  const std::string kept = grace.setting("dashboard.port");
  check(registered == GRACEUI_OK && server.running == 1 && server.port != 8642 && server.port >= 1024 &&
            page.status == 200 && kept == "8642",
        "free port: the browser view on a port of the system's",
        "GRACEUI_TEST_PORT=free: the server %s on port %u; its page: %d; the settings' dashboard.port still %s",
        server.running != 0 ? "runs" : "DOES NOT RUN", server.port, page.status, kept.c_str());
  SetEnvironmentVariableW(L"GRACEUI_TEST_PORT", nullptr);
}

void run_pages(const std::wstring& dll) {
  namespace io = grace::serve::iotest;
  using grace::protocol::Json;
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);

  // 1. Registration: checked, one space a mod, its id the mod's; the server starts for a browser space.
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const Connected other = connect(table, request_for("lantern"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const auto describe = [&](std::string_view pages, std::uint32_t views) {
    GraceUiSpaceDesc desc{};
    desc.size = sizeof desc;
    desc.views = views;
    desc.version = GRACEUI_VERSION_PACK(1, 0, 0);
    desc.pages = pages.data();
    desc.pages_length = pages.size();
    desc.settings = space.settings.data();
    desc.settings_length = space.settings.size();
    desc.increment = "0.2";
    desc.increment_length = 3;
    return desc;
  };
  const GraceUiSpaceDesc both = describe(space.pages, GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER);
  const GraceUiStatus registered = table->register_pages(demo.answer.consumer, &both);
  const GraceUiStatus twice = table->register_pages(demo.answer.consumer, &both);
  const GraceUiStatus not_its_own = table->register_pages(other.answer.consumer, &both);
  const GraceUiSpaceDesc no_view = describe(space.pages, 0);
  const GraceUiStatus viewless = table->register_pages(other.answer.consumer, &no_view);
  GraceUiTestServer server = grace.server();
  for (int i = 0; i < 150 && server.running == 0; ++i) {
    frames(game, 1);
    Sleep(20);
    server = grace.server();
  }
  check(registered == GRACEUI_OK && twice == GRACEUI_DUPLICATE_ID && not_its_own == GRACEUI_REFUSED &&
            viewless == GRACEUI_REFUSED && server.running == 1 && server.spaces == 1 && server.port == 8642,
        "spaces: register",
        "the demo space: %s, again %s; another mod's register of it %s, with no view %s; the server %s on port %u "
        "with %u space(s)",
        status_name(registered), status_name(twice), status_name(not_its_own), status_name(viewless),
        server.running != 0 ? "runs" : "DOES NOT RUN", server.port, server.spaces);
  if (server.running == 0) {
    grace.print_log();
    return;
  }

  // 2. The browser: the page's cookie, a ticket, and a WebSocket whose welcome names GraceUI's space and the demo's.
  io::start_client_network();
  const std::uint16_t port = static_cast<std::uint16_t>(server.port);
  const io::Reply page = io::request(port, io::page_load(port));
  const std::string cookie = io::cookie_from(page);
  const std::string ticket = cookie.empty() ? std::string() : io::fetch_ticket(port, cookie);
  io::WsClient view;
  Json welcome;
  const bool joined = !ticket.empty() && view.join(port, cookie, ticket, &welcome);
  std::string names;
  if (joined) {
    for (const Json& named : welcome["spaces"]) {
      names += (names.empty() ? "" : ", ") + named["id"].get<std::string>();
    }
  }
  check(page.status == 200 && !cookie.empty() && joined && names == "graceui, demo", "browser view: joins",
        "the page %d with %s; a ticket %s; the WebSocket %s, its welcome naming %s", page.status,
        cookie.empty() ? "NO COOKIE" : "the session's cookie", ticket.empty() ? "REFUSED" : "taken",
        joined ? "joined" : "REFUSED", names.c_str());
  if (!joined) {
    grace.print_log();
    return;
  }

  // 3. A view's sub reaches the mod as a sub frame; its state reaches the view.
  view.send_json({{"v", 1}, {"t", "sub"}, {"space", "demo"}, {"id", 1}, {"topic", "lantern.status"}});
  const SpaceEvent watched = wait_space(game, table, demo.answer.consumer);
  const std::string state_frame =
      R"({"v":1,"t":"state","space":"demo","topic":"lantern.status","rev":1,"body":{"sentence":"Burning steady."}})";
  const GraceUiStatus published = table->publish(demo.answer.consumer, state_frame.data(), state_frame.size());
  const std::string wrong_topic =
      R"({"v":1,"t":"state","space":"demo","topic":"lantern.smoke","rev":1,"body":{}})";
  const GraceUiStatus undeclared = table->publish(demo.answer.consumer, wrong_topic.data(), wrong_topic.size());
  const std::optional<Json> state = view.next_of("state", std::chrono::milliseconds(3000));
  check(watched.kind == GRACEUI_EVENT_WATCHED && watched.frame.find("\"lantern.status\"") != std::string::npos &&
            published == GRACEUI_OK && undeclared == GRACEUI_REFUSED && state &&
            (*state)["body"]["sentence"] == "Burning steady.",
        "spaces: sub and state", "the mod polled %s; its state %s and reached the view: %s; an undeclared topic's %s",
        watched.kind == GRACEUI_EVENT_WATCHED ? watched.frame.c_str() : "NO SUB", status_name(published),
        state ? "yes" : "NO", status_name(undeclared));

  // 4. A command: a buffer too tight keeps it; its answer reaches the view that asked, once.
  view.send_json({{"v", 1}, {"t", "cmd"}, {"space", "demo"}, {"id", 7}, {"name", "lantern.trim"},
                  {"args", Json::object()}});
  const SpaceEvent tight = wait_space(game, table, demo.answer.consumer, 8);
  const SpaceEvent command = poll_space(table, demo.answer.consumer);
  const std::string ok = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
  const GraceUiStatus answered = table->answer(demo.answer.consumer, command.reply, ok.data(), ok.size());
  const GraceUiStatus again = table->answer(demo.answer.consumer, command.reply, ok.data(), ok.size());
  const std::optional<Json> result = view.next_of("result", std::chrono::milliseconds(3000));
  check(tight.status == GRACEUI_FULL && tight.kind == GRACEUI_EVENT_COMMAND && tight.length == command.length &&
            command.kind == GRACEUI_EVENT_COMMAND && command.reply != 0 &&
            command.frame.find("\"lantern.trim\"") != std::string::npos && answered == GRACEUI_OK &&
            again == GRACEUI_REFUSED && result && (*result)["id"] == 7 && (*result)["ok"] == true,
        "spaces: a command and its answer",
        "8 bytes: %s (%llu needed); then %s with reply %llu; answered %s, again %s; the view's result %s",
        status_name(tight.status), static_cast<unsigned long long>(tight.length), command.frame.c_str(),
        static_cast<unsigned long long>(command.reply), status_name(answered), status_name(again),
        result ? result->dump().c_str() : "NONE");

  // 5. GraceUI's own space in the browser: a setting set there is the menu's too.
  view.send_json({{"v", 1}, {"t", "cmd"}, {"space", "graceui"}, {"id", 8}, {"name", "settings.set"},
                  {"args", {{"key", "ui.scale"}, {"value", 1.1}}}});
  std::optional<Json> set;
  for (int i = 0; i < 30 && !set; ++i) {
    frames(game, 1);
    set = view.next_of("result", std::chrono::milliseconds(100));
  }
  const std::string scale = grace.setting("ui.scale");
  check(set && (*set)["ok"] == true && scale == "1.1", "spaces: one store for both views",
        "settings.set ui.scale 1.1 from the browser: %s; the menu's store holds %s", set ? set->dump().c_str() : "NONE",
        scale.c_str());

  // 5b. An upload (G28, D226): the view's ticket and POST; the mod's poll with no room for the file is full, naming its
  // size; with room, the frame and the file, once; its answer reaches the view.
  std::string roster;
  while (roster.size() < 100000) {
    roster += "[[keeper]]\nname = \"Invented keeper\"\npost = \"North wall\"\n";
  }
  grace::serve::Sha256 sha;
  sha.update(roster);
  std::string hash;
  for (const std::uint8_t b : sha.finish()) {
    constexpr std::string_view kHex = "0123456789abcdef";
    hash += kHex[b >> 4];
    hash += kHex[b & 0xF];
  }
  view.send_json({{"v", 1}, {"t", "upload"}, {"space", "demo"}, {"id", 9}, {"name", "keepers.import"},
                  {"args", Json::object()}, {"size", roster.size()}, {"sha256", hash}});
  const std::optional<Json> upload_ticket = view.next_of("upload_ticket", std::chrono::milliseconds(3000));
  const std::string upload_head =
      "POST /upload HTTP/1.1\r\nHost: " + io::host_of(port) + "\r\nOrigin: " + io::origin_of(port) +
      "\r\nSec-Fetch-Site: same-origin\r\nContent-Type: application/octet-stream\r\nCookie: en_session=" + cookie +
      "\r\nGrace-Upload-Ticket: " + (upload_ticket ? (*upload_ticket)["ticket"].get<std::string>() : std::string()) +
      "\r\nContent-Length: " + std::to_string(roster.size()) + "\r\n\r\n";
  const io::Reply uploaded = io::request(port, upload_head + roster, std::chrono::milliseconds(10000));
  GraceUiEvent no_room{};
  std::vector<char> frame_buffer(16384);
  GraceUiStatus first = GRACEUI_OK;
  for (int i = 0; i < 150; ++i) {
    no_room = GraceUiEvent{};
    no_room.size = sizeof no_room;
    no_room.frame = frame_buffer.data();
    no_room.frame_capacity = frame_buffer.size();
    first = table->poll(demo.answer.consumer, &no_room);
    if (first != GRACEUI_OK || (no_room.kind != GRACEUI_EVENT_NONE && no_room.kind != GRACEUI_EVENT_DISPLAY)) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  std::vector<std::uint8_t> file_buffer(GRACEUI_UPLOAD_MAX);
  GraceUiEvent with_room{};
  with_room.size = sizeof with_room;
  with_room.frame = frame_buffer.data();
  with_room.frame_capacity = frame_buffer.size();
  with_room.file = file_buffer.data();
  with_room.file_capacity = file_buffer.size();
  const GraceUiStatus second = table->poll(demo.answer.consumer, &with_room);
  const std::string upload_frame(frame_buffer.data(), static_cast<std::size_t>(with_room.frame_length));
  const bool same = with_room.file_length == roster.size() &&
                    std::equal(roster.begin(), roster.end(), file_buffer.begin(),
                               [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
  const std::string done_frame = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
  const GraceUiStatus done = table->answer(demo.answer.consumer, with_room.reply, done_frame.data(), done_frame.size());
  std::optional<Json> upload_result;
  for (int i = 0; i < 30 && !upload_result; ++i) {
    frames(game, 1);
    upload_result = view.next_of("result", std::chrono::milliseconds(100));
  }
  check(upload_ticket.has_value() && uploaded.status == 202 && first == GRACEUI_FULL &&
            no_room.kind == GRACEUI_EVENT_COMMAND && no_room.file_length == roster.size() && second == GRACEUI_OK &&
            with_room.kind == GRACEUI_EVENT_COMMAND && upload_frame.find("\"keepers.import\"") != std::string::npos &&
            same && done == GRACEUI_OK && upload_result && (*upload_result)["id"] == 9 && (*upload_result)["ok"] == true,
        "spaces: an upload to the mod (D226)",
        "a ticket %s; the POST %d; no room: %s, %llu bytes named; with room: %s, the file %s; answered %s; the view's "
        "result %s",
        upload_ticket ? "given" : "NOT GIVEN", uploaded.status, status_name(first),
        static_cast<unsigned long long>(no_room.file_length), status_name(second), same ? "whole" : "NOT WHOLE",
        status_name(done), upload_result ? upload_result->dump().c_str() : "NONE");

  // With GRACE_HOLD_S set, the mod stays for that many seconds and answers as the demo space would, so that a real
  // browser can be pointed at the server meanwhile (BUILD.md): fixture states on watched, ok to every command.
  char hold_text[16] = {};
  const DWORD hold_length = GetEnvironmentVariableA("GRACE_HOLD_S", hold_text, sizeof hold_text);
  const int hold_s = hold_length > 0 && hold_length < sizeof hold_text ? std::atoi(hold_text) : 0;
  if (hold_s > 0) {
    std::printf("   (holding %d s: the browser view is at http://127.0.0.1:%u/)\n", hold_s, server.port);
    std::fflush(stdout);
    const Json fixtures = Json::parse(R"({"lantern.status":{"tiles":[{"label":"Flame","value":"Steady"},)"
                                      R"({"label":"Oil","value":"62%"}],"sentence":"The lantern burns steady amber."},)"
                                      R"("settings":null})");
    std::uint64_t rev = 10;
    const DWORD until = GetTickCount() + static_cast<DWORD>(hold_s) * 1000u;
    while (GetTickCount() < until) {
      const SpaceEvent event = poll_space(table, demo.answer.consumer);
      if (event.kind == GRACEUI_EVENT_WATCHED) {
        const Json sub = Json::parse(event.frame);
        const std::string topic = sub["topic"].get<std::string>();
        Json body;
        if (topic == "settings") {
          body = {{"schema", Json::parse(space.settings)["schema"]}, {"description", Json::parse(space.settings)["settings"]},
                  {"values", Json::object()}, {"changed", Json::array()}};
        } else if (fixtures.contains(topic)) {
          body = fixtures[topic];
        }
        if (!body.is_null()) {
          const std::string frame_text =
              Json{{"v", 1}, {"t", "state"}, {"space", "demo"}, {"topic", topic}, {"rev", ++rev}, {"body", body}}.dump();
          table->publish(demo.answer.consumer, frame_text.data(), frame_text.size());
        }
      } else if (event.kind == GRACEUI_EVENT_COMMAND) {
        const std::string done = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
        table->answer(demo.answer.consumer, event.reply, done.data(), done.size());
      }
      game.frame(kInputColour, PresentWith::present);
      Sleep(15);
    }
  }

  // 6. The mod disconnects: its space goes, the server stops, the view is told and closed.
  table->disconnect(demo.answer.consumer);
  table->disconnect(other.answer.consumer);
  for (int i = 0; i < 150 && grace.server().running != 0; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  server = grace.server();
  const bool closed = view.closed(std::chrono::milliseconds(3000));
  check(server.running == 0 && server.spaces == 0 && closed && grace.state().render_off == 0, "spaces: disconnect",
        "the server %s with %u space(s) left; the view %s", server.running == 0 ? "stopped" : "STILL RUNS",
        server.spaces, closed ? "closed" : "STILL OPEN");
  grace.print_log();
}

// ---- The menu on mods' spaces (D171), end to end in graceui.dll under Wine: a mod registers the demo space for the
// game's view only, and F5 opens the menu on it; the menu watches its settings as a view does, its commands reach the
// mod with a reply handle, a refusal's words show under the row; a second mod's space puts a switcher on the rail; a
// link at the rail's foot opens GraceUI's own, whose head links back.

GraceUiSpaceDesc game_space(std::string_view pages, std::string_view settings) {
  GraceUiSpaceDesc desc{};
  desc.size = sizeof desc;
  desc.views = GRACEUI_VIEW_GAME;
  desc.version = GRACEUI_VERSION_PACK(1, 0, 0);
  desc.pages = pages.data();
  desc.pages_length = pages.size();
  desc.settings = settings.data();
  desc.settings_length = settings.size();
  desc.increment = "0.2";
  desc.increment_length = 3;
  return desc;
}

// The mod's next event of `kind`, the others it polls on the way dropped, at most 3 s.
SpaceEvent wait_kind(Game& game, const GraceUiApi1* table, GraceUiConsumer mod, std::uint32_t kind) {
  SpaceEvent event;
  for (int i = 0; i < 150; ++i) {
    event = poll_space(table, mod);
    if (event.status != GRACEUI_OK || event.kind == kind) {
      return event;
    }
    if (event.kind == GRACEUI_EVENT_NONE) {
      game.frame(kInputColour, PresentWith::present);
      Sleep(20);
    }
  }
  return event;
}

void run_menu_space(const std::wstring& dll) {
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);

  // 1. The demo space, for the game's view only: registered, and no server, which only a browser space starts.
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const GraceUiSpaceDesc desc = game_space(space.pages, space.settings);
  const GraceUiStatus registered = table->register_pages(demo.answer.consumer, &desc);
  frames(game, 5);
  const GraceUiTestServer server = grace.server();
  check(registered == GRACEUI_OK && server.running == 0 && server.spaces == 1, "menu space: register",
        "the demo space for the game's view: %s; the server %s, %u space(s)", status_name(registered),
        server.running == 0 ? "not started" : "RUNS", server.spaces);

  // 2. F5 opens the menu on the mod's space, its first page; the menu watches its settings, and its state reaches it.
  g_seen = Seen{};
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  const SpaceEvent watched = wait_kind(game, table, demo.answer.consumer, GRACEUI_EVENT_WATCHED);
  const std::string state_frame =
      R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":3,"body":{"values":{"lantern.brightness":0.5}}})";
  const GraceUiStatus published = table->publish(demo.answer.consumer, state_frame.data(), state_frame.size());
  check(menu.open == 1 && std::strcmp(menu.page, "demo.overview") == 0 && watched.kind == GRACEUI_EVENT_WATCHED &&
            watched.frame.find("\"settings\"") != std::string::npos && published == GRACEUI_OK &&
            g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] == 0,
        "menu space: opens on the mod's", "F5: page %s, focus %s; the mod polled %s; its settings state %s",
        menu.page, menu.focused, watched.kind == GRACEUI_EVENT_WATCHED ? watched.frame.c_str() : "NO SUB",
        status_name(published));

  // 2b. The page's components (D184): the menu watches their topic while the page is open; the mod's state is drawn.
  const SpaceEvent status_watch = wait_kind(game, table, demo.answer.consumer, GRACEUI_EVENT_WATCHED);
  const bool empty_before = grace.menu_shows("Nothing to show yet.") != 0;
  const std::string status_frame =
      R"({"v":1,"t":"state","space":"demo","topic":"lantern.status","rev":1,"body":{"tiles":[{"label":"Flame","value":"Low"}],"sentence":"The lantern gutters in the wind."}})";
  const GraceUiStatus status_published = table->publish(demo.answer.consumer, status_frame.data(), status_frame.size());
  bool drawn = false;
  for (int i = 0; i < 100 && !drawn; ++i) {
    menu = menu_after(game, grace, menu.lists);
    drawn = grace.menu_shows("The lantern gutters in the wind.") != 0 && grace.menu_shows("Low") != 0;
  }
  check(status_watch.kind == GRACEUI_EVENT_WATCHED && status_watch.frame.find("\"lantern.status\"") != std::string::npos &&
            empty_before && status_published == GRACEUI_OK && drawn,
        "menu space: its components", "the mod polled %s; before its state the page said it had nothing %s; its state "
        "%s, drawn in game %s",
        status_watch.kind == GRACEUI_EVENT_WATCHED ? status_watch.frame.c_str() : "NO SUB", empty_before ? "yes" : "NO",
        status_name(status_published), drawn ? "yes" : "NO");

  // 3. Page Down to Behaviour, ↓ to Brightness, →: the mod's command, from the value its state gave (0.5, not the
  // default 0.8), reaches it as a view's does; the mod refuses it in words, which show under the row.
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, menu.lists);
  const std::string behaviour = menu.page;
  press(window, VK_DOWN, 0x50);
  menu = menu_after(game, grace, menu.lists);
  const std::string on_row = menu.focused;
  press(window, VK_RIGHT, 0x4D);
  const SpaceEvent command = wait_kind(game, table, demo.answer.consumer, GRACEUI_EVENT_COMMAND);
  Json cmd;
  if (command.kind == GRACEUI_EVENT_COMMAND) {
    cmd = Json::parse(command.frame);
  }
  const bool command_right = cmd.is_object() && cmd["name"] == "settings.set" &&
                             cmd["args"]["key"] == "lantern.brightness" && cmd["args"]["value"].is_number() &&
                             std::fabs(cmd["args"]["value"].get<double>() - 0.55) < 1e-6 && command.reply != 0;
  const std::uint32_t refusals_before = grace.menu().refusals;
  const std::string refusal =
      R"({"v":1,"t":"result","space":"demo","id":0,"error":"not_now","detail":"Not now: the keeper is trimming the wick."})";
  const GraceUiStatus answered = table->answer(demo.answer.consumer, command.reply, refusal.data(), refusal.size());
  for (int i = 0; i < 100 && grace.menu().refusals == refusals_before; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  menu = menu_after(game, grace, grace.menu().lists);
  const Frame shot = game.frame_and_read(kInputColour, PresentWith::present);
  // → again: from the mod's own 0.5, shown again after the refusal, not from the refused 0.55.
  press(window, VK_RIGHT, 0x4D);
  const SpaceEvent again = wait_kind(game, table, demo.answer.consumer, GRACEUI_EVENT_COMMAND);
  Json retry;
  if (again.kind == GRACEUI_EVENT_COMMAND) {
    retry = Json::parse(again.frame);
  }
  const bool from_its_own = retry.is_object() && retry["args"]["value"].is_number() &&
                            std::fabs(retry["args"]["value"].get<double>() - 0.55) < 1e-6;
  const std::string ok = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
  const GraceUiStatus accepted = table->answer(demo.answer.consumer, again.reply, ok.data(), ok.size());
  const std::string taken =
      R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":4,"body":{"values":{"lantern.brightness":0.55}}})";
  table->publish(demo.answer.consumer, taken.data(), taken.size());
  menu = menu_after(game, grace, grace.menu().lists);
  check(behaviour == "demo.behaviour" && on_row == "setting:lantern.brightness" && command_right &&
            answered == GRACEUI_OK && from_its_own && accepted == GRACEUI_OK &&
            menu.refusals == refusals_before + 1 && std::strcmp(menu.focused, "setting:lantern.brightness") == 0,
        "menu space: a command and its refusal",
        "Page Down: %s; ↓: %s; →: the mod polled %s; its refusal %s, shown under the row (%u refusal(s), focus %s); "
        "→ again: %s, accepted %s",
        behaviour.c_str(), on_row.c_str(), command.frame.c_str(), status_name(answered), menu.refusals - refusals_before,
        menu.focused, again.frame.c_str(), status_name(accepted));
  save_shot(shot, "menu-space-1920x1080.bmp");

  // 4. A second mod's space for the game's view, registered while the menu is open: a switcher heads the rail, and →
  // on it opens the other space; the menu stops watching the first's settings and watches the second's.
  std::string other_pages = std::string(space.pages);
  const std::string from = "\"space\":\"demo\"";
  const std::size_t at = other_pages.find(from);
  if (at != std::string::npos) {
    other_pages.replace(at, from.size(), "\"space\":\"lantern\"");
  }
  const std::size_t named = other_pages.find("\"Demo space\"");
  if (named != std::string::npos) {
    other_pages.replace(named, 12, "\"The second lantern\"");
  }
  const Connected lantern = connect(table, request_for("lantern"));
  const GraceUiSpaceDesc other = game_space(other_pages, space.settings);
  const GraceUiStatus second = table->register_pages(lantern.answer.consumer, &other);
  frames(game, 3);
  press(window, VK_ESCAPE, kScanEscape);
  for (int i = 0; i < 6; ++i) {
    press(window, VK_UP, 0x48);
  }
  menu = menu_after(game, grace, grace.menu().lists);
  const std::string switcher = menu.focused;
  press(window, VK_RIGHT, 0x4D);
  const SpaceEvent left_demo = wait_kind(game, table, demo.answer.consumer, GRACEUI_EVENT_UNWATCHED);
  const SpaceEvent joined = wait_kind(game, table, lantern.answer.consumer, GRACEUI_EVENT_WATCHED);
  menu = menu_after(game, grace, grace.menu().lists);
  check(second == GRACEUI_OK && switcher == "rail:switcher" && left_demo.kind == GRACEUI_EVENT_UNWATCHED &&
            joined.kind == GRACEUI_EVENT_WATCHED && std::strcmp(menu.focused, "rail:switcher") == 0,
        "menu space: a switcher between two",
        "the second space %s; Esc, ↑: %s; →: the demo mod polled %s, the second %s; focus %s", status_name(second),
        switcher.c_str(), left_demo.kind == GRACEUI_EVENT_UNWATCHED ? left_demo.frame.c_str() : "NO UNSUB",
        joined.kind == GRACEUI_EVENT_WATCHED ? joined.frame.c_str() : "NO SUB", menu.focused);

  // 5. The rail's foot opens GraceUI's own space; its head, the open mod's categories, leads back.
  std::string foot;
  for (int i = 0; i < 12 && foot.rfind("rail:graceui/", 0) != 0; ++i) {
    press(window, VK_DOWN, 0x50);
    menu = menu_after(game, grace, grace.menu().lists);
    foot = menu.focused;
  }
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent left_lantern = wait_kind(game, table, lantern.answer.consumer, GRACEUI_EVENT_UNWATCHED);
  menu = menu_after(game, grace, grace.menu().lists);
  const std::string own_page = menu.page;
  const std::string own_focus = menu.focused;
  for (int i = 0; i < 12; ++i) {
    press(window, VK_UP, 0x48);
  }
  menu = menu_after(game, grace, grace.menu().lists);
  const std::string head = menu.focused;
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent back = wait_kind(game, table, lantern.answer.consumer, GRACEUI_EVENT_WATCHED);
  menu = menu_after(game, grace, grace.menu().lists);
  check(foot.rfind("rail:graceui/", 0) == 0 && left_lantern.kind == GRACEUI_EVENT_UNWATCHED &&
            own_page.rfind("graceui.", 0) == 0 && own_focus == "rail:" + own_page &&
            head == "rail:lantern/demo.overview" && back.kind == GRACEUI_EVENT_WATCHED &&
            std::strcmp(menu.page, "demo.overview") == 0,
        "menu space: GraceUI's at the foot, the mod's at the head",
        "↓ to %s, Enter: page %s, focus %s, the second mod polled %s; ↑ to %s, Enter: %s, the second mod polled %s",
        foot.c_str(), own_page.c_str(), own_focus.c_str(),
        left_lantern.kind == GRACEUI_EVENT_UNWATCHED ? "its unsub" : "NO UNSUB", head.c_str(), menu.page,
        back.kind == GRACEUI_EVENT_WATCHED ? "its sub" : "NO SUB");

  // 6. F5 closes it: the mod hears the menu stop watching. Both mods go while it is closed, and F5 opens GraceUI's.
  press(window, VK_F5, kScanF5);
  const SpaceEvent closed = wait_kind(game, table, lantern.answer.consumer, GRACEUI_EVENT_UNWATCHED);
  table->disconnect(demo.answer.consumer);
  table->disconnect(lantern.answer.consumer);
  frames(game, 5);
  press(window, VK_F5, kScanF5);
  menu = menu_after(game, grace, grace.menu().lists);
  const GraceUiTestServer after = grace.server();
  check(closed.kind == GRACEUI_EVENT_UNWATCHED && after.spaces == 0 && menu.open == 1 &&
            menu.page == own_page && menu.off == 0 && grace.state().render_off == 0 &&
            g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] + g_seen.key_down[VK_ESCAPE] + g_seen.key_up[VK_ESCAPE] == 0,
        "menu space: the mods go",
        "F5 closed it: the mod polled %s; both disconnected (%u space(s) left); F5 opened %s; no fault; F5's and "
        "Esc's messages reaching the game: %d",
        closed.kind == GRACEUI_EVENT_UNWATCHED ? "its unsub" : "NO UNSUB", after.spaces, menu.page,
        g_seen.key_down[VK_F5] + g_seen.key_up[VK_F5] + g_seen.key_down[VK_ESCAPE] + g_seen.key_up[VK_ESCAPE]);
  grace.print_log();
}

// ---- The in-game spikes' tools (D175): grace-spike.dll, the observer EldenNet's launcher loads in a spike run, and
// grace-standin.dll, a stand-in for another overlay that patches Present's vtable slot with its own code. "spike" loads
// both before GraceUI (the stand-in's patch first, so GraceUI hooks the stand-in's Present), opens the typing test with
// F9 and types into it; "spike-after" loads both once GraceUI's hooks are in. Each reads back the report the observer
// wrote: its header, its state lines and the typing test's line.

std::string read_text(const std::wstring& path) {
  std::string text;
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return text;
  }
  char buffer[4096];
  DWORD got = 0;
  while (ReadFile(file, buffer, sizeof buffer, &got, nullptr) && got > 0) {
    text.append(buffer, got);
  }
  CloseHandle(file);
  return text;
}

std::vector<grace::protocol::Json> read_report(const std::wstring& path) {
  std::vector<grace::protocol::Json> lines;
  const std::string text = read_text(path);
  std::size_t start = 0;
  for (std::size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start)) {
    const grace::protocol::Json line = grace::protocol::Json::parse(text.substr(start, end - start), nullptr, false);
    if (!line.is_discarded()) {
      lines.push_back(line);
    }
    start = end + 1;
  }
  return lines;
}

std::vector<grace::protocol::Json> lines_of(const std::vector<grace::protocol::Json>& lines, const char* kind) {
  std::vector<grace::protocol::Json> out;
  for (const grace::protocol::Json& line : lines) {
    if (line.value("k", std::string()) == kind) {
      out.push_back(line);
    }
  }
  return out;
}

using StandInPatched = LONG (*)();

// Loads the stand-in and the observer from the harness's folder, and waits (presenting) until the stand-in has patched
// Present's slot, at most 5 s.
bool load_spike_tools(Game& game) {
  const HMODULE standin = LoadLibraryW((folder_of_this_program() + GRACE_STANDIN_FILE).c_str());
  const HMODULE spike = LoadLibraryW((folder_of_this_program() + L"grace-spike.dll").c_str());
  const FARPROC found = standin != nullptr ? GetProcAddress(standin, "GraceStandIn_Patched") : nullptr;
  if (spike == nullptr || found == nullptr) {
    return false;
  }
  // SAFETY: standin.c gives the export of that name this type.
  const auto patched = reinterpret_cast<StandInPatched>(reinterpret_cast<void (*)(void)>(found));
  for (int i = 0; i < 250 && patched() == 0; ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  return patched() == 1;
}

// A fault of the harness's own (D231): a continuable access violation with a mark, which the harness's handler, last in
// the process's list, resumes once the observer's, first, has recorded it.
constexpr ULONG_PTR kHarnessFault = 0x6772616365;

LONG CALLBACK resume_harness_fault(EXCEPTION_POINTERS* pointers) {
  const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
  return record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters == 3 &&
                 record.ExceptionInformation[2] == kHarnessFault
             ? EXCEPTION_CONTINUE_EXECUTION
             : EXCEPTION_CONTINUE_SEARCH;
}

// Never inlined, so that both of its faults come from one site (an unrolled loop would make two).
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void raise_harness_fault() {
  const ULONG_PTR arguments[3] = {0, 0, kHarnessFault};  // a read of the null page, and the harness's mark
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 3, arguments);
}

std::string lower_text(std::string text) {
  for (char& c : text) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return text;
}

// ---- D235's breach in the spike observer's report: a child process with GraceUI and the observer raises a probe's
// breach for real; the observer's recorder, first in the list, writes the fault with the guard's record, and GraceUI's
// own handler ends the process. The parent reads what the child left.
std::wstring heap_guard_spike_folder() { return folder_of_this_program() + L"spike-guard\\s4-1"; }

void run_heap_guard_spike(const std::wstring& dll) {
  using grace::protocol::Json;
  const std::wstring folder = heap_guard_spike_folder();
  const std::wstring path = folder + L"\\" + GRACE_SPIKE_FILE;
  SetEnvironmentVariableW(GRACE_SPIKE_DIR_VARIABLE, folder.c_str());
  SetEnvironmentVariableW(L"GRACE_SPIKE_STATE_MS", L"300");
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  if (!load_spike_tools(game)) {
    fatal("grace-spike.dll or the stand-in did not load");
  }
  for (int i = 0; i < 600 && lines_of(read_report(path), "state").size() < 2; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  grace.heap_guard_probe(2, 1);  // never returns
  std::printf("FAIL heap guard: a raised breach returned\n");
  std::fflush(stdout);
  ExitProcess(3);
}

void run_heap_guard_observer(const std::wstring& dll) {
  using grace::protocol::Json;
  const std::wstring parent = folder_of_this_program() + L"spike-guard";
  const std::wstring folder = heap_guard_spike_folder();
  CreateDirectoryW(parent.c_str(), nullptr);
  DeleteFileW((folder + L"\\" + GRACE_SPIKE_FILE).c_str());
  DeleteFileW((folder + L"\\" + GRACE_SPIKE_LOG_FILE).c_str());
  DeleteFileW((folder + L"\\" + GRACE_SPIKE_FAULT_FILE).c_str());
  std::fflush(stdout);
  const auto status = static_cast<DWORD>(run_child(dll, L"heap-guard-spike", this_program()));
  const std::vector<Json> report = read_report(folder + L"\\" + GRACE_SPIKE_FILE);
  const std::vector<Json> faults = lines_of(report, "fault");
  const std::vector<Json> states = lines_of(report, "state");
  const Json fault = faults.empty() ? Json::object() : faults.back();
  const Json guard = fault.contains("heap_guard") ? fault["heap_guard"] : Json::object();
  bool made_ours = false;
  if (guard.contains("made_at") && guard["made_at"].is_array() && !guard["made_at"].empty() &&
      guard["made_at"][0].is_string()) {
    made_ours = guard["made_at"][0].get<std::string>().rfind("graceui+", 0) == 0;
  }
  bool counted = false;
  if (!states.empty() && states.back().contains("heap") && states.back()["heap"].is_array() &&
      states.back()["heap"].size() == 6) {
    counted = states.back()["heap"][0].get<std::uint64_t>() > 0 && states.back().value("heap_breach", "") == "none";
  }
  const std::string text = lower_text(read_text(folder + L"\\" + GRACE_SPIKE_FAULT_FILE));
  const bool ended = status == GRACEUI_TEST_HEAP_GUARD_BREACH || status == 0xC0000602u;  // Wine's fail-fast
  check(ended && fault.value("code", std::uint64_t{0}) == GRACEUI_TEST_HEAP_GUARD_BREACH &&
            guard.value("breach", std::string()) == "past_end" && guard.value("size", std::uint64_t{0}) == 24 &&
            made_ours && counted && text.find("heap guard: a block written past its end, 24 bytes") != std::string::npos &&
            text.find("made at graceui.dll+0x") != std::string::npos,
        "heap guard: a breach in the spike observer's report",
        "the process ended with %#lx; its fault line: %s; the state lines %s the guard's counts; the text file %s",
        static_cast<unsigned long>(status), fault.dump().c_str(), counted ? "carry" : "DO NOT CARRY",
        text.find("made at graceui.dll+0x") != std::string::npos ? "names the maker in graceui.dll"
                                                                  : "DOES NOT NAME THE MAKER");
}

void run_spike(const std::wstring& dll, bool tools_first) {
  using grace::protocol::Json;
  const std::wstring parent = folder_of_this_program() + (tools_first ? L"spike-first" : L"spike-after");
  const std::wstring folder = parent + L"\\s4-1";
  const std::wstring path = folder + L"\\" + GRACE_SPIKE_FILE;
  const std::wstring log_path = folder + L"\\" + GRACE_SPIKE_LOG_FILE;
  const std::wstring fault_path = folder + L"\\" + GRACE_SPIKE_FAULT_FILE;
  CreateDirectoryW(parent.c_str(), nullptr);
  DeleteFileW(path.c_str());
  DeleteFileW(log_path.c_str());
  DeleteFileW(fault_path.c_str());
  SetEnvironmentVariableW(GRACE_SPIKE_DIR_VARIABLE, folder.c_str());
  SetEnvironmentVariableW(L"GRACE_SPIKE_TYPING_MS", L"3000");
  SetEnvironmentVariableW(L"GRACE_SPIKE_STATE_MS", L"300");
  Game game(false);
  const HWND window = game.window();
  cursor_into(window);
  bool loaded = tools_first ? load_spike_tools(game) : true;
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  if (!tools_first) {
    loaded = load_spike_tools(game);
  }
  std::vector<Json> report;
  // Three state lines, and on a slow machine (a software renderer at a frame or two a second) until one of them shows a
  // frame GraceUI drew: the check below still asks for it, this only waits for it, at most 600 frames.
  const auto drawn_yet = [&] {
    const std::vector<Json> states = lines_of(report, "state");
    return !states.empty() && states.back().value("frames_drawn", std::uint64_t{0}) > 0 &&
           states.back().value("frame_step", std::uint64_t{0}) == 11;
  };
  for (int i = 0; i < 600 && (lines_of(report, "typing_ready").empty() || lines_of(report, "state").size() < 3 ||
                              !drawn_yet());
       ++i) {
    frames(game, 1);
    Sleep(20);
    report = read_report(path);
  }

  // 1. The header: the run's word from its folder, the GraceUI found by name without loading it, and nothing else's.
  const std::vector<Json> headers = lines_of(report, "header");
  const Json header = headers.empty() ? Json::object() : headers.front();
  const Json version = Json::array({GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR, GRACE_BUILD_PATCH});
  check(loaded && headers.size() == 1 && header["report"] == "graceui_spike" && header["schema"] == 1 &&
            header["spike"] == "s4" && header["graceui"] == version && header["build"] == "test" &&
            header["found"] == "by_name" && header["connect"] == "ok" && header["eldennet_at_find"] == "absent" &&
            header["steam_overlay"] == "absent",
        "spike: the report's header",
        "the stand-in and the observer %s, %s GraceUI; the header: %s", loaded ? "loaded" : "NOT LOADED",
        tools_first ? "before" : "after", header.dump().c_str());

  // 2. Both overlays draw: the stand-in's Present counts frames, and GraceUI draws them, whichever came first.
  const std::vector<Json> states = lines_of(report, "state");
  const Json first = states.empty() ? Json::object() : states.front();
  const Json last = states.empty() ? Json::object() : states.back();
  const auto number = [](const Json& line, const char* key) { return line.value(key, std::uint64_t{0}); };
  check(states.size() >= 3 && last["standin"] == "patched" &&
            number(last, "standin_presents") > number(first, "standin_presents") &&
            number(last, "frames_drawn") > 0 && number(last, "install_runs") == 1 && number(last, "init_step") == 7 &&
            number(last, "frame_step") == 11 && number(last, "hooks") >= 6 &&
            last["host"] == "hooked" && last["prologue"] != "unknown" && number(last, "lists_refused") == 0,
        "spike: both overlays draw",
        "%zu state lines; the stand-in %s, its Present called %llu times between the first and the last; GraceUI "
        "%s with %llu hooks from %llu install run(s) (its init at step %llu), %llu frames drawn, Present's prologue %s",
        states.size(), last.value("standin", std::string("?")).c_str(),
        static_cast<unsigned long long>(number(last, "standin_presents") - number(first, "standin_presents")),
        last.value("host", std::string("?")).c_str(), static_cast<unsigned long long>(number(last, "hooks")),
        static_cast<unsigned long long>(number(last, "install_runs")),
        static_cast<unsigned long long>(number(last, "init_step")),
        static_cast<unsigned long long>(number(last, "frames_drawn")),
        last.value("prologue", std::string("?")).c_str());

  // 2b. How far GraceUI's first frames got, and GraceUI's own log (D230): the step only grows, to 11, with a state line
  // at its changes; the log is copied beside the report, numbered in order, and never into it.
  bool rising = true;
  std::uint64_t step = 0;
  std::size_t step_lines = 0;
  for (const Json& state : states) {
    const std::uint64_t now = number(state, "frame_step");
    rising = rising && now >= step;
    step_lines += now != step ? 1 : 0;
    step = now;
  }
  const std::string log = read_text(log_path);
  const std::string report_text = read_text(path);
  const bool queue_logged = log.find("present queue found") != std::string::npos;
  const bool header_line = log.rfind("# GraceUI's own log", 0) == 0;
  bool numbered = true;
  std::uint64_t expected = 0;
  for (std::size_t at = log.find(" n="); at != std::string::npos; at = log.find(" n=", at + 3)) {
    numbered = numbered && std::strtoull(log.c_str() + at + 3, nullptr, 10) == expected;
    ++expected;
  }
  // With the tools after GraceUI, the observer may first look once its first frames are done: one change, 0 to 11.
  check(rising && step == 11 && step_lines >= (tools_first ? 2u : 1u) && header_line && queue_logged && numbered && expected >= 3 &&
            report_text.find("present queue found") == std::string::npos,
        "spike: the first frames' step and GraceUI's log beside the report",
        "the step %s to %llu over %zu change(s); the log file %s, %llu line(s) %s, %s the queue's line; the report %s",
        rising ? "rose" : "DID NOT ONLY RISE", static_cast<unsigned long long>(step), step_lines,
        header_line ? "headed" : "NOT HEADED", static_cast<unsigned long long>(expected),
        numbered ? "numbered in order" : "NOT IN ORDER", queue_logged ? "with" : "WITHOUT",
        report_text.find("present queue found") == std::string::npos ? "holds none of it" : "HOLDS LOG TEXT");

  // 2c. The fault recorder (D231): a fault raised twice from one site is written once, recorded and never handled
  // (the harness's own handler resumes it), with the faulting thread's frames as fixed words in the report and as
  // modules' file names beside it.
  PVOID resume = AddVectoredExceptionHandler(0, &resume_harness_fault);
  for (int i = 0; i < 2; ++i) {
    raise_harness_fault();
  }
  if (resume != nullptr) {
    RemoveVectoredExceptionHandler(resume);
  }
  const std::vector<Json> faults = lines_of(read_report(path), "fault");
  const Json fault = faults.empty() ? Json::object() : faults.front();
  bool system_first = false;
  bool harness_frame = false;
  if (fault.contains("at") && fault["at"].is_array() && !fault["at"].empty() && fault["at"][0].is_string()) {
    const std::string first_at = fault["at"][0].get<std::string>();
    system_first = first_at.rfind("kernel+", 0) == 0 || first_at.rfind("ntdll+", 0) == 0;
    for (const Json& at : fault["at"]) {
      harness_frame = harness_frame || (at.is_string() && at.get<std::string>().rfind("game+", 0) == 0);
    }
  }
  const std::string fault_text = lower_text(read_text(fault_path));
  check(faults.size() == 1 && fault.value("code", std::uint64_t{0}) == EXCEPTION_ACCESS_VIOLATION &&
            fault.value("access", std::string()) == "read" && fault.value("null_target", std::uint64_t{9}) == 1 &&
            system_first && harness_frame && fault_text.rfind("# faults seen", 0) == 0 &&
            fault_text.find("grace-harness.exe+0x") != std::string::npos &&
            fault_text.find("kernelbase.dll+0x") != std::string::npos,
        "spike: the fault recorder",
        "%zu fault line(s) for two faults from one site; the first: %s; the text file %s the harness's and KernelBase's "
        "frames",
        faults.size(), fault.dump().c_str(),
        fault_text.find("grace-harness.exe+0x") != std::string::npos ? "names" : "DOES NOT NAME");

  // 3. The typing test (with the tools first): F9 opens it, letters and the mouse go to it and none reaches the game,
  // and it gives the controls back when its time is up.
  if (tools_first) {
    press(window, VK_F9, 0x43);
    GraceUiTestInput input = grace.input();
    for (int i = 0; i < 150 && input.holder_kind != GRACEUI_TEST_KIND_PANEL; ++i) {
      frames(game, 1);
      Sleep(20);
      input = grace.input();
    }
    const bool held = input.holder_kind == GRACEUI_TEST_KIND_PANEL;
    g_seen = Seen{};
    for (const char letter : {'w', 'a', 's', 'd', 'x'}) {
      const UINT vk = static_cast<UINT>(letter - 'a' + 'A');
      post_key(window, vk, 0, true, false);
      PostMessageW(window, WM_CHAR, static_cast<WPARAM>(letter), 1);
      post_key(window, vk, 0, false, false);
      frames(game, 2);
    }
    inject_mouse(30, 10, MOUSEEVENTF_MOVE);
    std::vector<Json> typed;
    for (int i = 0; i < 300 && typed.empty(); ++i) {
      frames(game, 1);
      Sleep(20);
      typed = lines_of(read_report(path), "typing");
    }
    const Json typing = typed.empty() ? Json::object() : typed.front();
    const Json passed = typing.value("passed", Json::array());
    const bool none_passed = passed.size() == 5 && passed[0] == 0 && passed[2] == 0 && passed[3] == 0 && passed[4] == 0;
    // From 0.2.1 (D176) its own panel receives the holder's events: every character GraceUI's Dear ImGui counted, the
    // keys, the pointer, and its end as GraceUI recorded it; and its own Dear ImGui takes every character feed gave it.
    const Json received = typing.value("received", Json::array());
    const bool panel_received = received.size() == 4 && received[0] == typing["chars"] && received[1] >= 10 &&
                                received[2] >= 1 && typing["end_event"] == "released" &&
                                number(typing, "dropped") == 0 && typing["imgui_chars"] == received[0];
    check(held && typed.size() == 1 && typing["end"] == "released" && number(typing, "held_ms") >= 3000 &&
              number(typing, "chars") >= 5 && none_passed && panel_received && g_seen.chars == 0 &&
              grace.input().holder_kind == 0,
          "spike: the typing test",
          "F9: the observer's panel %s input; its line: %s; characters reaching the game: %d; the holder after: %u",
          held ? "took" : "DID NOT TAKE", typing.dump().c_str(), g_seen.chars, grace.input().holder_kind);
  }
  grace.print_log();
}

// ---- A holder's events (D176): a mod that holds input with GRACEUI_INPUT_EVENTS gets what the player does through
// poll: keys with their modifiers and repeats, characters (a surrogate pair joined, WM_UNICHAR's, an IME's result), the
// pointer in its panel's pixels, a pad's buttons while it takes the pad, and the end of its holder with the reason, last
// and once. A mod that asked without the flag, a request of ABI 1.0's size, and a poll of ABI 1.0's event size get none
// of them; a mod that does not hold input gets none; set_caret places the IME's windows at the holder's caret alone.

constexpr std::size_t kHolderEventsKept = 256;  // a holder's events waiting at most (graceui.h)

// At ABI 1.1's event size unless a step names another: a holder's events, without 1.2's display event (D180).
std::vector<GraceUiEvent> poll_holder(const GraceUiApi1* table, GraceUiConsumer me, std::uint32_t size = 104) {
  std::vector<GraceUiEvent> got;
  for (int i = 0; i < 1000; ++i) {
    GraceUiEvent event{};
    event.size = size;
    if (table->poll(me, &event) != GRACEUI_OK || event.kind == GRACEUI_EVENT_NONE) {
      break;
    }
    got.push_back(event);
  }
  return got;
}

std::string events_text(const std::vector<GraceUiEvent>& events) {
  std::string text;
  for (const GraceUiEvent& e : events) {
    char one[96];
    switch (e.kind) {
      case GRACEUI_EVENT_KEY:
        std::snprintf(one, sizeof one, "key %02X%s%s mod %u", e.key, e.pressed != 0 ? " down" : " up",
                      e.repeat != 0 ? " repeat" : "", e.modifiers);
        break;
      case GRACEUI_EVENT_CHARACTER: std::snprintf(one, sizeof one, "char U+%04X", e.code_point); break;
      case GRACEUI_EVENT_POINTER:
        if (e.button_event != 0) {
          std::snprintf(one, sizeof one, "button %u %s at %.0f,%.0f", e.button, e.pressed != 0 ? "down" : "up", e.x,
                        e.y);
        } else if (e.wheel_x != 0.0f || e.wheel_y != 0.0f) {
          std::snprintf(one, sizeof one, "wheel %.1f,%.1f", e.wheel_x, e.wheel_y);
        } else {
          std::snprintf(one, sizeof one, "move %.0f,%.0f", e.x, e.y);
        }
        break;
      case GRACEUI_EVENT_PAD:
        std::snprintf(one, sizeof one, "pad %u %04X %s", e.pad, e.button, e.pressed != 0 ? "down" : "up");
        break;
      case GRACEUI_EVENT_INPUT_ENDED: std::snprintf(one, sizeof one, "ended %u", e.why); break;
      default: std::snprintf(one, sizeof one, "kind %u", e.kind); break;
    }
    text += text.empty() ? "" : "; ";
    text += one;
  }
  return text.empty() ? "none" : text;
}

bool only_ended(const std::vector<GraceUiEvent>& events, std::uint32_t why) {
  return events.size() == 1 && events[0].kind == GRACEUI_EVENT_INPUT_ENDED && events[0].why == why;
}

void run_holder_events(const std::wstring& dll) {
  Game game(false);
  const HWND window = game.window();
  cursor_into(window);
  pump_messages();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  // Three mods: one that asks for its events, one that asks without the flag, and one built against ABI 1.0's sizes.
  const Connected first = connect(table, request_for("harness-mod", GRACEUI_VERSION_PACK(0, 2, 1), "Harness mod",
                                                     "panels textures input world-state holder-events"));
  const Connected second =
      connect(table, request_for("plain-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Plain mod", "panels textures input"));
  const Connected third =
      connect(table, request_for("older-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Older mod", "panels textures input"));
  HarnessMod mod{table, first.answer.consumer, 0};
  HarnessMod plain{table, second.answer.consumer, 0};
  HarnessMod older{table, third.answer.consumer, 0};
  const std::uint32_t white_pixels[16] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  std::uint64_t field = 0, plain_field = 0, older_field = 0;
  const bool made = mod.texture(4, 4, white_pixels, mod.white) == GRACEUI_OK &&
                    plain.texture(4, 4, white_pixels, plain.white) == GRACEUI_OK &&
                    older.texture(4, 4, white_pixels, older.white) == GRACEUI_OK &&
                    mod.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 200, 100, 300, 120,
                              GRACEUI_PANEL_TAKES_INPUT, field) == GRACEUI_OK &&
                    plain.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 20, 20, 100, 60,
                                GRACEUI_PANEL_TAKES_INPUT, plain_field) == GRACEUI_OK &&
                    older.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 20, 200, 100, 60,
                                GRACEUI_PANEL_TAKES_INPUT, older_field) == GRACEUI_OK;
  mod.quad(field, 300, 120, Colour{40, 60, 90});
  plain.quad(plain_field, 100, 60, Colour{90, 60, 40});
  older.quad(older_field, 100, 60, Colour{60, 90, 40});
  for (const auto& [owner, id] : {std::pair{mod.me, field}, std::pair{plain.me, plain_field},
                                  std::pair{older.me, older_field}}) {
    table->show_panel(owner, id);
  }
  start_host(game, grace);
  frames(game, 30);
  until_atlas(game, grace);
  const bool cap = table->has_cap(GRACEUI_CAP_HOLDER_EVENTS, std::strlen(GRACEUI_CAP_HOLDER_EVENTS)) != 0;
  const bool covers = GRACEUI_API1_COVERS(table, set_caret) && table->set_caret != nullptr;
  check(made && first.status == GRACEUI_OK && second.status == GRACEUI_OK && third.status == GRACEUI_OK && cap &&
            covers && table->abi_minor == GRACEUI_ABI1_MINOR && GRACEUI_ABI1_MINOR >= 1u,
        "holder events: the capability",
        "three mods connected and their panels made (%s); has_cap(\"%s\") %s; the table %u bytes, minor %u, %s "
        "set_caret",
        made ? "yes" : "NO", GRACEUI_CAP_HOLDER_EVENTS, cap ? "yes" : "NO", table->size, table->abi_minor,
        covers ? "covering" : "NOT COVERING");

  // 1. Keys and characters. Nothing waits before the grant; then a key's press, its auto-repeat and its release, a
  // character, a surrogate pair joined, WM_UNICHAR's and an IME's result, in order, and none of them reaches the game.
  // A key that keeps no character (the left arrow), so that TranslateMessage adds none of its own.
  const std::vector<GraceUiEvent> before = poll_holder(table, mod.me);
  GraceUiStatus status = GRACEUI_FAULT;
  const GraceUiInputAnswer granted = mod.request(field, GRACEUI_HOLDER_MODAL, status,
                                                 GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE, GRACEUI_INPUT_EVENTS);
  frames(game, 2);
  g_seen = Seen{};
  post_key(window, VK_LEFT, 0x4B, true, false);
  post_key(window, VK_LEFT, 0x4B, true, true);
  post_key(window, VK_LEFT, 0x4B, false, false);
  PostMessageW(window, WM_CHAR, 'a', 1);
  PostMessageW(window, WM_CHAR, 0xD83D, 1);
  PostMessageW(window, WM_CHAR, 0xDE42, 1);
  PostMessageW(window, WM_UNICHAR, 0x1F600, 0);
  pump_messages();
  SendMessageW(window, WM_IME_CHAR, 0x3044, 1);
  frames(game, 2);
  const std::vector<GraceUiEvent> typed = poll_holder(table, mod.me);
  const auto key_is = [](const GraceUiEvent& e, std::uint32_t vk, bool pressed, bool repeat) {
    return e.kind == GRACEUI_EVENT_KEY && e.key == vk && (e.pressed != 0) == pressed && (e.repeat != 0) == repeat &&
           e.modifiers == 0 && e.dropped == 0;
  };
  const auto char_is = [](const GraceUiEvent& e, std::uint32_t code_point) {
    return e.kind == GRACEUI_EVENT_CHARACTER && e.code_point == code_point;
  };
  const bool typed_ok = typed.size() == 7 && key_is(typed[0], VK_LEFT, true, false) &&
                        key_is(typed[1], VK_LEFT, true, true) && key_is(typed[2], VK_LEFT, false, false) &&
                        char_is(typed[3], 'a') && char_is(typed[4], 0x1F642) && char_is(typed[5], 0x1F600) &&
                        char_is(typed[6], 0x3044);
  check(before.empty() && status == GRACEUI_OK && granted.answer == GRACEUI_INPUT_GRANTED && typed_ok &&
            g_seen.key_down[VK_LEFT] == 0 && g_seen.chars == 0,
        "holder events: keys and characters",
        "before the grant: %s; granted with the flag (%s); then: %s; keys and characters reaching the game: %d, %d",
        events_text(before).c_str(), answer_name(granted.answer), events_text(typed).c_str(), g_seen.key_down[VK_LEFT],
        g_seen.chars);

  // 2. Modifiers, as the keyboard holds them: Ctrl+F2 injected, with Ctrl's own press and release.
  inject_key(VK_CONTROL, 0x1D, true);
  settle();
  inject_key(VK_F2, 0x3C, true);
  settle();
  inject_key(VK_F2, 0x3C, false);
  settle();
  inject_key(VK_CONTROL, 0x1D, false);
  settle();
  frames(game, 2);
  const std::vector<GraceUiEvent> chord = poll_holder(table, mod.me);
  bool f2_with_ctrl = false;
  bool ctrl_up_plain = false;
  for (const GraceUiEvent& e : chord) {
    f2_with_ctrl = f2_with_ctrl || (e.kind == GRACEUI_EVENT_KEY && e.key == VK_F2 && e.pressed != 0 &&
                                    e.modifiers == GRACEUI_MOD_CTRL);
    ctrl_up_plain = ctrl_up_plain || (e.kind == GRACEUI_EVENT_KEY && e.key == VK_CONTROL && e.pressed == 0 &&
                                      e.modifiers == 0);
  }
  check(f2_with_ctrl && ctrl_up_plain && g_seen.key_down[VK_F2] == 0, "holder events: modifiers",
        "Ctrl+F2 injected: %s; F2 reaching the game: %d", events_text(chord).c_str(), g_seen.key_down[VK_F2]);

  // 3. The pointer, in the panel's own pixels (the panel at 200,100): three moves coalesced to the newest, a click
  // where it rests, the wheel both ways, and a move outside the panel, which comes as a position outside it.
  PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(210, 110));
  PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(230, 120));
  PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(250, 130));
  PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(250, 130));
  PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(250, 130));
  PostMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0);
  PostMessageW(window, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-240)), 0);
  PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
  pump_messages();
  frames(game, 2);
  const std::vector<GraceUiEvent> pointed = poll_holder(table, mod.me);
  const auto at = [](const GraceUiEvent& e, float x, float y) { return e.x == x && e.y == y; };
  const bool pointer_ok =
      pointed.size() == 6 && pointed[0].kind == GRACEUI_EVENT_POINTER && pointed[0].button_event == 0 &&
      at(pointed[0], 50, 30) && pointed[1].button_event == 1 && pointed[1].button == GRACEUI_BUTTON_LEFT &&
      pointed[1].pressed == 1 && at(pointed[1], 50, 30) && pointed[2].button_event == 1 && pointed[2].pressed == 0 &&
      pointed[3].wheel_y == 1.0f && pointed[3].wheel_x == 0.0f && pointed[4].wheel_x == -2.0f &&
      pointed[4].wheel_y == 0.0f && pointed[5].button_event == 0 && at(pointed[5], -100, -50);
  check(pointer_ok, "holder events: the pointer", "%s", events_text(pointed).c_str());

  // 4. Only the holder's mod, and only an event of ABI 1.1's size: the other two mods find nothing, and a poll of ABI
  // 1.0's size leaves the holder's events waiting for the next poll of 1.1's size.
  post_key(window, VK_HOME, 0x47, true, false);
  pump_messages();
  const std::vector<GraceUiEvent> to_plain = poll_holder(table, plain.me);
  const std::vector<GraceUiEvent> to_older = poll_holder(table, older.me, 48);
  const std::vector<GraceUiEvent> small_poll = poll_holder(table, mod.me, 48);
  const std::vector<GraceUiEvent> full_poll = poll_holder(table, mod.me);
  post_key(window, VK_HOME, 0x47, false, false);
  pump_messages();
  poll_holder(table, mod.me);
  check(to_plain.empty() && to_older.empty() && small_poll.empty() && full_poll.size() == 1 &&
            key_is(full_poll[0], VK_HOME, true, false),
        "holder events: only the holder's, at 1.1's size",
        "another mod polled: %s; a mod at 1.0's size: %s; the holder at 1.0's size: %s, then at 1.1's: %s",
        events_text(to_plain).c_str(), events_text(to_older).c_str(), events_text(small_poll).c_str(),
        events_text(full_poll).c_str());

  // 5. Each way out ends with INPUT_ENDED and its reason, after the events not yet taken are discarded: released,
  // the panel hidden, the player's exit (the menu key twice), danger and focus lost. A key waits before each.
  const auto hold = [&] {
    GraceUiStatus asked = GRACEUI_FAULT;
    const GraceUiInputAnswer answer = mod.request(field, GRACEUI_HOLDER_MODAL, asked,
                                                  GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE, GRACEUI_INPUT_EVENTS);
    frames(game, 2);
    post_key(window, VK_LEFT, 0x4B, true, false);
    post_key(window, VK_LEFT, 0x4B, false, false);
    pump_messages();
    return asked == GRACEUI_OK && answer.answer == GRACEUI_INPUT_GRANTED;
  };
  bool held_each = hold();
  table->release_input(mod.me);
  const std::vector<GraceUiEvent> released = poll_holder(table, mod.me);
  held_each = hold() && held_each;
  table->hide_panel(mod.me, field);
  const std::vector<GraceUiEvent> hidden = poll_holder(table, mod.me);
  table->show_panel(mod.me, field);
  frames(game, 2);
  held_each = hold() && held_each;
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  pump_messages();
  frames(game, 2);
  post_key(window, VK_F5, kScanF5, true, false);
  post_key(window, VK_F5, kScanF5, false, false);
  pump_messages();
  frames(game, 2);
  const std::vector<GraceUiEvent> exited = poll_holder(table, mod.me);
  held_each = hold() && held_each;
  mod.world(GRACEUI_WORLD_DANGER);
  frames(game, 2);
  const std::vector<GraceUiEvent> danger = poll_holder(table, mod.me);
  mod.world(0);
  frames(game, 2);
  held_each = hold() && held_each;
  const HWND other = CreateWindowExW(0, L"GraceHarness", L"other", WS_OVERLAPPEDWINDOW, 700, 400, 200, 120, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
  ShowWindow(other, SW_SHOW);
  SetForegroundWindow(other);
  SetFocus(other);
  pump_messages();
  frames(game, 3);
  const std::vector<GraceUiEvent> unfocused = poll_holder(table, mod.me);
  if (other != nullptr) {
    DestroyWindow(other);
  }
  cursor_into(window);
  pump_messages();
  frames(game, 3);
  const std::vector<GraceUiEvent> after_end = poll_holder(table, mod.me);
  check(held_each && only_ended(released, GRACEUI_CLOSED_RELEASED) && only_ended(hidden, GRACEUI_CLOSED_RELEASED) &&
            only_ended(exited, GRACEUI_CLOSED_PLAYER) && only_ended(danger, GRACEUI_CLOSED_DANGER) &&
            only_ended(unfocused, GRACEUI_CLOSED_FOCUS_LOST) && after_end.empty(),
        "holder events: each way out",
        "each grant %s; released: %s; hidden: %s; the player's exit: %s; danger: %s; focus lost: %s; after: %s",
        held_each ? "granted" : "NOT ALWAYS GRANTED", events_text(released).c_str(), events_text(hidden).c_str(),
        events_text(exited).c_str(), events_text(danger).c_str(), events_text(unfocused).c_str(),
        events_text(after_end).c_str());

  // 6. Without the flag, and with a request of ABI 1.0's size (whose flags GraceUI never reads, whatever follows it in
  // memory), a holder's mod gets none of them; a holder of the menu's is unchanged.
  GraceUiStatus plain_status = GRACEUI_FAULT;
  const GraceUiInputAnswer plain_granted = plain.request(plain_field, GRACEUI_HOLDER_MODAL, plain_status);
  frames(game, 2);
  post_key(window, VK_LEFT, 0x4B, true, false);
  PostMessageW(window, WM_CHAR, 'b', 1);
  post_key(window, VK_LEFT, 0x4B, false, false);
  pump_messages();
  const std::vector<GraceUiEvent> plain_got = poll_holder(table, plain.me);
  table->release_input(plain.me);
  const std::vector<GraceUiEvent> plain_end = poll_holder(table, plain.me);
  GraceUiInputRequest old_request{};
  old_request.size = 24;  // ABI 1.0's GraceUiInputRequest
  old_request.kind = GRACEUI_HOLDER_MODAL;
  old_request.wants = GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE;
  old_request.panel = older_field;
  old_request.flags = GRACEUI_INPUT_EVENTS;  // past its size: not read
  GraceUiInputAnswer old_answer{};
  old_answer.size = sizeof old_answer;
  const GraceUiStatus old_status = table->request_input(older.me, &old_request, &old_answer);
  frames(game, 2);
  PostMessageW(window, WM_CHAR, 'c', 1);
  pump_messages();
  const std::vector<GraceUiEvent> older_got = poll_holder(table, older.me);
  table->release_input(older.me);
  const std::vector<GraceUiEvent> older_end = poll_holder(table, older.me);
  check(plain_status == GRACEUI_OK && plain_granted.answer == GRACEUI_INPUT_GRANTED && plain_got.empty() &&
            plain_end.empty() && old_status == GRACEUI_OK && old_answer.answer == GRACEUI_INPUT_GRANTED &&
            older_got.empty() && older_end.empty(),
        "holder events: none without the flag",
        "a mod granted without the flag (%s) got %s, and at its end %s; a request of 1.0's size (%s) got %s, and at its "
        "end %s",
        answer_name(plain_granted.answer), events_text(plain_got).c_str(), events_text(plain_end).c_str(),
        answer_name(old_answer.answer), events_text(older_got).c_str(), events_text(older_end).c_str());

  // 7. A pad's buttons: none while the holder does not take the pad; one event a change while it does, by pad.
  bool pad_held = hold();
  poll_holder(table, mod.me);  // the key hold() leaves
  grace.pad(0, XINPUT_GAMEPAD_A);
  grace.pad(0, 0);
  const std::vector<GraceUiEvent> pad_unwanted = poll_holder(table, mod.me);
  table->release_input(mod.me);
  poll_holder(table, mod.me);
  GraceUiStatus pad_status = GRACEUI_FAULT;
  pad_held = mod.request(field, GRACEUI_HOLDER_MODAL, pad_status,
                         GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE | GRACEUI_WANT_PAD, GRACEUI_INPUT_EVENTS)
                     .answer == GRACEUI_INPUT_GRANTED &&
             pad_held;
  frames(game, 2);
  grace.pad(0, XINPUT_GAMEPAD_A);
  grace.pad(0, XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B);
  grace.pad(1, XINPUT_GAMEPAD_Y);
  grace.pad(0, 0);
  grace.pad(1, 0);
  const std::vector<GraceUiEvent> pads = poll_holder(table, mod.me);
  const auto pad_is = [](const GraceUiEvent& e, std::uint32_t pad, std::uint32_t button, bool pressed) {
    return e.kind == GRACEUI_EVENT_PAD && e.pad == pad && e.button == button && (e.pressed != 0) == pressed;
  };
  const bool pads_ok = pads.size() == 6 && pad_is(pads[0], 0, XINPUT_GAMEPAD_A, true) &&
                       pad_is(pads[1], 0, XINPUT_GAMEPAD_B, true) && pad_is(pads[2], 1, XINPUT_GAMEPAD_Y, true) &&
                       pad_is(pads[3], 0, XINPUT_GAMEPAD_A, false) && pad_is(pads[4], 0, XINPUT_GAMEPAD_B, false) &&
                       pad_is(pads[5], 1, XINPUT_GAMEPAD_Y, false);
  table->release_input(mod.me);
  poll_holder(table, mod.me);
  check(pad_held && pad_unwanted.empty() && pads_ok, "holder events: a pad's buttons",
        "a holder that does not take the pad: %s; one that does: %s", events_text(pad_unwanted).c_str(),
        events_text(pads).c_str());

  // 8. The caret: the holder's places the IME's composition window in the game window's pixels; another mod's, or one
  // on a panel not its own, or one not finite, counts for nothing.
  hold();
  poll_holder(table, mod.me);
  GraceUiCaret caret{};
  caret.size = sizeof caret;
  caret.panel = field;
  caret.x = 10;
  caret.y = 20;
  caret.height = 18;
  const GraceUiStatus caret_status = table->set_caret(mod.me, &caret);
  const auto composition_at = [&](POINT& where) {
    SendMessageW(window, WM_IME_STARTCOMPOSITION, 0, 0);
    COMPOSITIONFORM composition{};
    const HIMC context = ImmGetContext(window);
    const bool read = context != nullptr && ImmGetCompositionWindow(context, &composition) != FALSE;
    if (context != nullptr) {
      ImmReleaseContext(window, context);
    }
    SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
    where = composition.ptCurrentPos;
    return read;
  };
  POINT placed{};
  const bool read_placed = composition_at(placed);
  GraceUiCaret others = caret;
  others.panel = plain_field;
  others.x = 5;
  others.y = 5;
  const GraceUiStatus others_status = table->set_caret(plain.me, &others);  // not the holder: ignored
  POINT after_others{};
  const bool read_after = composition_at(after_others);
  const GraceUiStatus not_its_panel = table->set_caret(plain.me, &caret);
  GraceUiCaret bad = caret;
  bad.x = std::numeric_limits<float>::quiet_NaN();
  const GraceUiStatus not_finite = table->set_caret(mod.me, &bad);
  table->release_input(mod.me);
  poll_holder(table, mod.me);
  check(caret_status == GRACEUI_OK && read_placed && placed.x == 210 && placed.y == 120 &&
            others_status == GRACEUI_OK && read_after && after_others.x == 210 && after_others.y == 120 &&
            not_its_panel == GRACEUI_REFUSED && not_finite == GRACEUI_REFUSED,
        "holder events: the caret",
        "the holder's caret at 10,20 in its panel (%s): the composition window at %ld,%ld; another mod's (%s): still at "
        "%ld,%ld; on a panel not its own %s; not finite %s",
        status_name(caret_status), placed.x, placed.y, status_name(others_status), after_others.x, after_others.y,
        status_name(not_its_panel), status_name(not_finite));

  // 9. A mod that polls too slowly: 300 characters, 256 kept and the rest counted on the first event it takes.
  hold();
  poll_holder(table, mod.me);
  for (int i = 0; i < 300; ++i) {
    PostMessageW(window, WM_CHAR, 'a' + i % 26, 1);
  }
  pump_messages();
  const std::vector<GraceUiEvent> flood = poll_holder(table, mod.me);
  std::uint32_t dropped = 0;
  for (const GraceUiEvent& e : flood) {
    dropped += e.dropped;
  }
  table->release_input(mod.me);
  const std::vector<GraceUiEvent> flood_end = poll_holder(table, mod.me);
  check(flood.size() == kHolderEventsKept && !flood.empty() && flood[0].dropped == 300 - flood.size() &&
            dropped == flood[0].dropped && only_ended(flood_end, GRACEUI_CLOSED_RELEASED),
        "holder events: a slow mod",
        "300 characters waiting: %zu taken, %u dropped (counted on the first: %u); then %s", flood.size(), dropped,
        flood.empty() ? 0u : flood[0].dropped, events_text(flood_end).c_str());
  grace.print_log();
}

// ---- GraceUI's settings kept between sessions (D84, D179): one process changes them in the menu, which writes
// user\settings.json; the next starts from what it kept, its menu's faces baked at the kept scale, and a reset writes the
// defaults back (nothing kept); a third finds a file that is not GraceUI's and keeps the defaults.

// A scenario in a process of its own (both defined with the scenario dispatch below).
std::wstring this_program();
int run_child(const std::wstring& dll, const wchar_t* scenario, std::wstring program = this_program());

grace::protocol::Json kept_settings(const std::wstring& dll) {
  const std::vector<unsigned char> bytes = read_file(settings_file_of(dll));
  return grace::protocol::Json::parse(bytes.begin(), bytes.end(), nullptr, false);
}

bool no_settings_file(const std::wstring& dll) {
  return GetFileAttributesW(settings_file_of(dll).c_str()) == INVALID_FILE_ATTRIBUTES;
}

grace::protocol::Json wait_for_kept(Game& game, const std::wstring& dll, const grace::protocol::Json& want) {
  grace::protocol::Json kept;
  for (int i = 0; i < 150; ++i) {
    kept = kept_settings(dll);
    if (!kept.is_discarded() && kept.is_object() && kept.value("values", grace::protocol::Json()) == want) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  return kept;
}

void run_settings_write(const std::wstring& dll) {
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  const bool none_at_start = no_settings_file(dll);
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  const bool none_when_opened = no_settings_file(dll);  // nothing changed: nothing written
  press(window, VK_RIGHT, 0x4D);  // Menu size 1.25 -> 1.30
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_DOWN, 0x50);
  press(window, VK_RIGHT, 0x4D);  // the theme: Grace Clear
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  const Json want = Json::object({{"ui.scale", 1.3}, {"ui.menu.theme", "clear"}});
  const Json kept = wait_for_kept(game, dll, want);
  check(none_at_start && none_when_opened && !kept.is_discarded() && kept.value("graceui_settings", 0) == 1 &&
            kept.value("values", Json()) == want,
        "settings: kept for the next session",
        "no file at the start (%s) or when the menu opened (%s); after → on Menu size and Grace Clear: %s",
        none_at_start ? "yes" : "NO", none_when_opened ? "yes" : "NO", kept.is_discarded() ? "NO FILE" : kept.dump().c_str());
}

void run_settings_read(const std::wstring& dll) {
  using grace::protocol::Json;
  g_keep_settings = true;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  const std::string scale = grace.setting("ui.scale");
  const std::string theme = grace.setting("ui.menu.theme");
  const bool said = grace.logged("GraceUI's settings: 2 kept from an earlier session, 0 left out");
  check(scale == "1.3" && theme == "\"clear\"" && std::fabs(menu.baked_scale - 1.3f) < 0.001f &&
            std::fabs(menu.scale - 1.3f) < 0.001f && said,
        "settings: the next session starts from them",
        "ui.scale %s, the theme %s; the menu's faces baked at %.2f and laid out at %.2f; the log %s",
        scale.c_str(), theme.c_str(), menu.baked_scale, menu.scale, said ? "says so" : "DOES NOT SAY SO");
  // R on each resets it: nothing differs from its default, so nothing is kept.
  press(window, 'R', 0x13);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_DOWN, 0x50);
  press(window, 'R', 0x13);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  const Json kept = wait_for_kept(game, dll, Json::object());
  check(!kept.is_discarded() && kept.value("values", Json(nullptr)) == Json::object() &&
            grace.setting("ui.scale") == "1.25",
        "settings: a reset keeps nothing",
        "after R on both: %s, ui.scale %s", kept.is_discarded() ? "NO FILE" : kept.dump().c_str(),
        grace.setting("ui.scale").c_str());
}

void run_settings_bad(const std::wstring& dll) {
  g_keep_settings = true;
  const std::wstring file = settings_file_of(dll);
  CreateDirectoryW(file.substr(0, file.find_last_of(L'\\')).c_str(), nullptr);
  const HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  const char garbage[] = "{\"values\": {\"ui.scale\": 1.3";  // cut short
  DWORD written = 0;
  if (out != INVALID_HANDLE_VALUE) {
    WriteFile(out, garbage, sizeof garbage - 1, &written, nullptr);
    CloseHandle(out);
  }
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  frames(game, 5);
  const bool said = grace.logged("GraceUI's settings were not read: the file is not GraceUI's settings");
  check(written > 0 && grace.setting("ui.scale") == "1.25" && said, "settings: a file that is not GraceUI's",
        "a file cut short: ui.scale %s (the default), the log %s", grace.setting("ui.scale").c_str(),
        said ? "says it was not read" : "DOES NOT SAY SO");
  DeleteFileW(file.c_str());
}

void run_settings(const std::wstring& dll) {
  DeleteFileW(settings_file_of(dll).c_str());
  for (const wchar_t* child : {L"settings-write", L"settings-read", L"settings-bad"}) {
    std::fflush(stdout);
    const int status = run_child(dll, child);
    char name[64];
    std::snprintf(name, sizeof name, "%ls scenario", child);
    check(status == 0, name, "its process ended with %d", status);
  }
  DeleteFileW(settings_file_of(dll).c_str());
}

// ---- An anti-cheat found after the first request (§7.2 item 1, D81): a world-state provider's flag, or GraceUI's own
// module re-check while the server runs, closes the browser view's server for the rest of the session, whatever a space
// asks; the menu stays. Run from the build's GraceUI folder, as pages is, so that the server can start.

void run_anti_cheat_later(const std::wstring& dll, bool recheck) {
  using grace::protocol::Json;
  Game game(false);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const auto describe = [&](std::uint32_t views) {
    GraceUiSpaceDesc desc{};
    desc.size = sizeof desc;
    desc.views = views;
    desc.version = GRACEUI_VERSION_PACK(1, 0, 0);
    desc.pages = space.pages.data();
    desc.pages_length = space.pages.size();
    desc.settings = space.settings.data();
    desc.settings_length = space.settings.size();
    desc.increment = "0.2";
    desc.increment_length = 3;
    return desc;
  };
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space",
                                                    "notices pages world-state"));
  if (recheck) {
    grace.set_anti_cheat_recheck(200);  // before the server starts, which schedules its first re-check
  }
  const GraceUiSpaceDesc browser = describe(GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER);
  const GraceUiStatus registered = table->register_pages(demo.answer.consumer, &browser);
  const auto wait_server = [&](std::uint32_t running) {
    GraceUiTestServer server = grace.server();
    for (int i = 0; i < 150 && server.running != running; ++i) {
      frames(game, 1);
      Sleep(20);
      server = grace.server();
    }
    return server;
  };
  const GraceUiTestServer started = wait_server(1);
  const char* said = nullptr;
  GraceUiStatus flagged = GRACEUI_OK;
  if (recheck) {
    grace.set_anti_cheat_names(L"grace-harness", nullptr);  // this program's own module: the re-check finds it
    said = "GraceUI's module re-check found an anti-cheat: the browser view is closed for the rest of the session";
  } else {
    GraceUiWorldState state{};
    state.size = sizeof state;
    state.flags = GRACEUI_WORLD_ANTI_CHEAT;
    flagged = table->set_world_state(demo.answer.consumer, &state);
    said = "a world-state provider reports an anti-cheat: the browser view is closed for the rest of the session";
  }
  const GraceUiTestServer stopped = wait_server(0);
  // Closed for the session: the flag cleared, another browser space, and time pass, and it stays closed.
  GraceUiWorldState clear{};
  clear.size = sizeof clear;
  const GraceUiStatus cleared = table->set_world_state(demo.answer.consumer, &clear);
  const Connected later = connect(table, request_for("lantern", GRACEUI_VERSION_PACK(0, 1, 0), "Lantern", "pages"));
  GraceUiSpaceDesc again = browser;
  std::string pages_text(space.pages);
  const std::string from = "\"space\":\"demo\"";
  const std::size_t at = pages_text.find(from);
  if (at != std::string::npos) {
    pages_text.replace(at, from.size(), "\"space\":\"lantern\"");
  }
  again.pages = pages_text.data();
  again.pages_length = pages_text.size();
  const GraceUiStatus later_registered = table->register_pages(later.answer.consumer, &again);
  frames(game, 60);
  const GraceUiTestServer after = grace.server();
  press(game.window(), VK_F5, kScanF5);
  const GraceUiTestMenu menu = menu_after(game, grace, 0);
  check(registered == GRACEUI_OK && started.running == 1 && flagged == GRACEUI_OK && stopped.running == 0 &&
            grace.logged(said) && cleared == GRACEUI_OK && after.running == 0 && menu.open == 1 && menu.off == 0,
        recheck ? "anti-cheat later: the module re-check" : "anti-cheat later: a provider's flag",
        "the server ran (%u), then %s: running %u; the log %s; the flag cleared (%s), a second browser space (%s), "
        "60 frames: running %u; the menu still opens (%u)",
        started.running, recheck ? "a re-check of 200 ms found this program's module" : "the provider's flag",
        stopped.running, grace.logged(said) ? "says so" : "DOES NOT SAY SO", status_name(cleared),
        status_name(later_registered), after.running, menu.open);
  press(game.window(), VK_F5, kScanF5);
  frames(game, 2);
}

// ---- A space's pages in game, for a look review (not a check of the full run): GRACE_SPACE_DIR names a folder holding
// pages.json, settings.json and topics.json (a space as grace-dev takes it, with invented fixtures), GRACE_SHOTS the
// folder the frames go to. A mod registers the space for the game's view, answers each watch with its topic's fixture
// (a state topic's body; streams are the browser's in game, D184) and its settings with values.json's values if the
// folder has one, opens the menu on it and saves a frame of every page and tab, by Page Down and E, at 1920 by 1080 and
// the default Menu size. GRACE_SPACE_INCREMENT names the increment it registers (0.2 without it). GRACE_SHOTS_OWN=1
// walks on into GraceUI's own pages; GRACE_SHOTS_ONLY names the one page to save; GRACE_SHOTS_POINTER ("x,y", client
// pixels) leaves the pointer there instead of at the corner; GRACE_SHOTS_THEME (clear) starts GraceUI with that theme, as
// a kept setting (D179), and removes the file after; GRACE_SHOTS_END=1 saves each page's end too, focus moved down to
// it. Run by name only.

std::string env_text(const char* name) {
  char value[1024];
  const DWORD length = GetEnvironmentVariableA(name, value, sizeof value);
  return length == 0 || length >= sizeof value ? std::string() : std::string(value, length);
}

void run_page_shots(const std::wstring& dll) {
  using grace::protocol::Json;
  const std::string dir = env_text("GRACE_SPACE_DIR");
  const auto text_of = [&](const char* name) {
    const std::string path = dir + "\\" + name;
    const std::vector<unsigned char> bytes = read_file(std::wstring(path.begin(), path.end()));
    return std::string(bytes.begin(), bytes.end());
  };
  const std::string pages_text = text_of("pages.json");
  const std::string settings_text = text_of("settings.json");
  const Json pages = Json::parse(pages_text, nullptr, false);
  const Json topics = Json::parse(text_of("topics.json"), nullptr, false);
  // values.json, if any: the settings' values the mod publishes (a secret's as its status, "set" or ""); else defaults.
  const std::string values_text = text_of("values.json");
  const Json values = values_text.empty() ? Json::object() : Json::parse(values_text, nullptr, false);
  if (dir.empty() || pages.is_discarded() || !pages.is_object() || settings_text.empty() || !topics.is_object()) {
    fatal("page-shots: GRACE_SPACE_DIR does not name a space's folder");
  }
  const std::string id = pages.value("space", std::string());
  const std::string name = pages.value("name", std::string("A space"));
  std::string increment = env_text("GRACE_SPACE_INCREMENT");
  if (increment.empty()) {
    increment = "0.2";
  }
  const std::string theme = env_text("GRACE_SHOTS_THEME");
  const std::wstring kept_file = settings_file_of(dll);
  if (!theme.empty()) {
    const std::wstring folder = kept_file.substr(0, kept_file.find_last_of(L"\\/"));
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::string text = Json{{"graceui_settings", 1}, {"values", {{"ui.menu.theme", theme}}}}.dump();
    const HANDLE file = CreateFileW(kept_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE ||
        !WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
      fatal("page-shots: the theme could not be kept for GraceUI to read");
    }
    CloseHandle(file);
    g_keep_settings = true;
  }
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected mod = connect(table, request_for(id.c_str(), GRACEUI_VERSION_PACK(0, 2, 0), name.c_str(), "notices"));
  GraceUiSpaceDesc desc = game_space(pages_text, settings_text);
  desc.increment = increment.data();
  desc.increment_length = increment.size();
  const GraceUiStatus registered = table->register_pages(mod.answer.consumer, &desc);
  frames(game, 5);
  // The pointer out of the way, at the window's corner, or where GRACE_SHOTS_POINTER puts it.
  RECT client{};
  GetClientRect(window, &client);
  POINT corner{client.right - 2, client.bottom - 2};
  if (const std::string at = env_text("GRACE_SHOTS_POINTER"); !at.empty()) {
    corner.x = std::atoi(at.c_str());
    corner.y = at.find(',') != std::string::npos ? std::atoi(at.c_str() + at.find(',') + 1) : corner.y;
  }
  ClientToScreen(window, &corner);
  SetCursorPos(corner.x, corner.y);
  const bool own_too = env_text("GRACE_SHOTS_OWN") == "1";
  const std::string only = env_text("GRACE_SHOTS_ONLY");
  const Json own_pages = Json::parse(grace::pages::builtin_spaces()[0].pages, nullptr, false);
  std::uint64_t rev = 1;
  const auto serve_mod = [&] {
    for (int i = 0; i < 64; ++i) {
      const SpaceEvent event = poll_space(table, mod.answer.consumer, 65536);
      if (event.status != GRACEUI_OK || event.kind == GRACEUI_EVENT_NONE) {
        return;
      }
      if (event.kind != GRACEUI_EVENT_WATCHED) {
        continue;
      }
      const Json sub = Json::parse(event.frame, nullptr, false);
      const std::string topic = sub.is_object() ? sub.value("topic", std::string()) : std::string();
      Json body;
      if (topic == "settings") {
        body = Json{{"values", values.is_object() ? values : Json::object()}};
      } else if (topics.contains(topic) && topics[topic].is_object()) {
        body = topics[topic];
      } else {
        continue;
      }
      const std::string frame =
          Json{{"v", 1}, {"t", "state"}, {"space", id}, {"topic", topic}, {"rev", ++rev}, {"body", body}}.dump();
      table->publish(mod.answer.consumer, frame.data(), frame.size());
    }
  };
  const auto settle = [&] {
    for (int i = 0; i < 12; ++i) {
      serve_mod();
      game.frame(kInputColour, PresentWith::present);
      Sleep(25);
    }
    frames(game, 3);
  };
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  std::vector<std::string> seen;
  std::string shots;
  for (int step = 0; step < 64; ++step) {
    settle();
    menu = grace.menu();
    const std::string page = menu.page;
    if (page.empty() || std::find(seen.begin(), seen.end(), page) != seen.end() ||
        (!own_too && page.rfind("graceui.", 0) == 0)) {
      if (!own_too || page.rfind("graceui.", 0) == 0 || page.empty()) {
        break;
      }
      // The mod's pages are done: GraceUI's own, by the rail's foot link (Esc to the rail, down to its end, Enter).
      press(window, VK_ESCAPE, kScanEscape);
      for (int i = 0; i < 40; ++i) {
        press(window, VK_DOWN, 0x50);
      }
      press(window, VK_RETURN, 0x1C);
      settle();
      continue;
    }
    seen.push_back(page);
    std::size_t tabs = 0;
    for (const Json* data : {&pages, &own_pages}) {
      if (!data->is_object() || !data->contains("pages")) {
        continue;
      }
      for (const Json& p : (*data)["pages"]) {
        if (p.value("id", std::string()) == page && p.contains("tabs") && p["tabs"].is_array()) {
          tabs = p["tabs"].size();
        }
      }
    }
    for (std::size_t t = 0; t < std::max<std::size_t>(tabs, 1); ++t) {
      if (t > 0) {
        press(window, 'E', 0x12);
        settle();
      }
      if (!only.empty() && page != only) {
        continue;
      }
      const std::string file = page + (tabs > 1 ? "-tab" + std::to_string(t + 1) : std::string()) + ".bmp";
      save_shot(game.frame_and_read(kInputColour, PresentWith::present), file.c_str());
      shots += (shots.empty() ? "" : ", ") + file;
      if (env_text("GRACE_SHOTS_END") == "1") {
        for (int i = 0; i < 40; ++i) {
          press(window, VK_DOWN, 0x50);
        }
        settle();
        const std::string end = file.substr(0, file.size() - 4) + "-end.bmp";
        save_shot(game.frame_and_read(kInputColour, PresentWith::present), end.c_str());
        shots += ", " + end;
      }
    }
    press(window, VK_NEXT, 0x51);
    frames(game, 3);
  }
  check(registered == GRACEUI_OK && !seen.empty() && menu.off == 0, "page shots",
        "space %s registered %s at increment %s; frames: %s", id.c_str(), status_name(registered), increment.c_str(),
        shots.empty() ? "NONE" : shots.c_str());
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  if (!theme.empty()) {
    DeleteFileW(kept_file.c_str());  // the next scenario starts from GraceUI's defaults again
  }
}

// ---- The menu's mouse (§5.5): a press focuses and acts on what it is on, a held button's moves set a slider, the rail
// takes a click; at 1920 by 1080, where the client area and the back buffer are the same size.

struct MenuBounds {
  bool found = false;
  float row[4] = {};
  float control[4] = {};
};

MenuBounds menu_bounds(const GraceUi& grace, const char* name) {
  MenuBounds out;
  float values[8] = {};
  out.found = grace.get_menu_bounds(name, values) != 0;
  std::copy(values, values + 4, out.row);
  std::copy(values + 4, values + 8, out.control);
  return out;
}

void post_mouse(HWND window, UINT message, float x, float y, WPARAM wparam = 0) {
  PostMessageW(window, message, wparam, MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
}

// ---- The in-game text field (G38 (a), D57): GraceUI's own field for a text setting, a secret, a form's fields, a
// table's row secret and search, typed through the window procedure's characters; the clipboard both ways; the IME at
// the caret; and the close routine, which keeps a form's text for the next open.

void post_text(HWND window, std::wstring_view text) {
  for (const wchar_t unit : text) {
    PostMessageW(window, WM_CHAR, static_cast<WPARAM>(unit), 1);
  }
}

// Frames until the menu's last layout draws `text` (or no longer does, with `gone`), at most 3 s.
bool until_shows(Game& game, const GraceUi& grace, const char* text, bool gone = false) {
  for (int i = 0; i < 150; ++i) {
    if ((grace.menu_shows(text) != 0) != gone) {
      return true;
    }
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  return false;
}

bool until_focused(Game& game, const GraceUi& grace, const char* name) {
  for (int i = 0; i < 150; ++i) {
    if (std::strcmp(grace.menu().focused, name) == 0) {
      return true;
    }
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  return false;
}

// ↓ until the menu's focus is on `name`, at most `steps` presses.
bool down_to(Game& game, const GraceUi& grace, HWND window, const char* name, int steps = 16) {
  for (int i = 0; i < steps && std::strcmp(grace.menu().focused, name) != 0; ++i) {
    const std::uint64_t lists = grace.menu().lists;
    press(window, VK_DOWN, 0x50);
    menu_after(game, grace, lists);
  }
  return std::strcmp(grace.menu().focused, name) == 0;
}

std::wstring clipboard_text(HWND window) {
  std::wstring out;
  for (int i = 0; i < 20 && !OpenClipboard(window); ++i) {
    Sleep(10);
  }
  if (HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr) {
    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)); text != nullptr) {
      out = text;
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return out;
}

void set_clipboard(HWND window, std::wstring_view text) {
  for (int i = 0; i < 20 && !OpenClipboard(window); ++i) {
    Sleep(10);
  }
  EmptyClipboard();
  if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t)); memory != nullptr) {
    auto* units = static_cast<wchar_t*>(GlobalLock(memory));
    std::copy(text.begin(), text.end(), units);
    units[text.size()] = L'\0';
    GlobalUnlock(memory);
    SetClipboardData(CF_UNICODETEXT, memory);
  }
  CloseClipboard();
}

// A key pressed with Ctrl held, as the player's keyboard sends them, so that the window's key state has Ctrl down.
void ctrl_press(UINT vk, UINT scan) {
  inject_key(VK_CONTROL, 0x1D, true);
  settle();
  inject_key(vk, scan, true);
  settle();
  inject_key(vk, scan, false);
  settle();
  inject_key(VK_CONTROL, 0x1D, false);
  settle();
}

// The mod's next command, answered ok; its frame as JSON (null if none came).
grace::protocol::Json take_command(Game& game, const GraceUiApi1* table, GraceUiConsumer mod) {
  const SpaceEvent command = wait_kind(game, table, mod, GRACEUI_EVENT_COMMAND);
  if (command.kind != GRACEUI_EVENT_COMMAND) {
    return grace::protocol::Json();
  }
  const std::string ok = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
  table->answer(mod, command.reply, ok.data(), ok.size());
  return grace::protocol::Json::parse(command.frame, nullptr, false);
}

void run_typing(const std::wstring& dll) {
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const GraceUiSpaceDesc desc = game_space(space.pages, space.settings);
  const GraceUiConsumer mod = demo.answer.consumer;
  check(table->register_pages(mod, &desc) == GRACEUI_OK, "typing: register", "the demo space for the game's view");
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  wait_kind(game, table, mod, GRACEUI_EVENT_WATCHED);
  const std::string values =
      R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":2,"body":{"values":{"lantern.keeper":"The north keeper","lantern.oil_token":""}}})";
  table->publish(mod, values.data(), values.size());

  // 1. A text setting: Behaviour's Keeper tab, the keeper's name. Enter edits it; characters type at the caret; Q, a
  // menu key, types a letter while it edits; Backspace takes it again; Enter sends the text.
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, menu.lists);
  press(window, 'E', 0x12);
  menu = menu_after(game, grace, menu.lists);
  press(window, 'E', 0x12);
  menu = menu_after(game, grace, menu.lists);
  const bool on_keeper = down_to(game, grace, window, "setting:lantern.keeper");
  press(window, VK_RETURN, 0x1C);
  const bool editing = until_shows(game, grace, "Enter  Save    Esc  Cancel");
  post_text(window, L" Orla");
  until_shows(game, grace, "The north keeper Orla");
  inject_key('Q', 0x10, true);  // a real press: its character comes before its release, as a keyboard's does
  settle();
  inject_key('Q', 0x10, false);
  settle();
  const bool typed_q = until_shows(game, grace, "The north keeper Orlaq");
  press(window, VK_BACK, 0x0E);
  const bool took_q = until_shows(game, grace, "Orlaq", true) && grace.menu_shows("The north keeper Orla") != 0;
  const std::string tab_after = grace.menu().focused;
  press(window, VK_RETURN, 0x1C);
  const Json sent = take_command(game, table, mod);
  check(on_keeper && editing && typed_q && took_q && tab_after == "setting:lantern.keeper" && sent.is_object() &&
            sent["name"] == "settings.set" && sent["args"]["key"] == "lantern.keeper" &&
            sent["args"]["value"] == "The north keeper Orla",
        "typing: a text setting", "on the row %s; Enter edited it %s; typed, Q typed %s, Backspace took it %s; focus %s; "
        "Enter sent %s",
        on_keeper ? "yes" : "NO", editing ? "yes" : "NO", typed_q ? "yes" : "NO", took_q ? "yes" : "NO",
        tab_after.c_str(), sent.is_object() ? sent.dump().c_str() : "NOTHING");
  const std::string taken =
      R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":3,"body":{"values":{"lantern.keeper":"The north keeper Orla","lantern.oil_token":""}}})";
  table->publish(mod, taken.data(), taken.size());

  // 2. The IME's composition window at the field's caret, inside its field, while it edits; Escape drops the edit.
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Save    Esc  Cancel");
  frames(game, 3);
  const MenuBounds field = menu_bounds(grace, "setting:lantern.keeper");
  SendMessageW(window, WM_IME_STARTCOMPOSITION, 0, 0);
  COMPOSITIONFORM composition{};
  const HIMC context = ImmGetContext(window);
  const bool read_form = context != nullptr && ImmGetCompositionWindow(context, &composition) != FALSE;
  if (context != nullptr) {
    ImmReleaseContext(window, context);
  }
  SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
  // The field's bounds are the back buffer's pixels (1920 by 1080), the composition window's the client area's.
  RECT client{};
  GetClientRect(window, &client);
  const float sx = static_cast<float>(client.right) / 1920.0f;
  const float sy = static_cast<float>(client.bottom) / 1080.0f;
  const float px = static_cast<float>(composition.ptCurrentPos.x);
  const float py = static_cast<float>(composition.ptCurrentPos.y);
  const bool in_field = read_form && field.found && px >= std::floor(field.control[0] * sx) &&
                        px <= std::ceil((field.control[0] + field.control[2]) * sx) &&
                        py >= std::floor(field.control[1] * sy) &&
                        py <= std::ceil((field.control[1] + field.control[3]) * sy);
  post_text(window, L"!");
  until_shows(game, grace, "Orla!");
  press(window, VK_ESCAPE, kScanEscape);
  const bool dropped = until_shows(game, grace, "Enter  Edit    R  Reset    Esc  Back") &&
                       grace.menu_shows("Orla!") == 0;
  check(in_field && dropped, "typing: the IME at the caret",
        "the composition window at %ld,%ld in a client area of %ldx%ld; the field at %.0f,%.0f %.0fx%.0f in the back "
        "buffer: %s; Escape dropped the edit %s",
        composition.ptCurrentPos.x, composition.ptCurrentPos.y, client.right, client.bottom, field.control[0],
        field.control[1], field.control[2], field.control[3], in_field ? "inside" : "OUTSIDE", dropped ? "yes" : "NO");

  // 3. The clipboard: Ctrl+A and Ctrl+C put the field's text on it; Ctrl+V pastes the clipboard's, one paragraph.
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Save    Esc  Cancel");
  ctrl_press('A', kScanA);
  ctrl_press('C', 0x2E);
  frames(game, 5);
  const std::wstring copied = clipboard_text(window);
  set_clipboard(window, L"Lantern\r\nwarden");
  ctrl_press('A', kScanA);
  ctrl_press('V', 0x2F);
  const bool pasted = until_shows(game, grace, "Lantern warden");
  press(window, VK_RETURN, 0x1C);
  const Json paste_sent = take_command(game, table, mod);
  check(copied == L"The north keeper Orla" && pasted && paste_sent.is_object() &&
            paste_sent["args"]["value"] == "Lantern warden",
        "typing: the clipboard", "Ctrl+C put %zu characters on it (%s); Ctrl+V pasted %s; sent %s", copied.size(),
        copied == L"The north keeper Orla" ? "the field's" : "NOT THE FIELD'S", pasted ? "one paragraph" : "NOTHING",
        paste_sent.is_object() ? paste_sent.dump().c_str() : "NOTHING");

  // 4. A secret: masked, no glyph of it laid out; sent once; shown as Set when the mod says so.
  const bool on_token = down_to(game, grace, window, "setting:lantern.oil_token");
  const bool missing = grace.menu_shows("Missing") != 0;  // the control, and the help line's default
  press(window, VK_RETURN, 0x1C);
  const bool masked_open = until_shows(game, grace, "Enter  Set    Esc  Cancel");
  post_text(window, L"tok-9431");
  frames(game, 5);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool no_glyphs = grace.menu_shows("tok-9431") == 0 && grace.menu_shows("9431") == 0;
  press(window, VK_RETURN, 0x1C);
  const Json token = take_command(game, table, mod);
  const std::string set_state =
      R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":4,"body":{"values":{"lantern.keeper":"Lantern warden","lantern.oil_token":"set"}}})";
  table->publish(mod, set_state.data(), set_state.size());
  // The control reads Set; with the row no longer focused its help line (whose default reads Missing) closes.
  press(window, VK_UP, 0x48);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool shown_set = until_shows(game, grace, "Missing", true) && grace.menu_shows("Set") != 0;
  check(on_token && missing && masked_open && no_glyphs && token.is_object() &&
            token["args"]["key"] == "lantern.oil_token" && token["args"]["value"] == "tok-9431" && shown_set,
        "typing: a secret", "on the row %s (Missing %s); masked %s; its glyphs laid out: %s; sent to the mod %s; "
        "Missing no longer shown %s",
        on_token ? "yes" : "NO", missing ? "shown" : "NOT SHOWN", masked_open ? "yes" : "NO", no_glyphs ? "none" : "SOME",
        token.is_object() && token["args"]["value"] == "tok-9431" ? "once" : "NOT", shown_set ? "yes" : "NO");

  // 5. A form: Keepers' Hire a keeper, typed field by field (Enter moves on), sent together, empty once taken.
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, grace.menu().lists);
  const std::string roster =
      R"({"v":1,"t":"state","space":"demo","topic":"lantern.keepers","rev":1,"body":{"columns":[{"id":"name","label":"Keeper"},{"id":"ward","label":"Ward word"}],"rows":[{"id":"k1","name":"Corin Ash","ward":""},{"id":"k2","name":"Tamsin Reed","ward":"set"}]}})";
  wait_kind(game, table, mod, GRACEUI_EVENT_WATCHED);
  for (int i = 0; i < 5 && grace.menu_shows("Tamsin Reed") == 0; ++i) {  // once the menu watches the roster
    table->publish(mod, roster.data(), roster.size());
    until_shows(game, grace, "Tamsin Reed");
  }
  const bool on_name = down_to(game, grace, window, "field:hire/0/name", 24);
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Next    Esc  Done    Ctrl+V  Paste");
  post_text(window, L"Wren Hale");
  until_shows(game, grace, "Wren Hale");
  press(window, VK_RETURN, 0x1C);
  const bool moved_on = until_focused(game, grace, "field:hire/0/post");
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Next    Esc  Done    Ctrl+V  Paste");
  post_text(window, L"Bell tower");
  until_shows(game, grace, "Bell tower");
  press(window, VK_RETURN, 0x1C);
  const bool on_hire = until_focused(game, grace, "submit:hire/0");
  press(window, VK_RETURN, 0x1C);
  const Json hire = take_command(game, table, mod);
  const bool emptied = until_shows(game, grace, "Wren Hale", true) && grace.menu_shows("Bell tower") == 0;
  check(on_name && moved_on && on_hire && hire.is_object() && hire["name"] == "keepers.hire" &&
            hire["args"] == Json::object({{"name", "Wren Hale"}, {"post", "Bell tower"}}) && emptied,
        "typing: a form", "on Name %s; Enter moved to Post %s, then to Hire %s; sent %s; its fields empty once taken %s",
        on_name ? "yes" : "NO", moved_on ? "yes" : "NO", on_hire ? "yes" : "NO",
        hire.is_object() ? hire.dump().c_str() : "NOTHING", emptied ? "yes" : "NO");

  // 6. A row secret: Corin's ward word, masked, sent with the row's key.
  for (int i = 0; i < 16 && std::strcmp(grace.menu().focused, "secret:lantern.keepers/k1") != 0; ++i) {
    const std::uint64_t lists = grace.menu().lists;
    press(window, VK_UP, 0x48);
    menu_after(game, grace, lists);
  }
  const bool on_ward = std::strcmp(grace.menu().focused, "secret:lantern.keepers/k1") == 0;
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Set    Esc  Cancel");
  post_text(window, L"ember");
  frames(game, 5);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool ward_hidden = grace.menu_shows("ember") == 0;
  press(window, VK_RETURN, 0x1C);
  const Json ward = take_command(game, table, mod);
  check(on_ward && ward_hidden && ward.is_object() && ward["name"] == "keepers.ward" &&
            ward["args"] == Json::object({{"id", "k1"}, {"value", "ember"}}),
        "typing: a row secret", "on Corin's Set %s; its glyphs laid out: %s; sent %s", on_ward ? "yes" : "NO",
        ward_hidden ? "none" : "SOME", ward.is_object() && ward["args"]["id"] == "k1" ? "with the row's key" : "NOT");

  // 7. Search: `/` opens it from the page, its results as it is typed; Enter opens the setting's page on its row.
  post_text(window, L"/");
  const bool search_open = until_shows(game, grace, "↑ ↓  Choose    Enter  Open    Esc  Close");
  post_text(window, L"token");
  const bool found = until_shows(game, grace, "Oil merchant's token");
  press(window, VK_RETURN, 0x1C);
  const bool opened = until_focused(game, grace, "setting:lantern.oil_token") &&
                      std::strcmp(grace.menu().page, "demo.behaviour") == 0;
  const std::string opened_on = std::string(grace.menu().page) + ", " + grace.menu().focused;
  ctrl_press('F', 0x21);
  const bool ctrl_f = until_shows(game, grace, "↑ ↓  Choose    Enter  Open    Esc  Close");
  press(window, VK_ESCAPE, kScanEscape);
  const bool closed = until_shows(game, grace, "↑ ↓  Choose    Enter  Open    Esc  Close", true);
  check(search_open && found && opened && ctrl_f && closed, "typing: search",
        "`/` opened it %s; \"token\" found %s; Enter opened %s; Ctrl+F opened it %s; Escape closed it %s",
        search_open ? "yes" : "NO", found ? "the merchant's token" : "NOTHING", opened_on.c_str(),
        ctrl_f ? "yes" : "NO", closed ? "yes" : "NO");

  // 7b. Search across spaces (D219): from the demo space, GraceUI's own Menu size, named "GraceUI › Menu"; Enter
  // switches to GraceUI's space, on the Menu page, its row focused. Then back to the demo space for what follows.
  post_text(window, L"/");
  until_shows(game, grace, "↑ ↓  Choose    Enter  Open    Esc  Close");
  post_text(window, L"menu size");
  const bool across = until_shows(game, grace, "GraceUI \u203A Menu");
  press(window, VK_RETURN, 0x1C);
  const bool switched = until_focused(game, grace, "setting:ui.scale") &&
                        std::strcmp(grace.menu().page, "graceui.menu") == 0;
  const std::string switched_to = std::string(grace.menu().page) + ", " + grace.menu().focused;
  post_text(window, L"/");
  until_shows(game, grace, "↑ ↓  Choose    Enter  Open    Esc  Close");
  post_text(window, L"token");
  const bool from_own = until_shows(game, grace, "Demo space \u203A Behaviour");
  press(window, VK_RETURN, 0x1C);
  const bool back = until_focused(game, grace, "setting:lantern.oil_token") &&
                    std::strcmp(grace.menu().page, "demo.behaviour") == 0;
  check(across && switched && from_own && back, "typing: search across spaces",
        "GraceUI's Menu size found from the demo space %s; Enter opened %s; the demo's token found from GraceUI's %s, "
        "and opened %s", across ? "yes" : "NO", switched_to.c_str(), from_own ? "yes" : "NO", back ? "yes" : "NO");

  // 8. The close routine (G38 (2)): F5 while a form field edits; at the next open the field is not edited, and its
  // text is still there.
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool on_name_again = down_to(game, grace, window, "field:hire/0/name", 24);
  press(window, VK_RETURN, 0x1C);
  until_shows(game, grace, "Enter  Next    Esc  Done    Ctrl+V  Paste");
  post_text(window, L"Ash Moor");
  until_shows(game, grace, "Ash Moor");
  press(window, VK_F5, kScanF5);
  for (int i = 0; i < 100 && grace.menu().open != 0; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  const bool shut = grace.menu().open == 0;
  for (int i = 0; i < 110; ++i) {  // past the 2 s in which a second press of the menu key is the player's exit
    frames(game, 1);
    Sleep(20);
  }
  press(window, VK_F5, kScanF5);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool kept = until_shows(game, grace, "Ash Moor") && grace.menu_shows("Enter  Type    Esc  Back") != 0 &&
                    std::strcmp(grace.menu().focused, "field:hire/0/name") == 0;
  check(on_name_again && shut && kept, "typing: the close routine keeps a form's text",
        "F5 while Name was edited closed the menu %s; reopened on %s, not editing, its text kept %s",
        shut ? "yes" : "NO", grace.menu().focused, kept ? "yes" : "NO");
  press(window, VK_F5, kScanF5);
  frames(game, 5);
}

// ---- Master and detail in game (D215): a mod's master list, its first row chosen, the chosen row's member watched
// as a topic of its own and drawn, ↓ moving the watch to the next member, → into the detail and Esc back, and the
// close letting every member go.

// Polls until the mod hears `kind` for `topic` (its sub or unsub frame names it), at most 3 s; other events are passed
// over (a command among them answered ok).
bool heard(Game& game, const GraceUiApi1* table, GraceUiConsumer mod, std::uint32_t kind, const std::string& topic) {
  for (int i = 0; i < 150; ++i) {
    const SpaceEvent event = poll_space(table, mod);
    if (event.status != GRACEUI_OK) {
      return false;
    }
    if (event.kind == kind && event.frame.find("\"topic\":\"" + topic + "\"") != std::string::npos) {
      return true;
    }
    if (event.kind == GRACEUI_EVENT_COMMAND) {
      const std::string ok = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
      table->answer(mod, event.reply, ok.data(), ok.size());
    }
    if (event.kind == GRACEUI_EVENT_NONE) {
      game.frame(kInputColour, PresentWith::present);
      Sleep(20);
    }
  }
  return false;
}

void publish_state(const GraceUiApi1* table, GraceUiConsumer mod, const std::string& topic, std::uint64_t rev,
                   const std::string& body) {
  const std::string frame = R"({"v":1,"t":"state","space":"demo","topic":")" + topic + R"(","rev":)" +
                            std::to_string(rev) + R"(,"body":)" + body + "}";
  table->publish(mod, frame.data(), frame.size());
}

void run_master(const std::wstring& dll) {
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const GraceUiSpaceDesc desc = game_space(space.pages, space.settings);
  const GraceUiConsumer mod = demo.answer.consumer;
  check(table->register_pages(mod, &desc) == GRACEUI_OK, "master: register", "the demo space for the game's view");
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  for (int i = 0; i < 5 && std::strcmp(grace.menu().page, "demo.lamps") != 0; ++i) {
    press(window, VK_NEXT, 0x51);
    menu = menu_after(game, grace, grace.menu().lists);
  }
  // 1. The list's topic watched; once the mod publishes it, its first row is chosen and its member watched.
  const bool on_lamps = std::strcmp(grace.menu().page, "demo.lamps") == 0;
  const bool list_watched = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.lamps");
  const bool none_yet = until_shows(game, grace, "Choose a lamp to see how it burns.");
  publish_state(table, mod, "lantern.lamps", 1,
                R"({"columns":[{"id":"name","label":"Lamp"},{"id":"place","label":"Where"}],"rows":[{"id":"harbour-mouth","name":"Harbour mouth","place":"The north end of the wall"},{"id":"fish-steps","name":"Fish steps","place":"Halfway along"},{"id":"old-crane","name":"Old crane","place":"The inner end"}]})");
  const bool listed = until_shows(game, grace, "Harbour mouth");
  const bool first_watched = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.lamp/harbour-mouth");
  const bool focused_first = until_focused(game, grace, "item:lantern.lamps/harbour-mouth");
  publish_state(table, mod, "lantern.lamp/harbour-mouth", 1,
                R"({"tiles":[{"label":"Flame","value":"Tall"}],"facts":{"items":[{"label":"Glass","value":"Green"}]}})");
  const bool drawn = until_shows(game, grace, "Tall") && grace.menu_shows("Green") != 0;
  check(on_lamps && list_watched && none_yet && listed && first_watched && focused_first && drawn,
        "master: the first row chosen, its member watched and drawn",
        "on Lamps %s; the list watched %s; no row yet, its words %s; listed %s; harbour-mouth watched %s, focused %s; "
        "its tiles drawn %s",
        on_lamps ? "yes" : "NO", list_watched ? "yes" : "NO", none_yet ? "shown" : "NOT SHOWN", listed ? "yes" : "NO",
        first_watched ? "yes" : "NO", focused_first ? "yes" : "NO", drawn ? "yes" : "NO");
  const Frame shot = game.frame_and_read(kInputColour, PresentWith::present);
  save_shot(shot, "master-1920x1080.bmp");

  // 2. ↓: the next row chosen; the mod hears the first member let go and the next one watched; its state drawn.
  press(window, VK_DOWN, 0x50);
  const bool let_go = heard(game, table, mod, GRACEUI_EVENT_UNWATCHED, "lantern.lamp/harbour-mouth");
  const bool next_watched = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.lamp/fish-steps");
  publish_state(table, mod, "lantern.lamp/fish-steps", 1, R"({"tiles":[{"label":"Flame","value":"Low"}]})");
  const bool next_drawn = until_shows(game, grace, "Low") && grace.menu_shows("Tall") == 0;
  // A state for the member let go is taken and goes nowhere.
  publish_state(table, mod, "lantern.lamp/harbour-mouth", 2, R"({"tiles":[{"label":"Flame","value":"Stale"}]})");
  frames(game, 5);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool not_stale = grace.menu_shows("Stale") == 0;
  check(let_go && next_watched && next_drawn && not_stale && std::strcmp(grace.menu().focused, "item:lantern.lamps/fish-steps") == 0,
        "master: ↓ moves the watch to the next member",
        "harbour-mouth let go %s; fish-steps watched %s, drawn %s; the old member's late state shown: %s; focus %s",
        let_go ? "yes" : "NO", next_watched ? "yes" : "NO", next_drawn ? "yes" : "NO", not_stale ? "no" : "YES",
        grace.menu().focused);

  // 3. → into the detail, Esc back to the chosen row; then F5: the menu lets the list and the member go.
  press(window, VK_RIGHT, 0x4D);
  const bool into = until_focused(game, grace, "tab:lamp");
  press(window, VK_ESCAPE, kScanEscape);
  const bool back = until_focused(game, grace, "item:lantern.lamps/fish-steps");
  press(window, VK_F5, kScanF5);
  const bool member_gone = heard(game, table, mod, GRACEUI_EVENT_UNWATCHED, "lantern.lamp/fish-steps");
  check(into && back && member_gone, "master: into the detail and back; the close lets the member go",
        "→ into the detail %s; Esc back to the row %s; F5: the member unwatched %s", into ? "yes" : "NO",
        back ? "yes" : "NO", member_gone ? "yes" : "NO");
  frames(game, 5);
}

// ---- A slow command in game (D216): the control reads Working… while it waits, and the mod's answer after 11 s,
// past the ordinary 10 s, is still taken.
void run_slow(const std::wstring& dll) {
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const GraceUiSpaceDesc desc = game_space(space.pages, space.settings);
  const GraceUiConsumer mod = demo.answer.consumer;
  check(table->register_pages(mod, &desc) == GRACEUI_OK, "slow: register", "the demo space for the game's view");
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, grace.menu().lists);
  press(window, VK_NEXT, 0x51);
  menu = menu_after(game, grace, grace.menu().lists);
  const bool on_order = down_to(game, grace, window, "section-action:oil/order", 30);
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent command = wait_kind(game, table, mod, GRACEUI_EVENT_COMMAND);
  const bool working = until_shows(game, grace, "Working…");
  // 11 s pass, past the ordinary deadline; the menu still waits, and the mod's answer is taken.
  const std::uint32_t refusals = grace.menu().refusals;
  for (int i = 0; i < 550; ++i) {
    game.frame(kInputColour, PresentWith::present);
    Sleep(20);
  }
  const bool still = grace.menu_shows("Working…") != 0;
  const std::string ok = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})";
  const GraceUiStatus answered = command.kind == GRACEUI_EVENT_COMMAND
                                     ? table->answer(mod, command.reply, ok.data(), ok.size())
                                     : GRACEUI_FAULT;
  const bool done = until_shows(game, grace, "Order oil") && grace.menu_shows("Working…") == 0;
  check(on_order && command.kind == GRACEUI_EVENT_COMMAND && command.frame.find("\"oil.order\"") != std::string::npos &&
            working && still && answered == GRACEUI_OK && done && grace.menu().refusals == refusals,
        "slow: answered after 11 s",
        "on Order oil %s; the mod polled %s; Working… %s, still after 11 s %s; its answer then %s; the button back %s; "
        "refusals %u",
        on_order ? "yes" : "NO", command.kind == GRACEUI_EVENT_COMMAND ? command.frame.c_str() : "NOTHING",
        working ? "shown" : "NOT SHOWN", still ? "yes" : "NO", status_name(answered), done ? "yes" : "NO",
        grace.menu().refusals - refusals);
  press(window, VK_F5, kScanF5);
  frames(game, 5);
}

// ---- The editor block in game (D217): a document on a family's member drawn on the Lamps page's Your notes tab, a line
// typed and kept through the menu's close, Save sent with the document's revision and its conflict shown, and a slow
// draft's proposal beside its field.
void run_editor(const std::wstring& dll) {
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices"));
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  const GraceUiSpaceDesc desc = game_space(space.pages, space.settings);
  const GraceUiConsumer mod = demo.answer.consumer;
  check(table->register_pages(mod, &desc) == GRACEUI_OK, "editor: register", "the demo space for the game's view");
  const std::string lamps =
      R"({"columns":[{"id":"name","label":"Lamp"},{"id":"place","label":"Where"}],"rows":[{"id":"harbour-mouth","name":"Harbour mouth","place":"The north end of the wall"},{"id":"fish-steps","name":"Fish steps","place":"Halfway along"}]})";
  const std::string note =
      R"({"target":"harbour-mouth","rev":1,"state":{"word":"Saved","tone":"moss"},"draft_help":"Sent to the demo's own drafter.","fields":[)"
      R"({"id":"brief","label":"Short name","kind":"line","max_chars":24,"value":"Mouth lamp"},)"
      R"({"id":"public.summary","label":"How it looks","kind":"prose","max_words":35,"nullable":true,"value":"A tall lamp behind green glass."},)"
      R"({"id":"sayings","label":"What keepers say of it","kind":"list","max_items":4,"value":["Never let it gutter",{"text":"The old ward line","key":"s3","locked":"Written by hand."}]},)"
      R"({"id":"oil","label":"Oil it burns","kind":"pick","choices_from":"lantern.oils","value":"rapeseed"},)"
      R"({"id":"met","label":"Met","kind":"shown","value":"On the first evening"}]})";
  const std::string oils = R"({"choices":[{"value":"rapeseed","label":"Rapeseed oil"},{"value":"olive","label":"Olive oil"}]})";
  // Opens the Lamps page's Your notes tab, publishing the list, and the note once it is watched.
  const auto open_notes = [&] {
    press(window, VK_F5, kScanF5);
    menu_after(game, grace, 0);
    for (int i = 0; i < 5 && std::strcmp(grace.menu().page, "demo.lamps") != 0; ++i) {
      press(window, VK_NEXT, 0x51);
      menu_after(game, grace, grace.menu().lists);
    }
    const bool list = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.lamps");
    publish_state(table, mod, "lantern.lamps", 1, lamps);
    const bool row = until_focused(game, grace, "item:lantern.lamps/harbour-mouth");
    press(window, VK_RIGHT, 0x4D);
    const bool detail = until_focused(game, grace, "tab:lamp");
    for (int i = 0; i < 2; ++i) {
      press(window, 'E', 0x12);
      menu_after(game, grace, grace.menu().lists);
    }
    const bool tab = until_focused(game, grace, "tab:yours");
    const bool watched = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.note/harbour-mouth");
    publish_state(table, mod, "lantern.note/harbour-mouth", 1, note);
    const bool drawn = until_shows(game, grace, "What keepers say of it");
    const bool choices = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.oils");
    publish_state(table, mod, "lantern.oils", 1, oils);
    return list && row && detail && tab && watched && drawn && choices && until_shows(game, grace, "Rapeseed oil");
  };
  // 1. The document drawn: its state, its fields, a locked item's words, a pick's label from its topic.
  const bool opened = open_notes();
  const bool parts = grace.menu_shows("Mouth lamp") != 0 && grace.menu_shows("Locked: Written by hand.") != 0 &&
                     grace.menu_shows("Restore the previous version") != 0 && grace.menu_shows("6 / 35 words") != 0;
  check(opened && parts, "editor: the document drawn on its tab",
        "the list, the row, the tab, the note watched and drawn, the oils watched: %s; its fields, a locked item, its "
        "buttons, a count: %s", opened ? "yes" : "NO", parts ? "yes" : "NO");
  save_shot(game.frame_and_read(kInputColour, PresentWith::present), "editor-1920x1080.bmp");

  // 2. A line typed, then the menu closed and opened: the edit is still there, unsaved (G38 (2)).
  const bool on_brief = down_to(game, grace, window, "editor:your_notes/0/f/brief", 20);
  press(window, VK_RETURN, 0x1C);
  menu_after(game, grace, grace.menu().lists);
  post_text(window, L"s!");
  const bool typed = until_shows(game, grace, "Mouth lamps!");
  press(window, VK_RETURN, 0x1C);
  const bool unsaved = until_shows(game, grace, "Unsaved changes");
  press(window, VK_F5, kScanF5);
  const bool let_go = heard(game, table, mod, GRACEUI_EVENT_UNWATCHED, "lantern.note/harbour-mouth");
  frames(game, 5);
  // Opened again: on the same page and tab; the space publishes its list and its note again as they are watched.
  press(window, VK_F5, kScanF5);
  menu_after(game, grace, 0);
  const bool list_again = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.lamps");
  publish_state(table, mod, "lantern.lamps", 2, lamps);
  const bool note_again = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.note/harbour-mouth");
  publish_state(table, mod, "lantern.note/harbour-mouth", 2, note);
  const bool reopened = let_go && list_again && note_again;
  const bool kept = until_shows(game, grace, "Mouth lamps!") && grace.menu_shows("Unsaved changes") != 0;
  check(on_brief && typed && unsaved && reopened && kept, "editor: a typed edit kept through the menu's close",
        "on the brief %s; typed %s; Unsaved %s; the close let the note go, the open watched it again: %s; the edit "
        "still there %s", on_brief ? "yes" : "NO", typed ? "yes" : "NO", unsaved ? "yes" : "NO",
        reopened ? "yes" : "NO", kept ? "yes" : "NO");

  // 3. Save: the document with its revision; the mod answers conflict, and the view says so and offers Reload.
  for (int i = 0; i < 12 && std::strcmp(grace.menu().focused, "editor:your_notes/0/save") != 0; ++i) {
    press(window, VK_UP, 0x48);
    menu_after(game, grace, grace.menu().lists);
  }
  const bool on_save = std::strcmp(grace.menu().focused, "editor:your_notes/0/save") == 0;
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent save = wait_kind(game, table, mod, GRACEUI_EVENT_COMMAND);
  const grace::protocol::Json sent = grace::protocol::Json::parse(save.frame, nullptr, false);
  const bool args = sent.is_object() && sent.value("name", "") == "note.save" && sent.contains("args") &&
                    sent["args"].value("rev", 0) == 1 && sent["args"]["doc"].value("brief", "") == "Mouth lamps!" &&
                    !sent["args"]["doc"].contains("met");
  const std::string conflict = R"({"v":1,"t":"result","space":"demo","id":0,"error":"conflict"})";
  const GraceUiStatus answered = save.kind == GRACEUI_EVENT_COMMAND
                                     ? table->answer(mod, save.reply, conflict.data(), conflict.size())
                                     : GRACEUI_FAULT;
  const bool band = until_shows(game, grace, "Changed elsewhere since you opened it.") &&
                    grace.menu_shows("Reload") != 0;
  check(on_save && args && answered == GRACEUI_OK && band, "editor: Save sent; a conflict shown with Reload",
        "on Save %s; sent %s; answered %s; the conflict's band and Reload %s", on_save ? "yes" : "NO",
        save.kind == GRACEUI_EVENT_COMMAND ? save.frame.c_str() : "NOTHING", status_name(answered),
        band ? "shown" : "NOT SHOWN");
  save_shot(game.frame_and_read(kInputColour, PresentWith::present), "editor-conflict-1920x1080.bmp");

  // 4. A draft, slow: Working… while it runs; its proposal beside the field with Use this; Use this takes it.
  const bool on_notes = down_to(game, grace, window, "editor:your_notes/0/notes", 12);
  press(window, VK_RETURN, 0x1C);
  menu_after(game, grace, grace.menu().lists);
  post_text(window, L"sea");
  frames(game, 5);
  press(window, VK_ESCAPE, kScanEscape);
  menu_after(game, grace, grace.menu().lists);
  const bool on_draft = down_to(game, grace, window, "editor:your_notes/0/draft", 3);
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent asked = wait_kind(game, table, mod, GRACEUI_EVENT_COMMAND);
  const bool working = until_shows(game, grace, "Working…");
  const bool notes_sent = asked.frame.find("\"note.draft\"") != std::string::npos &&
                          asked.frame.find("\"notes\":\"sea\"") != std::string::npos;
  const std::string proposal =
      R"({"v":1,"t":"result","space":"demo","id":0,"ok":true,"body":{"draft":{"brief":"Sea lamp"},"notice":""}})";
  const GraceUiStatus drafted = asked.kind == GRACEUI_EVENT_COMMAND
                                    ? table->answer(mod, asked.reply, proposal.data(), proposal.size())
                                    : GRACEUI_FAULT;
  const bool beside = until_shows(game, grace, "Sea lamp") && grace.menu_shows("Use this") != 0 &&
                      grace.menu_shows("Working…") == 0;
  save_shot(game.frame_and_read(kInputColour, PresentWith::present), "editor-draft-1920x1080.bmp");
  const bool on_use = down_to(game, grace, window, "editor:your_notes/0/use/brief", 12);
  press(window, VK_RETURN, 0x1C);
  const bool used = until_shows(game, grace, "Use this", true) && grace.menu_shows("Mouth lamps!") == 0;
  check(on_notes && on_draft && working && notes_sent && drafted == GRACEUI_OK && beside && on_use && used,
        "editor: a slow draft's proposal used",
        "notes %s, Draft %s; sent %s; Working… %s; answered %s; its proposal beside the field %s; Use this %s, taken %s",
        on_notes ? "yes" : "NO", on_draft ? "yes" : "NO", asked.kind == GRACEUI_EVENT_COMMAND ? asked.frame.c_str() : "NOTHING",
        working ? "shown" : "NOT SHOWN", status_name(drafted), beside ? "yes" : "NO", on_use ? "yes" : "NO",
        used ? "yes" : "NO");

  // 5. A save whose answer names another row (D218): the list chooses it, its note is watched, and Save keeps focus
  // once that note is drawn.
  for (int i = 0; i < 16 && std::strcmp(grace.menu().focused, "editor:your_notes/0/save") != 0; ++i) {
    press(window, VK_UP, 0x48);
    menu_after(game, grace, grace.menu().lists);
  }
  const bool on_save_again = std::strcmp(grace.menu().focused, "editor:your_notes/0/save") == 0;
  press(window, VK_RETURN, 0x1C);
  const SpaceEvent saved = wait_kind(game, table, mod, GRACEUI_EVENT_COMMAND);
  const std::string chosen = R"({"v":1,"t":"result","space":"demo","id":0,"ok":true,"body":{"chosen":"fish-steps"}})";
  const GraceUiStatus took = saved.kind == GRACEUI_EVENT_COMMAND
                                 ? table->answer(mod, saved.reply, chosen.data(), chosen.size())
                                 : GRACEUI_FAULT;
  const bool next_note = heard(game, table, mod, GRACEUI_EVENT_WATCHED, "lantern.note/fish-steps");
  std::string fish = note;
  fish.replace(fish.find("harbour-mouth"), std::strlen("harbour-mouth"), "fish-steps");
  fish.replace(fish.find("Mouth lamp"), std::strlen("Mouth lamp"), "Steps lamp");
  publish_state(table, mod, "lantern.note/fish-steps", 1, fish);
  const bool drawn_next = until_shows(game, grace, "Steps lamp");
  const bool focus_kept = until_focused(game, grace, "editor:your_notes/0/save");
  check(on_save_again && took == GRACEUI_OK && next_note && drawn_next && focus_kept,
        "editor: a save's answer chooses the next row",
        "on Save %s; answered with fish-steps %s; its note watched %s, drawn %s; focus back on Save %s; focus %s",
        on_save_again ? "yes" : "NO", status_name(took), next_note ? "yes" : "NO", drawn_next ? "yes" : "NO",
        focus_kept ? "yes" : "NO", grace.menu().focused);
  press(window, VK_F5, kScanF5);
  frames(game, 5);
}

// The menu's pointer in a game that recentres its cursor every frame and reads the mouse as raw input, as the owner met
// it in the real game (D232): the pointer moves by the mouse's own motion, keeps where it was moved, and is not pulled
// back to the centre by the game's cursor.
// The menu across an Alt+Tab (G40), and Tab in it (G41): Tab moves no focus; the focus's loss lets go of input but
// keeps the menu, which takes input again on the focus's return on the same page and row; danger while it is away
// closes it, and the return finds it closed.
void run_menu_away(const std::wstring& dll) {
  Game game(false, 1280, 720);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  press(window, VK_RIGHT, 0x4D);  // into the page
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_DOWN, 0x50);
  menu = menu_after(game, grace, menu.lists);
  const std::string page = menu.page;
  const std::string row = menu.focused;

  // 1. Tab, and Shift+Tab, move no focus (G41).
  press(window, VK_TAB, 0x0F);
  frames(game, 4);
  menu = grace.menu();
  const bool tab_still = menu.open == 1 && page == menu.page && row == menu.focused;
  check(tab_still && !row.empty(), "menu away: Tab moves no focus",
        "on %s at %s; after Tab: %s at %s, %s", page.c_str(), row.c_str(), menu.page, menu.focused,
        menu.open == 1 ? "open" : "CLOSED");

  // 2. Away: another window takes the activation and the focus, as an Alt+Tab does.
  const auto take_focus = [&]() {
    const HWND other = CreateWindowExW(0, L"GraceHarness", L"other", WS_OVERLAPPEDWINDOW, 700, 400, 200, 120, nullptr,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(other, SW_SHOW);
    SetForegroundWindow(other);
    SetFocus(other);
    pump_messages();
    frames(game, 3);
    return other;
  };
  const auto give_back = [&](HWND other) {
    if (other != nullptr) {
      DestroyWindow(other);
    }
    cursor_into(window);
    pump_messages();
    frames(game, 3);
  };
  HWND other = take_focus();
  const GraceUiTestMenu away = grace.menu();
  const GraceUiTestInput away_input = grace.input();
  // 3. Back: the menu takes input again, on the same page and row.
  give_back(other);
  menu = menu_after(game, grace, away.lists);
  const GraceUiTestInput back_input = grace.input();
  const bool back = menu.open == 1 && page == menu.page && row == menu.focused &&
                    back_input.holder_kind == GRACEUI_TEST_KIND_MENU;
  check(away.open == 0 && away_input.holder_kind == 0 && away_input.last_close_reason == GRACEUI_TEST_CLOSED_FOCUS_LOST &&
            back,
        "menu away: it lets go at an Alt+Tab and takes input again on the return",
        "away: menu %s, holder %u (%s); back: menu %s on %s at %s, holder %u", away.open == 1 ? "HOLDING" : "let go",
        away_input.holder_kind, reason_name(away_input.last_close_reason), menu.open == 1 ? "open" : "CLOSED",
        menu.page, menu.focused, back_input.holder_kind);

  // 4. Away again, and danger meanwhile: the menu closes, and the return finds it closed (invariant 9).
  other = take_focus();
  grace.set_world(GRACEUI_TEST_WORLD_DANGER);
  frames(game, 3);
  grace.set_world(0);
  frames(game, 2);
  give_back(other);
  frames(game, 3);
  const GraceUiTestMenu after_danger = grace.menu();
  const GraceUiTestInput after_input = grace.input();
  check(after_danger.open == 0 && after_input.holder_kind == 0, "menu away: danger while away closes it",
        "after danger while away and the focus's return: menu %s, holder %u",
        after_danger.open == 1 ? "OPEN AGAIN" : "closed", after_input.holder_kind);
}

void run_menu_recentred(const std::wstring& dll) {
  Game game(false, 1280, 720);
  const HWND window = game.window();
  cursor_into(window);
  game.recentre();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  frames(game, 3);
  const GraceUiTestInput start = grace.input();
  const auto move_by = [&](LONG dx, LONG dy, int times) {
    for (int i = 0; i < times; ++i) {
      inject_mouse(dx, dy, MOUSEEVENTF_MOVE);
      settle();
      frames(game, 1);
    }
  };
  // 1. Right and down by 240 and 120 counts, in steps, the game recentring its cursor before each frame. Steps of six
  // counts at most: Wine, like Windows' legacy acceleration, doubles an injected move above its first threshold (6).
  move_by(6, 3, 40);
  frames(game, 2);
  const GraceUiTestInput moved = grace.input();
  // 2. No motion for a dozen frames: the game goes on recentring, and the pointer stays.
  frames(game, 12);
  const GraceUiTestInput kept = grace.input();
  // 3. Left by 800 counts: clamped at the client area's edge, never outside it.
  move_by(-80, 0, 10);
  frames(game, 2);
  const GraceUiTestInput edge = grace.input();
  menu = grace.menu();
  const int right = moved.imgui_mouse_x - start.imgui_mouse_x;
  const int down = moved.imgui_mouse_y - start.imgui_mouse_y;
  check(right >= 200 && right <= 280 && down >= 100 && down <= 140 && kept.imgui_mouse_x == moved.imgui_mouse_x &&
            kept.imgui_mouse_y == moved.imgui_mouse_y && edge.imgui_mouse_x == 0 && menu.off == 0,
        "menu recentred: the pointer by the mouse's motion",
        "from %d,%d: moved by 240,120 counts to %d,%d (%+d,%+d); after 12 frames of the game's recentring %d,%d; then "
        "800 counts left: %d (the client area's edge 0)",
        start.imgui_mouse_x, start.imgui_mouse_y, moved.imgui_mouse_x, moved.imgui_mouse_y, right, down,
        kept.imgui_mouse_x, kept.imgui_mouse_y, edge.imgui_mouse_x);
  press(window, VK_F5, kScanF5);
  frames(game, 2);
}

void run_menu_mouse(const std::wstring& dll) {
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  start_host(game, grace);
  until_atlas(game, grace);
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  const int seen_before = g_seen.mouse;

  // 1. A choice: a press on its second segment chooses it.
  const MenuBounds theme = menu_bounds(grace, "setting:ui.menu.theme");
  const float tx = theme.control[0] + theme.control[2] * 0.75f;
  const float ty = theme.control[1] + theme.control[3] / 2;
  post_mouse(window, WM_MOUSEMOVE, tx, ty);
  post_mouse(window, WM_LBUTTONDOWN, tx, ty, MK_LBUTTON);
  post_mouse(window, WM_LBUTTONUP, tx, ty);
  menu = menu_after(game, grace, menu.lists);
  const std::string chosen = grace.setting("ui.menu.theme");
  const std::string theme_focus = menu.focused;
  check(theme.found && chosen == "\"clear\"" && theme_focus == "setting:ui.menu.theme", "menu mouse: a choice",
        "the theme's control %s at %.0f,%.0f %.0fx%.0f; a click on its second segment: %s, focus %s",
        theme.found ? "drawn" : "NOT FOUND", theme.control[0], theme.control[1], theme.control[2], theme.control[3],
        chosen.c_str(), theme_focus.c_str());

  // 2. A slider: the press sets the value under it, the held button's move follows, the release lets go.
  const MenuBounds scale = menu_bounds(grace, "setting:ui.scale");
  const float sy = scale.control[1] + scale.control[3] / 2;
  const float left = scale.control[0] + scale.control[2] * 0.2f;
  const float right = scale.control[0] + scale.control[2] * 0.6f;
  post_mouse(window, WM_MOUSEMOVE, left, sy);
  post_mouse(window, WM_LBUTTONDOWN, left, sy, MK_LBUTTON);
  menu = menu_after(game, grace, menu.lists);
  const std::string pressed = grace.setting("ui.scale");
  post_mouse(window, WM_MOUSEMOVE, right, sy, MK_LBUTTON);
  menu = menu_after(game, grace, menu.lists);
  const std::string dragged = grace.setting("ui.scale");
  post_mouse(window, WM_LBUTTONUP, right, sy);
  post_mouse(window, WM_MOUSEMOVE, left, sy);
  menu = menu_after(game, grace, menu.lists);
  const std::string released = grace.setting("ui.scale");
  const double at_press = std::atof(pressed.c_str());
  const double at_drag = std::atof(dragged.c_str());
  check(scale.found && at_press >= 0.75 && at_press < at_drag && at_drag <= 1.5 && released == dragged,
        "menu mouse: a slider", "Menu size: pressed at 20%% of its column %s, dragged to 60%% %s, released and moved %s",
        pressed.c_str(), dragged.c_str(), released.c_str());

  // 3. The rail: a click on Installed mods opens it.
  const MenuBounds mods = menu_bounds(grace, "rail:graceui.mods");
  const float mx = mods.row[0] + mods.row[2] / 2;
  const float my = mods.row[1] + mods.row[3] / 2;
  post_mouse(window, WM_MOUSEMOVE, mx, my);
  post_mouse(window, WM_LBUTTONDOWN, mx, my, MK_LBUTTON);
  post_mouse(window, WM_LBUTTONUP, mx, my);
  menu = menu_after(game, grace, menu.lists);
  check(mods.found && std::string_view(menu.page) == "graceui.mods" && menu.refusals == 0 && menu.off == 0,
        "menu mouse: the rail", "a click on Installed mods: page %s, focus %s; refusals %u; the game's window saw %d "
        "mouse message(s) while the menu held the mouse",
        menu.page, menu.focused, menu.refusals, g_seen.mouse - seen_before);
  save_shot(game.frame_and_read(kInputColour, PresentWith::present), "menu-mouse-1920x1080.bmp");
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  DeleteFileW(settings_file_of(dll).c_str());  // the clicks kept a theme and a size: the next scenario starts afresh
}

// ---- Quiet notices (G32, D181): the Mods page's switch quiets a mod's notices, which are kept in its list instead of
// drawn over the game; the mod learns nothing; GraceUI's own notices still show; the next session keeps the switch.

// The mods' notices showing now (GraceUI's own left out): their texts, oldest first.
std::vector<std::string> mod_notices(const GraceUi& grace) {
  std::vector<std::string> out;
  for (const GraceUiTestNotice& notice : grace.notices()) {
    if (notice.owner != 0) {
      out.emplace_back(notice.text);
    }
  }
  return out;
}

std::string joined(const std::vector<std::string>& texts) {
  std::string out;
  for (const std::string& text : texts) {
    out += (out.empty() ? "\"" : ", \"") + text + "\"";
  }
  return out.empty() ? "none" : out;
}

// One of GraceUI's own topics as its space last published it, once GraceUI's thread has caught up.
grace::protocol::Json own_topic(Game& game, const GraceUi& grace, const char* topic,
                                const std::function<bool(const grace::protocol::Json&)>& until) {
  using grace::protocol::Json;
  Json body;
  for (int i = 0; i < 100; ++i) {
    std::vector<char> text(16 * 1024);
    const std::uint32_t length = grace.get_topic(topic, text.data(), static_cast<std::uint32_t>(text.size()));
    body = length == 0 ? Json() : Json::parse(text.data(), text.data() + length, nullptr, false);
    if (!body.is_discarded() && !body.is_null() && until(body)) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  return body;
}

// The Notices word of mod `id`'s row on the Mods page, or "" if it has no row.
std::string notices_word(const grace::protocol::Json& mods, const char* id) {
  if (!mods.is_object() || !mods.contains("rows") || !mods["rows"].is_array()) {
    return {};
  }
  for (const grace::protocol::Json& row : mods["rows"]) {
    if (row.value("id", std::string()) == id) {
      return row.value("notices", std::string());
    }
  }
  return {};
}

// The Mods page's rows in a few words: "quiet-camp Shown (Notices, 2.3.4); ...".
std::string rows_text(const grace::protocol::Json& mods) {
  if (!mods.is_object() || !mods.contains("rows") || !mods["rows"].is_array()) {
    return "NO BODY";
  }
  std::string out;
  for (const grace::protocol::Json& row : mods["rows"]) {
    out += (out.empty() ? "" : "; ") + row.value("id", std::string()) + " " + row.value("notices", std::string()) + " (" +
           row.value("uses", std::string()) + ", " + row.value("version", std::string()) + ")";
  }
  return out.empty() ? "none" : out;
}

// The kept list's rows in a few words: "Quiet Camp: \"...\"; ...".
std::string kept_text(const grace::protocol::Json& list) {
  if (!list.is_object() || !list.contains("rows") || !list["rows"].is_array()) {
    return "NO BODY";
  }
  std::string out;
  for (const grace::protocol::Json& row : list["rows"]) {
    out += (out.empty() ? "" : "; ") + row.value("mod", std::string()) + ": \"" + row.value("text", std::string()) + "\"";
  }
  return out.empty() ? "none" : out;
}

// Opens the menu and walks to mod `id`'s switch on the Mods page: Page Down to the page, ↓ to its row.
GraceUiTestMenu to_switch(Game& game, const GraceUi& grace, HWND window, const char* id) {
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  for (int i = 0; i < 4 && std::string_view(menu.page) != "graceui.mods"; ++i) {
    press(window, VK_NEXT, 0x51);
    menu = menu_after(game, grace, menu.lists);
  }
  const std::string want = std::string("cell:graceui.mods/") + id;
  for (int i = 0; i < 10 && menu.focused != want; ++i) {
    press(window, VK_DOWN, 0x50);
    menu = menu_after(game, grace, menu.lists);
  }
  return menu;
}

grace::protocol::Json wait_for_quiet(Game& game, const std::wstring& dll, const grace::protocol::Json& want) {
  grace::protocol::Json kept;
  for (int i = 0; i < 150; ++i) {
    kept = kept_settings(dll);
    if (!kept.is_discarded() && kept.is_object() && kept.value("quiet_mods", grace::protocol::Json()) == want) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  return kept;
}

void run_quiet_write(const std::wstring& dll) {
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  const Connected camp = connect(table, request_for("quiet-camp", GRACEUI_VERSION_PACK(0, 1, 0), "Quiet Camp", "notices"));
  const Connected tide =
      connect(table, request_for("tide-charts", GRACEUI_VERSION_PACK(0, 1, 0), "Tide Charts", "notices panels"));
  start_host(game, grace);
  until_atlas(game, grace);

  // 1. Both shown; the Mods page lists both, its switches on.
  const GraceUiStatus first = post(table, camp.answer.consumer, "The camp is pitched.", GRACEUI_TONE_NORMAL, 1);
  post(table, tide.answer.consumer, "The tide is in.");
  const Json mods = own_topic(game, grace, "graceui.mods", [](const Json& b) { return b["rows"].size() == 2; });
  const std::vector<std::string> shown = mod_notices(grace);
  const bool rows_ok = notices_word(mods, "quiet-camp") == "Shown" && notices_word(mods, "tide-charts") == "Shown" &&
                       mods["rows"][1].value("uses", std::string()) == "Notices, panels" &&
                       mods["rows"][1].value("version", std::string()) == "2.3.4";
  check(camp.status == GRACEUI_OK && tide.status == GRACEUI_OK && first == GRACEUI_OK && shown.size() == 2 && rows_ok,
        "quiet: the Mods page lists the mods connected", "notices showing: %s; the page's rows: %s",
        joined(shown).c_str(), rows_text(mods).c_str());

  // 2. Enter on Quiet Camp's switch in the menu: its notice goes, Tide Charts' stays.
  GraceUiTestMenu menu = to_switch(game, grace, window, "quiet-camp");
  const std::string focused_on = menu.focused;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const Json quieted = own_topic(game, grace, "graceui.mods",
                                 [](const Json& b) { return notices_word(b, "quiet-camp") == "Quiet"; });
  const std::vector<std::string> after = mod_notices(grace);
  check(focused_on == "cell:graceui.mods/quiet-camp" && notices_word(quieted, "quiet-camp") == "Quiet" &&
            notices_word(quieted, "tide-charts") == "Shown" && after == std::vector<std::string>{"The tide is in."} &&
            menu.refusals == 0 && grace.logged("quiet-camp's notices are quiet"),
        "quiet: the switch, from the menu", "focus on %s; Enter → %s; notices showing: %s; refusals %u",
        focused_on.c_str(), rows_text(quieted).c_str(), joined(after).c_str(), menu.refusals);

  // 3. What it posts now: accepted (the mod learns nothing), kept in the page's list, never drawn; a notice that is not
  // a notice's is still refused; a withdrawal is accepted. GraceUI's own still show.
  const GraceUiStatus kept_status = post(table, camp.answer.consumer, "Firewood is running low.");
  const GraceUiStatus keyed = post(table, camp.answer.consumer, "The camp is struck.", GRACEUI_TONE_WARNING, 1);
  const GraceUiStatus bad_tone = post(table, camp.answer.consumer, "A bad tone.", 9);
  GraceUiNotice withdraw{};
  withdraw.size = sizeof withdraw;
  withdraw.key = 1;
  const GraceUiStatus withdrawn = table->notice(camp.answer.consumer, &withdraw);
  const Json list = own_topic(game, grace, "graceui.notices", [](const Json& b) { return b["rows"].size() == 2; });
  const bool list_ok = list.is_object() && list["rows"].size() == 2 &&
                       list["rows"][0] == Json::object({{"mod", "Quiet Camp"}, {"text", "The camp is struck."}}) &&
                       list["rows"][1] == Json::object({{"mod", "Quiet Camp"}, {"text", "Firewood is running low."}});
  const Connected future = connect(table, request_for("future-mod", GRACEUI_VERSION_PACK(9, 0, 0), "Future mod"));
  frames(game, 3);
  bool own_shown = false;
  for (const GraceUiTestNotice& notice : grace.notices()) {
    own_shown = own_shown || (notice.owner == 0 && std::string_view(notice.text).find("Future mod needs") == 0);
  }
  const std::vector<std::string> still = mod_notices(grace);
  check(kept_status == GRACEUI_OK && keyed == GRACEUI_OK && bad_tone == GRACEUI_REFUSED && withdrawn == GRACEUI_OK &&
            list_ok && still == std::vector<std::string>{"The tide is in."} && future.status == GRACEUI_TOO_OLD &&
            own_shown,
        "quiet: a quiet mod's notices are kept, not drawn",
        "posts %s, %s, a bad tone %s, a withdrawal %s; the list: %s; showing: %s; GraceUI's own %s",
        status_name(kept_status), status_name(keyed), status_name(bad_tone), status_name(withdrawn),
        kept_text(list).c_str(), joined(still).c_str(), own_shown ? "shown" : "NOT SHOWN");
  menu = menu_after(game, grace, menu.lists);  // the page laid out again with the kept list
  save_shot(game.frame_and_read(kInputColour, PresentWith::present), "mods-grace-1920x1080.bmp");

  // 4. Kept for the next session: the file names the quiet mod, and only its id.
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  const Json file = wait_for_quiet(game, dll, Json::array({"quiet-camp"}));
  check(!file.is_discarded() && file.value("quiet_mods", Json()) == Json::array({"quiet-camp"}) &&
            file.value("values", Json()) == Json::object(),
        "quiet: kept for the next session", "the file: %s", file.is_discarded() ? "NO FILE" : file.dump().c_str());
}

void run_quiet_read(const std::wstring& dll) {
  using grace::protocol::Json;
  g_keep_settings = true;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  const Connected camp = connect(table, request_for("quiet-camp", GRACEUI_VERSION_PACK(0, 1, 0), "Quiet Camp", "notices"));
  start_host(game, grace);
  until_atlas(game, grace);
  const GraceUiStatus posted = post(table, camp.answer.consumer, "Smoke rises from the camp.");
  const Json mods = own_topic(game, grace, "graceui.mods",
                              [](const Json& b) { return notices_word(b, "quiet-camp") == "Quiet"; });
  const std::vector<std::string> shown = mod_notices(grace);
  check(posted == GRACEUI_OK && shown.empty() && notices_word(mods, "quiet-camp") == "Quiet" &&
            grace.logged("GraceUI's settings: 1 mods' notices kept quiet"),
        "quiet: the next session keeps it quiet", "a post %s; showing: %s; the page's rows: %s",
        status_name(posted), joined(shown).c_str(), rows_text(mods).c_str());

  // → on its switch: shown again, at once and for the next session.
  GraceUiTestMenu menu = to_switch(game, grace, window, "quiet-camp");
  press(window, VK_RIGHT, 0x4D);
  menu = menu_after(game, grace, menu.lists);
  const Json back = own_topic(game, grace, "graceui.mods",
                              [](const Json& b) { return notices_word(b, "quiet-camp") == "Shown"; });
  press(window, VK_F5, kScanF5);
  frames(game, 2);
  const GraceUiStatus again = post(table, camp.answer.consumer, "The fire is lit.");
  frames(game, 2);
  const std::vector<std::string> now = mod_notices(grace);
  Json file;
  for (int i = 0; i < 150; ++i) {
    file = kept_settings(dll);
    if (!file.is_discarded() && file.is_object() && !file.contains("quiet_mods")) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  check(notices_word(back, "quiet-camp") == "Shown" && again == GRACEUI_OK &&
            now == std::vector<std::string>{"The fire is lit."} && !file.is_discarded() && !file.contains("quiet_mods"),
        "quiet: shown again", "→: %s; a post %s, showing: %s; the file: %s", rows_text(back).c_str(), status_name(again),
        joined(now).c_str(), file.is_discarded() ? "NO FILE" : file.dump().c_str());
}

// ---- Other devices at home (G9, §7.2 item 16): the network listener on 127.0.0.2 (GraceUI_Test_SetNetworkAddresses,
// never a real address), Pair a device's sheet in the menu, a device paired with its code, refused what GraceUI marks
// sensitive, forgotten from the menu, and kept in user\devices.toml for the next session. Run from the build's GraceUI
// folder, as pages is, so that the server can start.

constexpr std::array<std::uint8_t, 4> kHomeAddress{127, 0, 0, 2};

std::wstring devices_file_of(const std::wstring& dll) {
  const std::size_t slash = dll.find_last_of(L"\\/");
  return (slash == std::wstring::npos ? std::wstring() : dll.substr(0, slash + 1)) + L"user\\devices.toml";
}

// The harness's note to its second process: the kept device's cookie, beside the file it pairs with; deleted after.
std::wstring device_note_of(const std::wstring& dll) {
  const std::size_t slash = dll.find_last_of(L"\\/");
  return (slash == std::wstring::npos ? std::wstring() : dll.substr(0, slash + 1)) + L"user\\harness-device.txt";
}

std::string home_host(std::uint16_t port) { return "127.0.0.2:" + std::to_string(port); }
std::string home_origin(std::uint16_t port) { return "http://127.0.0.2:" + std::to_string(port); }

std::string device_cookie_field(std::string_view cookie) {
  return cookie.empty() ? std::string() : "Cookie: en_device=" + std::string(cookie) + "\r\n";
}

std::string home_page_load(std::uint16_t port, std::string_view cookie = "") {
  return "GET / HTTP/1.1\r\nHost: " + home_host(port) +
         "\r\nSec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\n" +
         device_cookie_field(cookie) + "\r\n";
}

std::string home_post(std::uint16_t port, std::string_view path, std::string_view body, std::string_view cookie = "") {
  return "POST " + std::string(path) + " HTTP/1.1\r\nHost: " + home_host(port) + "\r\nOrigin: " + home_origin(port) +
         "\r\nSec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\nContent-Type: application/json\r\n" +
         device_cookie_field(cookie) + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

std::string home_upgrade(std::uint16_t port, std::string_view cookie) {
  return "GET /api/ws HTTP/1.1\r\nHost: " + home_host(port) + "\r\nOrigin: " + home_origin(port) +
         "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
         "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: websocket\r\n" +
         device_cookie_field(cookie) + "\r\n";
}

std::string device_from(const grace::serve::iotest::Reply& reply) {
  const std::optional<std::string> set = reply.field("set-cookie");
  if (!set || !set->starts_with("en_device=")) {
    return {};
  }
  return set->substr(10, set->find(';') - 10);
}

bool home_listening(std::uint16_t port) {
  grace::serve::iotest::Client client;
  return client.connect_ipv4(kHomeAddress, port);
}

// A device's WebSocket on the network listener: upgraded with its cookie, then its ticket.
bool device_join(grace::serve::iotest::WsClient& ws, std::uint16_t port, std::string_view cookie) {
  namespace io = grace::serve::iotest;
  const io::Reply ticket = io::request_ipv4(kHomeAddress, port, home_post(port, "/api/ticket", "{}", cookie));
  const grace::protocol::Json json = grace::protocol::Json::parse(ticket.body, nullptr, false);
  if (ticket.status != 200 || !json.is_object() || !json.contains("ticket")) {
    return false;
  }
  return ws.open_head(kHomeAddress, port, home_upgrade(port, cookie)) == 101 &&
         ws.hello(json["ticket"].get<std::string>());
}

// A command on a joined WebSocket and its result: "ok", or its error's name.
std::string answered(grace::serve::iotest::WsClient& ws, std::uint32_t id, const char* name, grace::protocol::Json args) {
  using grace::protocol::Json;
  ws.send_json(Json{{"v", 1}, {"t", "cmd"}, {"space", "graceui"}, {"id", id}, {"name", name}, {"args", std::move(args)}});
  for (int i = 0; i < 20; ++i) {
    const std::optional<Json> result = ws.next_of("result", std::chrono::milliseconds(500));
    if (result && result->value("id", 0u) == id) {
      return result->value("ok", false) ? std::string("ok") : result->value("error", std::string("no answer"));
    }
  }
  return "no answer";
}

// Opens the menu (if `open`) and walks to `page`, then down to `focus`.
GraceUiTestMenu to_focus(Game& game, const GraceUi& grace, HWND window, const char* page, const std::string& focus,
                         bool open) {
  GraceUiTestMenu menu = grace.menu();
  if (open) {
    press(window, VK_F5, kScanF5);
    menu = menu_after(game, grace, menu.lists);
  }
  for (int i = 0; i < 8 && std::string_view(menu.page) != page; ++i) {
    press(window, VK_NEXT, 0x51);
    menu = menu_after(game, grace, menu.lists);
  }
  for (const UINT key : {VK_DOWN, VK_UP}) {  // down first; up if it lies above
    for (int i = 0; i < 40 && menu.focused != focus; ++i) {
      const std::string before = menu.focused;
      press(window, key, key == VK_DOWN ? 0x50 : 0x48);
      menu = menu_after(game, grace, menu.lists);
      if (menu.focused == before) {
        break;  // the page's end
      }
    }
  }
  return menu;
}

std::string wait_code(Game& game, const GraceUi& grace) {
  std::string code;
  for (int i = 0; i < 150 && code.size() != 6; ++i) {
    frames(game, 1);
    Sleep(20);
    code = grace.pairing_code();
  }
  return code;
}

// True once no pairing code is showing, waited for as long as wait_code waits for one.
bool wait_no_code(Game& game, const GraceUi& grace) {
  for (int i = 0; i < 150; ++i) {
    if (grace.pairing_code().empty()) {
      return true;
    }
    frames(game, 1);
    Sleep(20);
  }
  return grace.pairing_code().empty();
}

bool wait_shows(Game& game, const GraceUi& grace, const std::string& text) {
  for (int i = 0; i < 150; ++i) {
    if (grace.menu_shows(text.c_str()) != 0) {
      return true;
    }
    frames(game, 1);
    Sleep(20);
  }
  return false;
}

std::string device_rows(const grace::protocol::Json& body) {
  if (!body.is_object() || !body.contains("rows") || !body["rows"].is_array()) {
    return "NO BODY";
  }
  std::string out;
  for (const grace::protocol::Json& row : body["rows"]) {
    out += (out.empty() ? "" : "; ") + row.value("name", std::string()) + " (" + row.value("state", std::string()) + ")";
  }
  return out.empty() ? "none" : out;
}

// The demo space for the browser view alone, so that the server runs and the menu opens on GraceUI's own pages.
GraceUiStatus register_browser_demo(const GraceUiApi1* table, GraceUiConsumer consumer) {
  const grace::pages::BuiltinSpace& space = grace::pages::builtin_spaces()[1];
  GraceUiSpaceDesc desc{};
  desc.size = sizeof desc;
  desc.views = GRACEUI_VIEW_BROWSER;
  desc.version = GRACEUI_VERSION_PACK(1, 0, 0);
  desc.pages = space.pages.data();
  desc.pages_length = space.pages.size();
  desc.settings = space.settings.data();
  desc.settings_length = space.settings.size();
  desc.increment = "0.2";
  desc.increment_length = 3;
  return table->register_pages(consumer, &desc);
}

GraceUiTestServer wait_running(Game& game, const GraceUi& grace) {
  GraceUiTestServer server = grace.server();
  for (int i = 0; i < 150 && server.running == 0; ++i) {
    frames(game, 1);
    Sleep(20);
    server = grace.server();
  }
  return server;
}

bool wait_listening(Game& game, std::uint16_t port, bool want) {
  for (int i = 0; i < 150; ++i) {
    if (home_listening(port) == want) {
      return true;
    }
    frames(game, 1);
    Sleep(20);
  }
  return false;
}

void run_pairing_write(const std::wstring& dll) {
  namespace io = grace::serve::iotest;
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_network_addresses(kHomeAddress.data(), 1);
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices pages"));
  const GraceUiStatus registered = register_browser_demo(table, demo.answer.consumer);
  const GraceUiTestServer server = wait_running(game, grace);
  if (registered != GRACEUI_OK || server.running == 0) {
    check(false, "pairing: the server runs", "register %s; the server %s", status_name(registered),
          server.running != 0 ? "runs" : "DOES NOT RUN");
    grace.print_log();
    return;
  }
  io::start_client_network();
  const std::uint16_t port = static_cast<std::uint16_t>(server.port);

  // 1. Off by default: nothing listens on the network address, and Pair a device says why.
  const bool closed_at_first = !home_listening(port);
  GraceUiTestMenu menu = to_focus(game, grace, window, "graceui.browser", "section-action:other-devices/pair", true);
  const std::string on_pair = menu.focused;
  const std::uint32_t refusals = menu.refusals;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const bool said_off = wait_shows(game, grace, "Turn on Other devices on your home network first.");
  check(closed_at_first && on_pair == "section-action:other-devices/pair" && menu.refusals == refusals + 1 && said_off &&
            grace.pairing_code().empty(),
        "pairing: off by default", "127.0.0.2 %s; focus on %s; Pair a device %s, %s",
        closed_at_first ? "closed" : "LISTENING", on_pair.c_str(), menu.refusals == refusals + 1 ? "refused" : "NOT REFUSED",
        said_off ? "saying why" : "WITHOUT ITS WORDS");

  // 2. On, from the menu: the listener opens; an unpaired device sees the pairing page alone, and no cookie.
  press(window, VK_DOWN, 0x50);
  menu = menu_after(game, grace, menu.lists);
  const std::string on_switch = menu.focused;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const bool opened = wait_listening(game, port, true);
  const io::Reply unpaired = io::request_ipv4(kHomeAddress, port, home_page_load(port));
  const io::Reply no_ticket = io::request_ipv4(kHomeAddress, port, home_post(port, "/api/ticket", "{}"));
  check(on_switch == "setting:dashboard.lan" && opened && unpaired.status == 200 &&
            unpaired.body.find("Pair this device") != std::string::npos && unpaired.count("set-cookie") == 0 &&
            no_ticket.status == 403,
        "pairing: on, an unpaired device sees the pairing page", "focus on %s; 127.0.0.2 %s; a page load %d%s; a ticket %d",
        on_switch.c_str(), opened ? "listening" : "NOT LISTENING", unpaired.status,
        unpaired.body.find("Pair this device") != std::string::npos ? " (the pairing page)" : " (NOT THE PAIRING PAGE)",
        no_ticket.status);

  // 3. Pair a device: the sheet shows the code and the address; a wrong code is refused; the right one pairs, once.
  press(window, VK_UP, 0x48);
  menu = menu_after(game, grace, menu.lists);
  const std::string on_pair_again = menu.focused;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const std::string code = wait_code(game, grace);
  if (code.size() != 6) {
    note("focus was on %s; refusals %u", on_pair_again.c_str(), menu.refusals);
  }
  const std::string spaced = code.size() == 6 ? code.substr(0, 3) + " " + code.substr(3) : std::string("?");
  const bool shows_code = wait_shows(game, grace, spaced);
  const bool shows_address = wait_shows(game, grace, "http://127.0.0.2:" + std::to_string(port) + "/");
  const std::string wrong_code = code == "000000" ? "000001" : "000000";
  const io::Reply wrong = io::request_ipv4(kHomeAddress, port,
                                           home_post(port, "/api/pair", Json{{"code", wrong_code}, {"name", "x"}}.dump()));
  const io::Reply paired = io::request_ipv4(
      kHomeAddress, port, home_post(port, "/api/pair", Json{{"code", code}, {"name", "Hallway tablet"}}.dump()));
  const std::string tablet = device_from(paired);
  const io::Reply again = io::request_ipv4(
      kHomeAddress, port, home_post(port, "/api/pair", Json{{"code", code}, {"name", "Hallway tablet"}}.dump()));
  const bool shows_paired = wait_shows(game, grace, "Hallway tablet is paired");
  const Json listed = own_topic(game, grace, "graceui.devices", [](const Json& b) { return b["rows"].size() == 1; });
  check(code.size() == 6 && shows_code && shows_address && wrong.status == 403 && paired.status == 200 &&
            !tablet.empty() && again.status == 403 && shows_paired && device_rows(listed).starts_with("Hallway tablet ("),
        "pairing: the sheet's code pairs a device once",
        "a code %s; shown %s, its address %s; a wrong code %d; the right one %d %s; again %d; the sheet %s; listed: %s",
        code.size() == 6 ? "of six digits" : "MISSING", shows_code ? "yes" : "NO", shows_address ? "yes" : "NO",
        wrong.status, paired.status, tablet.empty() ? "WITHOUT A COOKIE" : "with its cookie", again.status,
        shows_paired ? "says it is paired" : "DOES NOT SAY SO", device_rows(listed).c_str());

  // 4. Kept: devices.toml holds its name and a hash, never the cookie; the settings file never names a device.
  std::string file;
  for (int i = 0; i < 150 && file.find("Hallway tablet") == std::string::npos; ++i) {
    const std::vector<unsigned char> bytes = read_file(devices_file_of(dll));
    file.assign(bytes.begin(), bytes.end());
    frames(game, 1);
    Sleep(20);
  }
  const std::vector<unsigned char> settings_bytes = read_file(settings_file_of(dll));
  const std::string settings(settings_bytes.begin(), settings_bytes.end());
  check(file.find("graceui_devices = 1") != std::string::npos && file.find("name = \"Hallway tablet\"") != std::string::npos &&
            file.find("hash = \"") != std::string::npos && !tablet.empty() && file.find(tablet) == std::string::npos &&
            settings.find("Hallway") == std::string::npos && settings.find(tablet) == std::string::npos,
        "pairing: kept in devices.toml, never the cookie", "the file %s; the settings file %s",
        file.empty() ? "MISSING" : (file.find(tablet) == std::string::npos ? "holds a hash" : "HOLDS THE COOKIE"),
        settings.find("Hallway") == std::string::npos ? "names no device" : "NAMES THE DEVICE");

  // 5. The paired device opens the app and a WebSocket; it is refused what GraceUI marks sensitive, not the rest.
  const io::Reply app = io::request_ipv4(kHomeAddress, port, home_page_load(port, tablet));
  io::WsClient device;
  const bool device_joined = device_join(device, port, tablet);
  const std::string lan_off = answered(device, 1, "settings.set", Json{{"key", "dashboard.lan"}, {"value", false}});
  const std::string forget = answered(device, 2, "dashboard.forget_device", Json{{"id", "0123456789abcdef"}});
  const std::string pair = answered(device, 3, "dashboard.pair", Json::object());
  const std::string disconnect = answered(device, 4, "dashboard.disconnect", Json{{"confirm", true}});
  const std::string scale = answered(device, 5, "settings.set", Json{{"key", "ui.scale"}, {"value", 1.3}});
  const std::string menu_key = answered(device, 6, "settings.set", Json{{"key", "hotkeys.menu"}, {"value", "F6"}});
  const std::string hotkeys = answered(device, 7, "hotkeys.reset_all", Json{{"confirm", true}});
  const std::string notices = answered(device, 8, "mods.notices", Json{{"id", "demo"}, {"on", false}});
  check(app.status == 200 && app.body.find("Pair this device") == std::string::npos && device_from(app) == tablet &&
            device_joined && lan_off == "not_now" && forget == "not_now" && pair == "not_now" && disconnect == "not_now" &&
            menu_key == "not_now" && hotkeys == "not_now" && notices == "not_now" && scale == "ok",
        "pairing: a paired device, refused the sensitive commands",
        "the page %d%s; its WebSocket %s; dashboard.lan %s, forget %s, pair %s, disconnect %s, the menu's key %s, "
        "hotkeys %s, notices %s; ui.scale %s",
        app.status, device_from(app) == tablet ? " (its cookie renewed)" : " (NO RENEWAL)",
        device_joined ? "joined" : "REFUSED", lan_off.c_str(), forget.c_str(), pair.c_str(), disconnect.c_str(),
        menu_key.c_str(), hotkeys.c_str(), notices.c_str(), scale.c_str());

  // 6. The code lives only while its sheet is open (D113): Escape closes it, and closing the menu does too. The lists
  // are read afresh first: step 5's pairing and its device's ui.scale laid the menu out anew, so the snapshot from
  // step 4 would let menu_after return before Escape is handled (a load-dependent failure, 2026-11-23); the code's
  // end is then waited for itself, within wait_code's 3 s.
  menu = grace.menu();
  press(window, VK_ESCAPE, kScanEscape);
  menu = menu_after(game, grace, menu.lists);
  const bool gone_on_escape = wait_no_code(game, grace);
  press(window, VK_RETURN, 0x1C);  // focus is still on Pair a device
  menu = menu_after(game, grace, menu.lists);
  const std::string second = wait_code(game, grace);
  press(window, VK_F5, kScanF5);
  const bool gone_on_close = wait_no_code(game, grace);
  const io::Reply stale = io::request_ipv4(
      kHomeAddress, port, home_post(port, "/api/pair", Json{{"code", second}, {"name", "Late phone"}}.dump()));
  check(gone_on_escape && second.size() == 6 && gone_on_close && stale.status == 403 &&
            stale.body.find("none is showing") != std::string::npos,
        "pairing: the code ends with its sheet", "Escape %s; a new code %s; the menu closed %s; that code then %d",
        gone_on_escape ? "voids it" : "LEAVES IT", second.size() == 6 ? "shown" : "MISSING",
        gone_on_close ? "voids it" : "LEAVES IT", stale.status);

  // 7. A second device; then Forget the first from the menu: its WebSocket ends, its cookie opens the pairing page alone.
  menu = to_focus(game, grace, window, "graceui.browser", "section-action:other-devices/pair", true);
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const std::string third = wait_code(game, grace);
  const io::Reply laptop_reply = io::request_ipv4(
      kHomeAddress, port, home_post(port, "/api/pair", Json{{"code", third}, {"name", "Spare laptop"}}.dump()));
  const std::string laptop = device_from(laptop_reply);
  wait_shows(game, grace, "Spare laptop is paired");
  press(window, VK_ESCAPE, kScanEscape);
  menu = menu_after(game, grace, menu.lists);
  const Json both = own_topic(game, grace, "graceui.devices", [](const Json& b) { return b["rows"].size() == 2; });
  const std::string tablet_id =
      both.is_object() && both["rows"].size() == 2 ? both["rows"][0].value("id", std::string()) : std::string();
  menu = to_focus(game, grace, window, "graceui.browser", "row:graceui.devices/" + tablet_id + "/forget", false);
  const std::string on_forget = menu.focused;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_RETURN, 0x1C);  // destructive: the second press confirms
  menu = menu_after(game, grace, menu.lists);
  const std::optional<Json> bye = device.next_of("bye", std::chrono::milliseconds(3000));
  const Json left = own_topic(game, grace, "graceui.devices", [](const Json& b) { return b["rows"].size() == 1; });
  const io::Reply revoked = io::request_ipv4(kHomeAddress, port, home_page_load(port, tablet));
  check(!laptop.empty() && on_forget == "row:graceui.devices/" + tablet_id + "/forget" && bye.has_value() &&
            bye->value("reason", std::string()) == "unauthorised" && device_rows(left).starts_with("Spare laptop (") &&
            revoked.body.find("Pair this device") != std::string::npos,
        "pairing: Forget revokes a device at once", "a second device %s; focus on %s; the first's WebSocket %s; listed: %s; "
        "its cookie then opens %s",
        laptop.empty() ? "NOT PAIRED" : "paired", on_forget.c_str(),
        bye ? bye->value("reason", std::string("?")).c_str() : "STAYED OPEN", device_rows(left).c_str(),
        revoked.body.find("Pair this device") != std::string::npos ? "the pairing page" : "THE APP");

  // 8. Off from the menu: the listener and every device's connection close; the devices stay listed.
  io::WsClient laptop_ws;
  const bool laptop_joined = device_join(laptop_ws, port, laptop);
  menu = to_focus(game, grace, window, "graceui.browser", "setting:dashboard.lan", false);
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const std::optional<Json> shut = laptop_ws.next_of("bye", std::chrono::milliseconds(3000));
  const bool closed = wait_listening(game, port, false);
  const Json still = own_topic(game, grace, "graceui.devices",
                              [](const Json& b) { return device_rows(b) == "Spare laptop (Not connected)"; });
  check(laptop_joined && shut.has_value() && shut->value("reason", std::string()) == "shutdown" && closed &&
            device_rows(still) == "Spare laptop (Not connected)",
        "pairing: off closes every device connection", "the laptop's WebSocket %s, then %s; 127.0.0.2 %s; listed: %s",
        laptop_joined ? "joined" : "REFUSED", shut ? shut->value("reason", std::string("?")).c_str() : "STAYED OPEN",
        closed ? "closed" : "STILL LISTENING", device_rows(still).c_str());
  press(window, VK_F5, kScanF5);
  frames(game, 3);

  // 9. The log names no address, code, cookie or device.
  std::string named;
  char line[200];
  for (std::uint32_t i = 0; grace.log_line(i, line, sizeof line) > 0; ++i) {
    for (const std::string& secret : {std::string("127.0.0.2"), code, second, third, tablet, laptop,
                                      std::string("Hallway"), std::string("Spare laptop")}) {
      if (!secret.empty() && std::strstr(line, secret.c_str()) != nullptr) {
        named = line;
      }
    }
  }
  check(named.empty() && grace.logged("a device was paired with the browser view (2 paired)") &&
            grace.logged("a device was forgotten (1 paired)"),
        "pairing: the log names no address, code, cookie or device", "%s",
        named.empty() ? "counts only" : named.c_str());

  // For the next session: the laptop's cookie, which only its hash in devices.toml can match.
  if (HANDLE note = CreateFileW(device_note_of(dll).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
      note != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(note, laptop.data(), static_cast<DWORD>(laptop.size()), &written, nullptr);
    CloseHandle(note);
  }
}

void run_pairing_read(const std::wstring& dll) {
  namespace io = grace::serve::iotest;
  using grace::protocol::Json;
  g_keep_settings = true;
  const std::vector<unsigned char> note = read_file(device_note_of(dll));
  const std::string laptop(note.begin(), note.end());
  DeleteFileW(device_note_of(dll).c_str());
  Game game(false, 1920, 1080);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  grace.set_network_addresses(kHomeAddress.data(), 1);
  const GraceUiApi1* table = start_host(game, grace);
  until_atlas(game, grace);
  const Connected demo = connect(table, request_for("demo", GRACEUI_VERSION_PACK(0, 1, 0), "The demo space", "notices pages"));
  register_browser_demo(table, demo.answer.consumer);
  const GraceUiTestServer server = wait_running(game, grace);
  io::start_client_network();
  const std::uint16_t port = static_cast<std::uint16_t>(server.port);
  const Json listed = own_topic(game, grace, "graceui.devices", [](const Json& b) { return b["rows"].size() == 1; });
  // Other devices was left off: on again from this PC's browser (dashboard.lan is never a device's to change).
  const io::Reply page = io::request(port, io::page_load(port));
  const std::string cookie = io::cookie_from(page);
  io::WsClient pc;
  const bool joined = !cookie.empty() && pc.join(port, cookie, io::fetch_ticket(port, cookie));
  const std::string on = joined ? answered(pc, 1, "settings.set", Json{{"key", "dashboard.lan"}, {"value", true}})
                                : std::string("not joined");
  const bool opened = wait_listening(game, port, true);
  const io::Reply kept = io::request_ipv4(kHomeAddress, port, home_page_load(port, laptop));
  io::WsClient device;
  const bool device_joined = device_join(device, port, laptop);
  const io::Reply stranger =
      io::request_ipv4(kHomeAddress, port, home_page_load(port, std::string(laptop.size(), 'A')));
  check(server.running == 1 && grace.logged("paired devices: 1 kept from an earlier session, 0 left out") &&
            device_rows(listed).starts_with("Spare laptop (") && on == "ok" && opened && !laptop.empty() &&
            kept.status == 200 && kept.body.find("Pair this device") == std::string::npos && device_joined &&
            stranger.body.find("Pair this device") != std::string::npos,
        "pairing: the next session keeps the device", "listed: %s; on from this PC %s; 127.0.0.2 %s; the kept cookie "
        "opens %s, its WebSocket %s; another cookie the %s",
        device_rows(listed).c_str(), on.c_str(), opened ? "listening" : "NOT LISTENING",
        kept.body.find("Pair this device") == std::string::npos ? "the app" : "THE PAIRING PAGE",
        device_joined ? "joined" : "REFUSED",
        stranger.body.find("Pair this device") != std::string::npos ? "pairing page" : "APP");
}

void run_pairing(const std::wstring& dll) {
  DeleteFileW(devices_file_of(dll).c_str());
  DeleteFileW(device_note_of(dll).c_str());
  for (const wchar_t* child : {L"pairing-write", L"pairing-read"}) {
    std::fflush(stdout);
    const int status = run_child(dll, child);
    char name[64];
    std::snprintf(name, sizeof name, "%ls scenario", child);
    check(status == 0, name, "its process ended with %d", status);
  }
  DeleteFileW(devices_file_of(dll).c_str());
  DeleteFileW(device_note_of(dll).c_str());
  DeleteFileW(settings_file_of(dll).c_str());
}

void run_quiet(const std::wstring& dll) {
  DeleteFileW(settings_file_of(dll).c_str());
  for (const wchar_t* child : {L"quiet-write", L"quiet-read"}) {
    std::fflush(stdout);
    const int status = run_child(dll, child);
    char name[64];
    std::snprintf(name, sizeof name, "%ls scenario", child);
    check(status == 0, name, "its process ended with %d", status);
  }
  DeleteFileW(settings_file_of(dll).c_str());
}

// ---- The display (D180): what a mod learns at connect (ui.scale and its default; the size once a swap chain is bound),
// its GRACEUI_EVENT_DISPLAY once after warm-up and after each change of the size or ui.scale, only at 1.2's event size,
// and a GRACEUI_FACE_SCALED face baked at its pixel height times the content scale.

GraceUiStatus face_of(const GraceUiApi1* table, GraceUiConsumer me, const std::vector<unsigned char>& data,
                      std::uint32_t pixels, std::uint32_t flags, std::uint32_t& id) {
  GraceUiFaceDesc desc{};
  desc.size = sizeof desc;
  desc.pixel_height = pixels;
  desc.data = data.data();
  desc.data_length = data.size();
  desc.flags = flags;
  return table->register_face(me, &desc, &id);
}

std::vector<GraceUiEvent> displays_of(const GraceUiApi1* table, GraceUiConsumer me,
                                      std::uint32_t size = sizeof(GraceUiEvent)) {
  std::vector<GraceUiEvent> out;
  for (const GraceUiEvent& e : poll_holder(table, me, size)) {
    if (e.kind == GRACEUI_EVENT_DISPLAY) {
      out.push_back(e);
    }
  }
  return out;
}

bool close_to(float a, float b) { return std::fabs(a - b) < 0.002f; }

std::string display_text(const std::vector<GraceUiEvent>& events) {
  std::string text;
  for (const GraceUiEvent& e : events) {
    char one[160];
    std::snprintf(one, sizeof one, "%ux%u, ui.scale %.2f (default %.2f), scale %.3f, content %.3f, faces %.3f",
                  e.display_width, e.display_height, e.ui_scale, e.ui_scale_default, e.scale, e.content_scale,
                  e.faces_scale);
    text += text.empty() ? "" : "; ";
    text += one;
  }
  return text.empty() ? "none" : text;
}

// ---- Rebinding hotkeys (G37, D208 to D210): in the menu, a row's key captured, a taken one asked about and moved, the
// menu's key moved; each mod built for ABI 1.4 told by GRACEUI_EVENT_HOTKEY_CHANGED and describe_hotkey; all kept for
// the next session, in two processes of their own; then every hotkey reset from the page's action.

GraceUiStatus hotkey_of(const GraceUiApi1* table, GraceUiConsumer mod, const char* action, const char* name,
                        std::uint32_t vk, std::uint32_t& id) {
  GraceUiHotkeyDesc desc{};
  desc.size = sizeof desc;
  desc.key = vk;
  desc.action = action;
  desc.action_length = std::strlen(action);
  desc.name = name;
  desc.name_length = std::strlen(name);
  return table->register_hotkey(mod, &desc, &id);
}

GraceUiHotkeyBinding binding_of(const GraceUiApi1* table, GraceUiConsumer mod, std::uint32_t id, GraceUiStatus& status) {
  GraceUiHotkeyBinding out{};
  out.size = sizeof out;
  status = GRACEUI_API1_COVERS(table, describe_hotkey) ? table->describe_hotkey(mod, id, &out) : GRACEUI_NOT_YET;
  return out;
}

// The ids of the hotkeys whose change the mod was told of, until it has none waiting.
std::vector<std::uint32_t> changes_of(const GraceUiApi1* table, GraceUiConsumer mod) {
  std::vector<std::uint32_t> ids;
  for (const GraceUiEvent& e : poll_holder(table, mod)) {
    if (e.kind == GRACEUI_EVENT_HOTKEY_CHANGED) {
      ids.push_back(e.hotkey_id);
    }
  }
  return ids;
}

// The cell `column` of the Hotkeys page's row `id`, as GraceUI's space last published it.
std::string hotkey_cell(const grace::protocol::Json& body, const char* id, const char* column) {
  if (body.is_object() && body.contains("rows") && body["rows"].is_array()) {
    for (const grace::protocol::Json& row : body["rows"]) {
      if (row.is_object() && row.value("id", std::string()) == id) {
        return row.value(column, std::string());
      }
    }
  }
  return {};
}

// The menu opened (with `menu_vk`), on the Hotkeys page, focus moved down to `focus`.
GraceUiTestMenu to_hotkeys(Game& game, const GraceUi& grace, HWND window, const char* focus, UINT menu_vk = VK_F5,
                           UINT menu_scan = kScanF5) {
  GraceUiTestMenu menu = grace.menu();
  if (menu.open == 0) {
    press(window, menu_vk, menu_scan);
    menu = menu_after(game, grace, 0);
  }
  for (int i = 0; i < 6 && std::string_view(menu.page) != "graceui.hotkeys"; ++i) {
    press(window, VK_NEXT, 0x51);
    menu = menu_after(game, grace, menu.lists);
  }
  for (int i = 0; i < 6 && std::string_view(menu.focused) != "action:reset-all"; ++i) {
    press(window, VK_UP, 0x48);
    menu = menu_after(game, grace, menu.lists);
  }
  for (int i = 0; i < 30 && std::string_view(menu.focused) != focus; ++i) {
    press(window, VK_DOWN, 0x50);
    menu = menu_after(game, grace, menu.lists);
  }
  return menu;
}

grace::protocol::Json wait_for_bindings(Game& game, const std::wstring& dll, bool want_some) {
  grace::protocol::Json kept;
  for (int i = 0; i < 150; ++i) {
    kept = kept_settings(dll);
    if (!kept.is_discarded() && kept.is_object() && kept.contains("hotkey_bindings") == want_some) {
      break;
    }
    frames(game, 1);
    Sleep(20);
  }
  return kept;
}

void run_bindings_write(const std::wstring& dll) {
  using grace::protocol::Json;
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  const Connected tide = connect(table, request_for("tide-charts", GRACEUI_VERSION_PACK(0, 2, 16), "Tide Charts", "hotkeys hotkey-bindings wake"));
  const Connected camp = connect(table, request_for("quiet-camp", GRACEUI_VERSION_PACK(0, 2, 16), "Quiet Camp", "hotkeys hotkey-bindings"));
  GraceUiConnectRequest older_request = request_for("older-mod", GRACEUI_VERSION_PACK(0, 2, 2), "Older mod", "hotkeys");
  older_request.abi_minor = 3;
  const Connected older = connect(table, older_request);
  std::uint32_t tide_id = 0, camp_id = 0, older_id = 0;
  const GraceUiStatus tide_status = hotkey_of(table, tide.answer.consumer, "tide-table", "Show the tide table", VK_F9, tide_id);
  const GraceUiStatus camp_status = hotkey_of(table, camp.answer.consumer, "pitch-tent", "Pitch the tent", VK_F7, camp_id);
  const GraceUiStatus older_status = hotkey_of(table, older.answer.consumer, "old", "An old one", VK_F11, older_id);
  GraceUiStatus described_status = GRACEUI_FAULT;
  const GraceUiHotkeyBinding before = binding_of(table, tide.answer.consumer, tide_id, described_status);
  GraceUiStatus foreign_status = GRACEUI_FAULT;
  binding_of(table, camp.answer.consumer, tide_id, foreign_status);
  GraceUiHotkeyBinding short_one{};
  short_one.size = 16;
  const GraceUiStatus small_status = table->describe_hotkey(tide.answer.consumer, tide_id, &short_one);
  const bool cap = table->has_cap(GRACEUI_CAP_HOTKEY_BINDINGS, std::strlen(GRACEUI_CAP_HOTKEY_BINDINGS)) != 0;
  check(tide.status == GRACEUI_OK && camp.status == GRACEUI_OK && older.status == GRACEUI_OK && cap &&
            tide_status == GRACEUI_OK && camp_status == GRACEUI_OK && older_status == GRACEUI_OK &&
            described_status == GRACEUI_OK && before.key == VK_F9 && std::string(before.key_text) == "F9" &&
            before.bound == GRACEUI_HOTKEY_KEY_BOUND && before.chord_text[0] == '\0' &&
            foreign_status == GRACEUI_REFUSED && small_status == GRACEUI_REFUSED && table->abi_minor >= 4u,
        "bindings: describe_hotkey",
        "has_cap(\"%s\") %s, minor %u; Tide Charts' F9: %s, key %u \"%s\", bound %u; another mod's id %s; a binding "
        "of 16 bytes %s",
        GRACEUI_CAP_HOTKEY_BINDINGS, cap ? "yes" : "NO", table->abi_minor, status_name(described_status), before.key,
        before.key_text, before.bound, status_name(foreign_status), status_name(small_status));

  void* tide_wake = nullptr;  // its wait handle (D212): set by the change below
  table->wake_event(tide.answer.consumer, &tide_wake);
  start_host(game, grace);
  until_atlas(game, grace);
  // 1. Change key on Tide Charts' row in the menu: Enter, then F8, taken whole.
  GraceUiTestMenu menu = to_hotkeys(game, grace, window, "row:graceui.hotkeys/tide-charts/tide-table/key");
  poll_holder(table, tide.answer.consumer, sizeof(GraceUiEvent));
  while (tide_wake != nullptr && WaitForSingleObject(tide_wake, 0) == WAIT_OBJECT_0) {
  }
  const std::string on_row = menu.focused;
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const bool asks_for_key = grace.menu_shows("Press the key to use");
  press(window, VK_F8, 0x42);
  menu = menu_after(game, grace, menu.lists);
  const Json first = own_topic(game, grace, "graceui.hotkeys", [](const Json& b) {
    return hotkey_cell(b, "tide-charts/tide-table", "key") == "F8";
  });
  const bool woken = tide_wake != nullptr && WaitForSingleObject(tide_wake, 0) == WAIT_OBJECT_0;
  if (tide_wake != nullptr) {
    CloseHandle(tide_wake);
  }
  GraceUiStatus after_status = GRACEUI_FAULT;
  const GraceUiHotkeyBinding after = binding_of(table, tide.answer.consumer, tide_id, after_status);
  const std::vector<std::uint32_t> told = changes_of(table, tide.answer.consumer);
  check(on_row == "row:graceui.hotkeys/tide-charts/tide-table/key" && asks_for_key &&
            hotkey_cell(first, "tide-charts/tide-table", "key") == "F8" && after.key == VK_F8 &&
            std::string(after.key_text) == "F8" && told == std::vector<std::uint32_t>{tide_id} && menu.open == 1 &&
            woken,
        "bindings: a key captured in the menu",
        "focus on %s; the modal %s; the page's row: %s; describe_hotkey: \"%s\" (vk %u); told: %zu change(s), its "
        "wait handle %s; the menu %s",
        on_row.c_str(), asks_for_key ? "asked for a key" : "DID NOT ASK", hotkey_cell(first, "tide-charts/tide-table", "key").c_str(),
        after.key_text, after.key, told.size(), woken ? "set" : "NOT SET", menu.open != 0 ? "still open" : "CLOSED");

  // 2. F7, Quiet Camp's: asked first, Enter moves it; Quiet Camp's hotkey is left with no key and told so.
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_F7, 0x41);
  menu = menu_after(game, grace, menu.lists);
  const bool asked = grace.menu_shows("F7 is Quiet Camp's Pitch the tent. Use it here, and leave that one with no key?");
  const Json unmoved = own_topic(game, grace, "graceui.hotkeys", [](const Json&) { return true; });
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const Json moved = own_topic(game, grace, "graceui.hotkeys", [](const Json& b) {
    return hotkey_cell(b, "quiet-camp/pitch-tent", "key") == "No key";
  });
  const std::vector<std::uint32_t> camp_told = changes_of(table, camp.answer.consumer);
  GraceUiStatus camp_status_now = GRACEUI_FAULT;
  const GraceUiHotkeyBinding camp_now = binding_of(table, camp.answer.consumer, camp_id, camp_status_now);
  check(asked && hotkey_cell(unmoved, "quiet-camp/pitch-tent", "key") == "F7" &&
            hotkey_cell(moved, "tide-charts/tide-table", "key") == "F7" &&
            hotkey_cell(moved, "quiet-camp/pitch-tent", "key") == "No key" &&
            camp_told == std::vector<std::uint32_t>{camp_id} && camp_now.key == 0 && camp_now.bound == 0 &&
            changes_of(table, older.answer.consumer).empty(),
        "bindings: a taken key asked about, then moved",
        "asked %s; before Enter Quiet Camp kept %s; after: Tide Charts %s, Quiet Camp %s (told %zu, key %u); the older "
        "mod told nothing",
        asked ? "yes" : "NO", hotkey_cell(unmoved, "quiet-camp/pitch-tent", "key").c_str(),
        hotkey_cell(moved, "tide-charts/tide-table", "key").c_str(),
        hotkey_cell(moved, "quiet-camp/pitch-tent", "key").c_str(), camp_told.size(), camp_now.key);

  // 3. The menu's key moved to F6 on its row: F6 then closes the menu, and F5 opens nothing.
  menu = to_hotkeys(game, grace, window, "setting:hotkeys.menu");
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_F6, 0x40);
  menu = menu_after(game, grace, menu.lists);
  std::string menu_key;
  for (int i = 0; i < 100 && menu_key != "\"F6\""; ++i) {
    menu_key = grace.setting("hotkeys.menu");
    frames(game, 1);
    Sleep(20);
  }
  frames(game, 10);
  press(window, VK_F6, 0x40);
  for (int i = 0; i < 100 && grace.menu().open != 0; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  const bool closed_by_f6 = grace.menu().open == 0;
  press(window, VK_F5, kScanF5);
  frames(game, 20);
  Sleep(200);
  const bool f5_opens = grace.menu().open != 0;
  check(menu_key == "\"F6\"" && closed_by_f6 && !f5_opens && grace.logged("the menu's key is F6"),
        "bindings: the menu's key moved", "hotkeys.menu %s; F6 %s the menu; F5 then %s it", menu_key.c_str(),
        closed_by_f6 ? "closed" : "DID NOT CLOSE", f5_opens ? "OPENED" : "did not open");

  // 4. Kept for the next session: ids and key names only.
  const Json file = wait_for_bindings(game, dll, true);
  const Json bindings = file.is_object() ? file.value("hotkey_bindings", Json()) : Json();
  const Json want = Json::array({Json::object({{"mod", "tide-charts"}, {"action", "tide-table"}, {"key", "F7"}, {"chord", 0}}),
                                 Json::object({{"mod", "quiet-camp"}, {"action", "pitch-tent"}, {"key", ""}, {"chord", 0}})});
  bool same = bindings.is_array() && bindings.size() == 2;
  for (const Json& one : want) {
    same = same && std::find(bindings.begin(), bindings.end(), one) != bindings.end();
  }
  check(same && file.value("values", Json()).value("hotkeys.menu", Json()) == "F6", "bindings: kept for the next session",
        "the file: %s", file.is_discarded() ? "NO FILE" : file.dump().c_str());
}

void run_bindings_read(const std::wstring& dll) {
  using grace::protocol::Json;
  g_keep_settings = true;  // what bindings-write kept (D208)
  Game game(false, 1920, 1080);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  // Tide Charts connects before GraceUI's thread has read its settings: its default F9 first, the kept F7 once read.
  const Connected tide = connect(table, request_for("tide-charts", GRACEUI_VERSION_PACK(0, 2, 16), "Tide Charts", "hotkeys hotkey-bindings"));
  std::uint32_t tide_id = 0, camp_id = 0;
  const GraceUiStatus tide_first = hotkey_of(table, tide.answer.consumer, "tide-table", "Show the tide table", VK_F9, tide_id);
  start_host(game, grace);
  until_atlas(game, grace);
  for (int i = 0; i < 150 && !grace.logged("2 hotkey bindings kept"); ++i) {
    frames(game, 1);
    Sleep(20);
  }
  // Quiet Camp connects after: its kept "no key" at once, over its default F7.
  const Connected camp = connect(table, request_for("quiet-camp", GRACEUI_VERSION_PACK(0, 2, 16), "Quiet Camp", "hotkeys hotkey-bindings"));
  const GraceUiStatus camp_second = hotkey_of(table, camp.answer.consumer, "pitch-tent", "Pitch the tent", VK_F7, camp_id);
  GraceUiStatus a = GRACEUI_FAULT, b = GRACEUI_FAULT;
  const GraceUiHotkeyBinding tide_now = binding_of(table, tide.answer.consumer, tide_id, a);
  const GraceUiHotkeyBinding camp_now = binding_of(table, camp.answer.consumer, camp_id, b);
  const std::vector<std::uint32_t> tide_told = changes_of(table, tide.answer.consumer);
  for (int i = 0; i < 30; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  press(window, VK_F5, kScanF5);
  frames(game, 20);
  Sleep(200);
  const bool f5_opens = grace.menu().open != 0;
  press(window, VK_F6, 0x40);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  check(tide_first == GRACEUI_OK && camp_second == GRACEUI_UNBOUND && tide_now.key == VK_F7 &&
            std::string(tide_now.key_text) == "F7" &&
            (tide_told.empty() || tide_told == std::vector<std::uint32_t>{tide_id}) &&
            camp_now.key == 0 && !f5_opens && menu.open == 1 && grace.logged("2 hotkey bindings kept"),
        "bindings: the next session keeps them",
        "Tide Charts, registered as GraceUI started, %s and now \"%s\" (told %zu: 1 if its thread read the settings "
        "after, 0 if before); Quiet Camp, after, %s, "
        "its key %u; F5 %s; F6 %s",
        status_name(tide_first), tide_now.key_text, tide_told.size(), status_name(camp_second), camp_now.key,
        f5_opens ? "OPENED the menu" : "opened nothing", menu.open != 0 ? "opened the menu" : "DID NOT OPEN IT");

  // Reset every hotkey, from the page's action and its second press: the mods' defaults where free, nothing kept.
  menu = to_hotkeys(game, grace, window, "action:reset-all", VK_F6, 0x40);
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_RETURN, 0x1C);
  menu = menu_after(game, grace, menu.lists);
  const Json reset = own_topic(game, grace, "graceui.hotkeys", [](const Json& body) {
    return hotkey_cell(body, "tide-charts/tide-table", "key") == "F9";
  });
  const Json file = wait_for_bindings(game, dll, false);
  check(hotkey_cell(reset, "tide-charts/tide-table", "key") == "F9" &&
            hotkey_cell(reset, "quiet-camp/pitch-tent", "key") == "F7" &&
            hotkey_cell(reset, "quiet-camp/pitch-tent", "state") == "Bound" && file.is_object() &&
            !file.contains("hotkey_bindings"),
        "bindings: every hotkey reset", "Tide Charts %s, Quiet Camp %s (%s); the file: %s",
        hotkey_cell(reset, "tide-charts/tide-table", "key").c_str(), hotkey_cell(reset, "quiet-camp/pitch-tent", "key").c_str(),
        hotkey_cell(reset, "quiet-camp/pitch-tent", "state").c_str(), file.is_discarded() ? "NO FILE" : file.dump().c_str());
}

void run_bindings(const std::wstring& dll) {
  DeleteFileW(settings_file_of(dll).c_str());
  for (const wchar_t* child : {L"bindings-write", L"bindings-read"}) {
    std::fflush(stdout);
    const int status = run_child(dll, child);
    char name[64];
    std::snprintf(name, sizeof name, "%ls scenario", child);
    check(status == 0, name, "its process ended with %d", status);
  }
  DeleteFileW(settings_file_of(dll).c_str());
}

// ---- A wait handle for each mod's events (D212, ABI 1.5): set once when asked for, then whenever something is kept for
// the mod's poll, timed by a thread that waits on it while this one plays the game's part.
void run_wake(const std::wstring& dll) {
  Game game(false, 1280, 720);
  const HWND window = game.window();
  cursor_into(window);
  pump_messages();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  const Connected connected = connect(table, request_for("wake-mod", GRACEUI_VERSION_PACK(0, 2, 17), "Wake mod",
                                                         "hotkeys input holder-events display wake panels textures"));
  HarnessMod mod{table, connected.answer.consumer, 0};
  const std::uint32_t white_pixels[16] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  std::uint64_t field = 0;
  const bool made = mod.texture(4, 4, white_pixels, mod.white) == GRACEUI_OK &&
                    mod.panel(GRACEUI_LAYER_MODAL, GRACEUI_ANCHOR_TOP_LEFT, 200, 100, 300, 120, GRACEUI_PANEL_TAKES_INPUT,
                              field) == GRACEUI_OK;
  mod.quad(field, 300, 120, Colour{40, 60, 90});
  table->show_panel(mod.me, field);
  std::uint32_t f9 = 0;
  const GraceUiStatus hotkey = hotkey_of(table, mod.me, "wake-key", "Wake key", VK_F9, f9);
  const bool covers = GRACEUI_API1_COVERS(table, wake_event);
  const GraceUiStatus refused = covers ? table->wake_event(mod.me, nullptr) : GRACEUI_NOT_YET;
  void* handle = nullptr;
  const GraceUiStatus got = covers ? table->wake_event(mod.me, &handle) : GRACEUI_NOT_YET;
  const bool set_at_ask = handle != nullptr && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
  const bool then_unset = handle != nullptr && WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;  // auto-reset
  const bool cap = table->has_cap(GRACEUI_CAP_WAKE, std::strlen(GRACEUI_CAP_WAKE)) != 0;
  check(made && hotkey == GRACEUI_OK && covers && cap && refused == GRACEUI_REFUSED && got == GRACEUI_OK && set_at_ask &&
            then_unset && table->abi_minor >= 5u,
        "wake: the handle", "has_cap(\"%s\") %s, minor %u; a null place %s; asked %s, set once at once %s, then unset %s",
        GRACEUI_CAP_WAKE, cap ? "yes" : "NO", table->abi_minor, status_name(refused), status_name(got),
        set_at_ask ? "yes" : "NO", then_unset ? "yes" : "NO");
  if (handle == nullptr) {
    return;
  }
  start_host(game, grace);
  until_atlas(game, grace);
  frames(game, 30);

  // How long after `act` the waiting thread woke, in ms; -1 if it did not within 2 s. What waited before is taken first.
  const auto timed = [&](const std::function<void()>& act) {
    poll_holder(table, mod.me, sizeof(GraceUiEvent));
    while (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
    }
    std::atomic<std::int64_t> woke{0};
    std::thread waiter([&] {
      if (WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0) {
        woke.store(std::chrono::steady_clock::now().time_since_epoch().count());
      }
    });
    const auto start = std::chrono::steady_clock::now();
    act();
    for (int i = 0; i < 200 && woke.load() == 0; ++i) {
      pump_messages();
      game.frame(kInputColour, PresentWith::present);
      Sleep(5);
    }
    waiter.join();
    const std::int64_t at = woke.load();
    if (at == 0) {
      return -1.0;
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::duration(at) - start.time_since_epoch())
        .count();
  };
  const auto kinds = [&] {
    std::vector<std::uint32_t> out;
    for (const GraceUiEvent& e : poll_holder(table, mod.me, sizeof(GraceUiEvent))) {
      out.push_back(e.kind);
    }
    return out;
  };
  const auto has = [](const std::vector<std::uint32_t>& got_kinds, std::uint32_t kind) {
    return std::find(got_kinds.begin(), got_kinds.end(), kind) != got_kinds.end();
  };

  // 1. A hotkey's press.
  const double pressed_ms = timed([&] {
    post_key(window, VK_F9, 0x43, true, false);
    post_key(window, VK_F9, 0x43, false, false);
  });
  const std::vector<std::uint32_t> after_press = kinds();
  // 2. A holder's event, with the mod holding input.
  GraceUiStatus request_status = GRACEUI_FAULT;
  const bool holds = mod.request(field, GRACEUI_HOLDER_MODAL, request_status, GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE,
                                 GRACEUI_INPUT_EVENTS).answer == GRACEUI_INPUT_GRANTED;
  frames(game, 2);
  const double typed_ms = timed([&] {
    post_key(window, VK_LEFT, 0x4B, true, false);
    post_key(window, VK_LEFT, 0x4B, false, false);
  });
  const std::vector<std::uint32_t> after_typing = kinds();
  table->release_input(mod.me);
  frames(game, 3);
  // 3. The display.
  const double resized_ms = timed([&] { game.resize(1600, 900); });
  const std::vector<std::uint32_t> after_resize = kinds();
  // 4. Nothing: nothing wakes it.
  poll_holder(table, mod.me, sizeof(GraceUiEvent));
  while (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
  }
  for (int i = 0; i < 30; ++i) {
    pump_messages();
    game.frame(kInputColour, PresentWith::present);
    Sleep(10);
  }
  const bool quiet = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
  check(pressed_ms >= 0 && pressed_ms < 50 && has(after_press, GRACEUI_EVENT_HOTKEY) && holds && typed_ms >= 0 &&
            typed_ms < 50 && has(after_typing, GRACEUI_EVENT_KEY) && resized_ms >= 0 &&
            has(after_resize, GRACEUI_EVENT_DISPLAY) && quiet,
        "wake: set for what the mod's poll has",
        "a hotkey's press woke the waiting thread in %.1f ms (polled: hotkey %s); a holder's key in %.1f ms (key "
        "%s); a resize in %.1f ms (display %s); with nothing to poll, it stayed unset %s",
        pressed_ms, has(after_press, GRACEUI_EVENT_HOTKEY) ? "yes" : "NO", typed_ms,
        has(after_typing, GRACEUI_EVENT_KEY) ? "yes" : "NO", resized_ms,
        has(after_resize, GRACEUI_EVENT_DISPLAY) ? "yes" : "NO", quiet ? "yes" : "NO");
  CloseHandle(handle);  // the mod's own; GraceUI keeps its event
}

// ---- Pad chords (D204, proposal 0005), through GraceUI_Test_PadRead (Wine has no pad): what the game's XInputGetState
// returns read by read, and the mod's hotkey event. The times are the proposal's estimates, with margins for Wine's
// clock; S4's second round confirms them on a pad.
void run_chords(const std::wstring& dll) {
  Game game(false);
  cursor_into(game.window());
  pump_messages();
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  constexpr std::uint32_t kUp = XINPUT_GAMEPAD_DPAD_UP;
  constexpr std::uint32_t kView = XINPUT_GAMEPAD_BACK;
  constexpr std::uint32_t kPacket = 40;  // the pad's own packet number, which these reads never change
  const Connected first = connect(table, request_for("chord-mod", GRACEUI_VERSION_PACK(0, 2, 14), "Chord mod",
                                                     "hotkeys input pad-chords"));
  const Connected second = connect(table, request_for("chord-two", GRACEUI_VERSION_PACK(0, 2, 14), "Chord two",
                                                      "hotkeys pad-chords"));
  GraceUiConnectRequest older_request = request_for("chord-older", GRACEUI_VERSION_PACK(0, 2, 2), "Chord older", "hotkeys");
  older_request.abi_minor = 2;  // built before ABI 1.3
  const Connected older = connect(table, older_request);
  const auto chord_of = [&](GraceUiConsumer mod, const char* action, std::uint32_t key, std::uint32_t pad_chord,
                            std::uint32_t& id) {
    GraceUiHotkeyDesc desc{};
    desc.size = sizeof desc;
    desc.key = key;
    desc.pad_chord = pad_chord;
    desc.action = action;
    desc.action_length = std::strlen(action);
    desc.name = action;
    desc.name_length = std::strlen(action);
    return table->register_hotkey(mod, &desc, &id);
  };
  std::uint32_t talk = 0;
  std::uint32_t unused = 0;
  std::uint32_t shout = 0;
  const GraceUiStatus talk_status = chord_of(first.answer.consumer, "talk", 0, GRACEUI_PAD_CHORD(kUp, kView), talk);
  const GraceUiStatus mask_status = chord_of(first.answer.consumer, "mask", 0, kUp | kView, unused);
  const GraceUiStatus same_status = chord_of(first.answer.consumer, "same", 0, GRACEUI_PAD_CHORD(kUp, kUp), unused);
  const GraceUiStatus taken_status =
      chord_of(second.answer.consumer, "shout", VK_F9, GRACEUI_PAD_CHORD(kUp, kView), shout);
  const GraceUiStatus older_status =
      chord_of(older.answer.consumer, "talk", 0, GRACEUI_PAD_CHORD(kUp, kView), unused);
  const bool taken_logged = grace.logged("hotkey shout is unbound: its default chord is taken");
  const bool cap = table->has_cap(GRACEUI_CAP_PAD_CHORDS, std::strlen(GRACEUI_CAP_PAD_CHORDS)) != 0;
  check(first.status == GRACEUI_OK && second.status == GRACEUI_OK && older.status == GRACEUI_OK && cap &&
            table->abi_minor >= 3u && talk_status == GRACEUI_OK && talk != 0 && mask_status == GRACEUI_REFUSED &&
            same_status == GRACEUI_REFUSED && taken_status == GRACEUI_UNBOUND && shout != 0 && taken_logged &&
            older_status == GRACEUI_NOT_YET,
        "chords: registered",
        "has_cap(\"%s\") %s, the table's minor %u; D-pad Up then View %s; as a mask %s; one button twice %s; the same "
        "chord by a second mod, with F9, %s (logged: %s); from a mod built before ABI 1.3 %s",
        GRACEUI_CAP_PAD_CHORDS, cap ? "yes" : "NO", table->abi_minor, status_name(talk_status),
        status_name(mask_status), status_name(same_status), status_name(taken_status), taken_logged ? "yes" : "NO",
        status_name(older_status));

  start_host(game, grace);
  frames(game, 30);
  until_atlas(game, grace);

  // The Hotkeys page's list (D206): the menu's key first, then each hotkey with its key, chord and state; the older
  // mod's chord was never registered.
  using grace::protocol::Json;
  const auto row_of = [](const Json& body, const char* mod) {
    if (body.is_object() && body.contains("rows") && body["rows"].is_array()) {
      for (const Json& row : body["rows"]) {
        if (row.is_object() && row.value("mod", std::string()) == mod) {
          return row;
        }
      }
    }
    return Json();
  };
  const Json listed = own_topic(game, grace, "graceui.hotkeys", [&](const Json& body) {
    return !row_of(body, "Chord two").is_null();
  });
  const Json menu_row = row_of(listed, "GraceUI");
  const Json talk_row = row_of(listed, "Chord mod");
  const Json shout_row = row_of(listed, "Chord two");
  const auto cell = [](const Json& row, const char* column) {
    return row.is_object() ? row.value(column, std::string()) : std::string();
  };
  check(cell(menu_row, "key") == "F5" && cell(talk_row, "action") == "talk" && cell(talk_row, "key") == "No key" &&
            cell(talk_row, "chord") == "D-pad Up, then View" && cell(talk_row, "state") == "Bound" &&
            cell(shout_row, "key") == "F9" &&
            cell(shout_row, "state") == "Chord unbound: Chord mod took its chord first" &&
            row_of(listed, "Chord older").is_null() && listed.contains("rows") && listed["rows"].size() == 3,
        "chords: the Hotkeys page",
        "graceui.hotkeys lists %zu row(s): the menu's key %s; talk: %s, %s, %s; shout: %s, %s",
        listed.contains("rows") ? listed["rows"].size() : std::size_t{0}, cell(menu_row, "key").c_str(),
        cell(talk_row, "key").c_str(), cell(talk_row, "chord").c_str(), cell(talk_row, "state").c_str(),
        cell(shout_row, "key").c_str(), cell(shout_row, "state").c_str());

  struct Seen {
    std::uint32_t buttons = 0;
    std::uint32_t packet = 0;
  };
  const auto read = [&](std::uint32_t buttons) {
    game.frame(kInputColour, PresentWith::present);  // the overlay stays ready: a request would be granted
    Seen seen;
    seen.packet = kPacket;
    seen.buttons = grace.pad_read(0, buttons, &seen.packet);
    return seen;
  };
  const auto presses = [&](GraceUiConsumer mod, std::uint32_t id) {
    std::uint32_t count = 0;
    GraceUiEvent event{};
    event.size = sizeof event;
    for (int i = 0; i < 16 && table->poll(mod, &event) == GRACEUI_OK && event.kind != GRACEUI_EVENT_NONE; ++i) {
      if (event.kind == GRACEUI_EVENT_HOTKEY && event.hotkey_id == id) {
        count += event.count;
      }
    }
    return count;
  };
  const auto wait = [&](DWORD ms) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < ms) {
      game.frame(kInputColour, PresentWith::present);
      Sleep(10);
    }
  };

  // 1. The anchor reaches the game; a key 250 ms later is held back; held 0.5 s more, the hotkey fires once and both
  // are hidden until released, the packet number changed so that the game reads it.
  read(0);
  const Seen anchor = read(kUp);
  wait(250);
  const Seen held = read(kUp | kView);
  wait(150);
  const Seen still = read(kUp | kView);
  wait(400);
  const Seen fired = read(kUp | kView);
  const std::uint32_t fired_presses = presses(first.answer.consumer, talk);
  const Seen long_hold = read(kUp | kView);
  const Seen view_up = read(kUp);
  const Seen both_up = read(0);
  const Seen fresh = read(kUp);
  read(0);
  check(anchor.buttons == kUp && held.buttons == kUp && still.buttons == kUp && fired.buttons == 0 &&
            fired.packet != still.packet && fired_presses == 1 && long_hold.buttons == 0 && view_up.buttons == 0 &&
            both_up.buttons == 0 && fresh.buttons == kUp,
        "chords: complete",
        "the game read: the anchor %#x; the key 250 ms on %#x, then %#x; at 0.65 s %#x (packet %u, then %u), the "
        "hotkey %u time(s); held on %#x, View up %#x, both up %#x, Up afresh %#x",
        anchor.buttons, held.buttons, still.buttons, fired.buttons, still.packet, fired.packet, fired_presses,
        long_hold.buttons, view_up.buttons, both_up.buttons, fresh.buttons);

  // 2. A key released before 0.4 s: one tap, of two reads and 50 ms, the packet number changing at each step.
  const Seen up_again = read(kUp);
  wait(250);
  const Seen held_again = read(kUp | kView);
  wait(60);
  const Seen tap = read(kUp);
  const Seen tap_two = read(kUp);
  wait(80);
  const Seen tap_end = read(kUp);
  const Seen after_tap = read(kUp);
  read(0);
  const std::uint32_t tap_presses = presses(first.answer.consumer, talk);
  check(up_again.buttons == kUp && held_again.buttons == kUp && tap.buttons == (kUp | kView) &&
            tap.packet != held_again.packet && tap_two.buttons == (kUp | kView) && tap_end.buttons == kUp &&
            tap_end.packet != tap.packet && after_tap.buttons == kUp && tap_presses == 0,
        "chords: a tap",
        "held back %#x; released at 60 ms: the game read %#x (packet %u, then %u), %#x, and 80 ms on %#x (packet %u), "
        "then %#x; the hotkey %u time(s)",
        held_again.buttons, tap.buttons, held_again.packet, tap.packet, tap_two.buttons, tap_end.buttons,
        tap_end.packet, after_tap.buttons, tap_presses);

  // 3. In danger a request would be answered not_now: the key reaches the game at once, and nothing fires.
  grace.set_world(GRACEUI_TEST_WORLD_DANGER);
  read(kUp);
  wait(250);
  const Seen danger = read(kUp | kView);
  wait(500);
  const Seen danger_later = read(kUp | kView);
  read(0);
  grace.set_world(0);
  const std::uint32_t danger_presses = presses(first.answer.consumer, talk);

  // 4. A key within 150 ms of the anchor is not a chord.
  read(kUp);
  wait(40);
  const Seen quick = read(kUp | kView);
  wait(500);
  const Seen quick_later = read(kUp | kView);
  read(0);
  const std::uint32_t quick_presses = presses(first.answer.consumer, talk);

  // 5. While a holder has input it has the pad: the game sees it at rest, and nothing fires.
  const bool granted = take(grace, game, 1, kPanelKind);
  read(kUp);
  wait(250);
  const Seen holding = read(kUp | kView);
  wait(500);
  const Seen holding_later = read(kUp | kView);
  read(0);
  grace.release_input(1);
  frames(game, 2);
  read(0);
  wait(300);  // past the close's window, in which a press held is taken for the closing one (invariant 8)
  read(0);
  const std::uint32_t holder_presses = presses(first.answer.consumer, talk);
  check(danger.buttons == (kUp | kView) && danger_later.buttons == (kUp | kView) && danger_presses == 0 &&
            quick.buttons == (kUp | kView) && quick_later.buttons == (kUp | kView) && quick_presses == 0 && granted &&
            holding.buttons == 0 && holding_later.buttons == 0 && holder_presses == 0,
        "chords: standing aside",
        "in danger the game read %#x, then %#x, the hotkey %u time(s); a key 40 ms after the anchor %#x, then %#x, "
        "%u time(s); with a holder (%s) %#x, then %#x, %u time(s)",
        danger.buttons, danger_later.buttons, danger_presses, quick.buttons, quick_later.buttons, quick_presses,
        granted ? "granted" : "NOT GRANTED", holding.buttons, holding_later.buttons, holder_presses);

  // 6. A mod's chords go with it: the key then reaches the game. The second mod's chord stays unbound until the player
  // binds it (GraceUI 0.6's Hotkeys page), so nothing fires.
  table->disconnect(first.answer.consumer);
  read(kUp);
  wait(250);
  const Seen gone = read(kUp | kView);
  wait(500);
  read(0);
  const std::uint32_t second_presses = presses(second.answer.consumer, shout);
  check(gone.buttons == (kUp | kView) && second_presses == 0, "chords: a mod's go with it",
        "after the first mod's disconnect, the key 250 ms after the anchor reached the game (%#x); the second's "
        "unbound chord fired %u time(s)",
        gone.buttons, second_presses);
}

void run_display(const std::wstring& dll) {
  Game game(false, 1280, 720);
  const HWND window = game.window();
  cursor_into(window);
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const GraceUiApi1* table = grace.table();
  if (table == nullptr) {
    fatal("no table");
  }
  // 1. Connect before the game's swap chain is bound: ui.scale and its default, no size yet. An answer of 1.0's size
  // is written no further than 1.0 reached.
  const Connected first = connect(table, request_for("display-mod", GRACEUI_VERSION_PACK(0, 2, 2), "Display mod",
                                                     "faces display"));
  GraceUiConnectRequest older_request = request_for("older-mod", GRACEUI_VERSION_PACK(0, 1, 0), "Older mod", "faces");
  GraceUiConnectAnswer older_answer{};
  older_answer.ui_scale = 7.0f;  // past a 1.0 answer's end: left alone
  older_answer.size = 32;
  const GraceUiStatus older_status = table->connect(&older_request, &older_answer);
  check(first.status == GRACEUI_OK && close_to(first.answer.ui_scale, 1.25f) &&
            close_to(first.answer.ui_scale_default, 1.25f) && first.answer.display_width == 0 &&
            first.answer.display_height == 0 && table->has_cap(GRACEUI_CAP_DISPLAY, std::strlen(GRACEUI_CAP_DISPLAY)) &&
            older_status == GRACEUI_OK && older_answer.ui_scale == 7.0f && GRACEUI_ABI1_MINOR >= 2u,
        "display: at connect",
        "ui.scale %.2f, its default %.2f, the size %ux%u (no swap chain bound yet); has_cap(\"display\") %s; a 1.0 "
        "answer's bytes past its end %s",
        first.answer.ui_scale, first.answer.ui_scale_default, first.answer.display_width,
        first.answer.display_height, table->has_cap(GRACEUI_CAP_DISPLAY, 7) != 0 ? "yes" : "NO",
        older_answer.ui_scale == 7.0f ? "untouched" : "WRITTEN");

  // 2. Before warm-up: a scaled face of 16 px and a plain one; nothing to poll yet.
  const std::vector<unsigned char> noto = read_file(folder_of_this_program() + L"fonts\\NotoSans-Regular.ttf");
  std::uint32_t scaled_id = 0;
  std::uint32_t plain_id = 0;
  std::uint32_t unknown_id = 0;
  const GraceUiStatus scaled_status = face_of(table, first.answer.consumer, noto, 16, GRACEUI_FACE_SCALED, scaled_id);
  const GraceUiStatus plain_status = face_of(table, first.answer.consumer, noto, 16, 0, plain_id);
  const GraceUiStatus unknown_flag = face_of(table, older_answer.consumer, noto, 16, 2u, unknown_id);
  const std::vector<GraceUiEvent> early = displays_of(table, first.answer.consumer);
  start_host(game, grace);
  for (int i = 0; i < 150 && grace.state().atlas_generation == 0; ++i) {
    frames(game, 1);
    Sleep(20);
  }
  until_atlas(game, grace);

  // 3. After warm-up: one display event, 1280 by 720 at the default (content 720 / 1080 = 0.667), the scaled face baked
  // at 16 x 0.667 to a whole pixel (11) and the plain one at 16; a second poll finds nothing; a poll of 1.1's size never gets it.
  const std::vector<GraceUiEvent> warm = displays_of(table, first.answer.consumer);
  const std::vector<GraceUiEvent> again = displays_of(table, first.answer.consumer);
  const std::vector<GraceUiEvent> older_events = displays_of(table, older_answer.consumer, 104);
  GraceUiGlyphTable scaled_table{};
  scaled_table.size = sizeof scaled_table;
  GraceUiGlyphTable plain_table{};
  plain_table.size = sizeof plain_table;
  const GraceUiStatus scaled_glyphs = table->glyph_table(first.answer.consumer, scaled_id, &scaled_table);
  const GraceUiStatus plain_glyphs = table->glyph_table(first.answer.consumer, plain_id, &plain_table);
  const bool warm_ok = warm.size() == 1 && warm[0].display_width == 1280 && warm[0].display_height == 720 &&
                       close_to(warm[0].ui_scale, 1.25f) && close_to(warm[0].ui_scale_default, 1.25f) &&
                       close_to(warm[0].scale, 720.0f / 1080.0f * 1.25f) && close_to(warm[0].content_scale, 720.0f / 1080.0f) &&
                       close_to(warm[0].faces_scale, 720.0f / 1080.0f);
  check(scaled_status == GRACEUI_OK && plain_status == GRACEUI_OK && unknown_flag == GRACEUI_REFUSED && early.empty() &&
            warm_ok && again.empty() && older_events.empty() && scaled_glyphs == GRACEUI_OK &&
            plain_glyphs == GRACEUI_OK && std::fabs(scaled_table.pixel_height - std::round(16.0f * 720.0f / 1080.0f)) < 0.05f &&
            std::fabs(plain_table.pixel_height - 16.0f) < 0.05f,
        "display: after warm-up",
        "faces: scaled %s, plain %s, an unknown flag %s; before warm-up %s; after it: %s; again: %s; at 1.1's size: %s; "
        "the scaled face baked at %.2f px, the plain one at %.2f",
        status_name(scaled_status), status_name(plain_status), status_name(unknown_flag), display_text(early).c_str(),
        display_text(warm).c_str(), display_text(again).c_str(), display_text(older_events).c_str(),
        scaled_table.pixel_height, plain_table.pixel_height);

  // 4. ui.scale changed in the menu (1.25 to 1.30): one event, the faces' scale as they were baked.
  press(window, VK_F5, kScanF5);
  GraceUiTestMenu menu = menu_after(game, grace, 0);
  press(window, VK_RIGHT, 0x4D);
  menu = menu_after(game, grace, menu.lists);
  press(window, VK_F5, kScanF5);
  frames(game, 3);
  std::vector<GraceUiEvent> scaled;
  for (int i = 0; i < 50 && scaled.empty(); ++i) {
    scaled = displays_of(table, first.answer.consumer);
    if (scaled.empty()) {
      frames(game, 1);
      Sleep(20);
    }
  }
  check(scaled.size() == 1 && close_to(scaled[0].ui_scale, 1.3f) && close_to(scaled[0].scale, 720.0f / 1080.0f * 1.3f) &&
            close_to(scaled[0].content_scale, 720.0f / 1080.0f * 1.3f / 1.25f) &&
            close_to(scaled[0].faces_scale, 720.0f / 1080.0f),
        "display: a new ui.scale", "→ on Menu size: %s", display_text(scaled).c_str());

  // 5. The game resizes its swap chain: one event with the new size.
  game.resize(1600, 900);
  frames(game, 3);
  std::vector<GraceUiEvent> resized;
  for (int i = 0; i < 50 && resized.empty(); ++i) {
    resized = displays_of(table, first.answer.consumer);
    if (resized.empty()) {
      frames(game, 1);
      Sleep(20);
    }
  }
  check(resized.size() == 1 && resized[0].display_width == 1600 && resized[0].display_height == 900 &&
            close_to(resized[0].scale, 900.0f / 1080.0f * 1.3f) &&
            close_to(resized[0].content_scale, 900.0f / 1080.0f * 1.3f / 1.25f),
        "display: a resize", "to 1600x900: %s", display_text(resized).c_str());

  // 6. A mod that connects now: the size in its answer, and the display at its first poll (no scaled face: 0).
  const Connected late = connect(table, request_for("late-mod", GRACEUI_VERSION_PACK(0, 2, 2), "Late mod", "display"));
  const std::vector<GraceUiEvent> late_events = displays_of(table, late.answer.consumer);
  check(late.status == GRACEUI_OK && late.answer.display_width == 1600 && late.answer.display_height == 900 &&
            close_to(late.answer.ui_scale, 1.3f) && late_events.size() == 1 && late_events[0].faces_scale == 0.0f &&
            late_events[0].display_width == 1600,
        "display: a mod that connects later",
        "its answer: %ux%u, ui.scale %.2f; its first poll: %s", late.answer.display_width, late.answer.display_height,
        late.answer.ui_scale, display_text(late_events).c_str());
  DeleteFileW(settings_file_of(dll).c_str());  // the menu kept ui.scale: the next scenario starts from the defaults
}

void run_absent() {
  GraceUiSysFound found{};
  found.size = sizeof found;
  const ULONGLONG start = GetTickCount64();
  const GraceUiApi1* api = graceui_sys_find(GRACEUI_SYS_LOAD | GRACEUI_SYS_STOP_IF_ABSENT, 30000, &found);
  const ULONGLONG took = GetTickCount64() - start;
  GraceUiSysFound without{};
  without.size = sizeof without;
  const ULONGLONG start_without = GetTickCount64();
  graceui_sys_find(GRACEUI_SYS_LOAD, 700, &without);  // without the flag: it looks until its time is up
  const ULONGLONG took_without = GetTickCount64() - start_without;
  check(api == nullptr && found.installed == 0 && found.attempts == 1 && took < 1000 && without.attempts >= 3 &&
            took_without >= 650,
        "not installed",
        "with GraceUI absent: %s, installed %u, after %u look(s) and %llu ms; without the flag, %u looks in %llu ms",
        api == nullptr ? "nothing found" : "A TABLE", found.installed, found.attempts,
        static_cast<unsigned long long>(took), without.attempts, static_cast<unsigned long long>(took_without));
}

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
int run_child(const std::wstring& dll, const wchar_t* scenario, std::wstring program) {
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

// ---- S10.4.d: two example mods at once, each way round (D156): hello-notice and hello-debug-view, the one loaded
// before GraceUI and the other after it, then the other way round in a second process. The debug view is EldenNet's
// banner and F11 view in miniature: a banner notice under its own key, and a panel on its own headless Dear ImGui that
// GraceUI's hotkey F11 opens and closes, its presses taken through poll.

constexpr Colour kExamplesColour{28, 34, 30};
constexpr Colour kDebugGround{HELLO_DEBUG_GROUND_R, HELLO_DEBUG_GROUND_G, HELLO_DEBUG_GROUND_B};

std::wstring debug_view_path() { return folder_of_this_program() + L"hello-debug-view.dll"; }

// The debug view's connection and banner as GraceUI holds them ("" if it has none).
bool debug_view_connected(const GraceUi& grace, std::string& banner) {
  for (const GraceUiTestConsumer& consumer : grace.consumers()) {
    if (std::string_view(consumer.id) != HELLO_DEBUG_ID) {
      continue;
    }
    for (const GraceUiTestNotice& notice : grace.notices()) {
      if (notice.owner == consumer.owner) {
        banner = notice.text;
        return true;
      }
    }
  }
  return false;
}

void run_examples(const std::wstring& dll, bool notice_first) {
  Game game(false);
  const HWND window = game.window();
  const char* first_name = notice_first ? "hello-notice" : HELLO_DEBUG_ID;
  const char* second_name = notice_first ? HELLO_DEBUG_ID : "hello-notice";
  const std::wstring first_path = notice_first ? example_path() : debug_view_path();
  const std::wstring second_path = notice_first ? debug_view_path() : example_path();
  const auto connected = [&](const GraceUi& grace, const char* name) {
    std::string text;
    std::uint32_t owner = 0;
    return std::string_view(name) == "hello-notice" ? example_connected(grace, text, owner)
                                                    : debug_view_connected(grace, text);
  };

  // The first, loaded before GraceUI: it looks for GraceUI every 250 ms and finds it once it is loaded, and its request
  // starts the host. The second, loaded once the hooks are in, finds GraceUI at its first look.
  if (LoadLibraryW(first_path.c_str()) == nullptr) {
    fatal("the first example did not load");
  }
  present_until(&game, kExamplesColour, 600, [] { return false; });
  GraceUi grace;
  if (!grace.load(dll)) {
    fatal("graceui.dll did not load, or lacks the test exports");
  }
  const bool first_seen = present_until(&game, kExamplesColour, 10000, [&] { return connected(grace, first_name); });
  start_host(game, grace);
  if (LoadLibraryW(second_path.c_str()) == nullptr) {
    fatal("the second example did not load");
  }
  const bool second_seen = present_until(&game, kExamplesColour, 10000, [&] { return connected(grace, second_name); });
  std::string banner;
  std::string notice_text;
  std::uint32_t notice_owner = 0;
  debug_view_connected(grace, banner);
  example_connected(grace, notice_text, notice_owner);
  const std::vector<GraceUiTestConsumer> consumers = grace.consumers();
  check(first_seen && second_seen && consumers.size() == 2 && banner == HELLO_DEBUG_BANNER_CLOSED &&
            notice_text.find("GraceUI " + version_words(kBuildVersion)) != std::string::npos,
        "two examples",
        "%s, loaded before GraceUI, %s; %s, loaded after the hooks, %s; %zu mods connected; the debug view's banner "
        "\"%s\"; hello-notice's notice \"%s\"",
        first_name, first_seen ? "connected" : "DID NOT CONNECT", second_name,
        second_seen ? "connected" : "DID NOT CONNECT", consumers.size(), banner.c_str(), notice_text.c_str());

  // Both notices are drawn, and nothing else: the debug view's panel stays hidden until F11.
  Frame frame;
  std::string why;
  bool both = false;
  for (int i = 0; i < 60 && !both; ++i) {
    frame = game.frame_and_read(kExamplesColour, PresentWith::present);
    both = notices_drawn(frame, kExamplesColour, {GRACEUI_TONE_NORMAL, GRACEUI_TONE_NORMAL}, false, why);
  }
  check(both, "both notices", "the banner and hello-notice's notice, the view still closed: %s", why.c_str());

  // F11 opens the view: GraceUI's hotkey, the press kept from the game; the mod takes it through poll, shows its panel,
  // anchored at the bottom left, and replaces its banner's text in place.
  cursor_into(window);
  GameInput reads;
  reads.open(window);
  reads.keys();
  g_seen = Seen{};
  inject_key(VK_F11, 0x57, true);
  settle();
  const bool f11_read = (reads.keys()[DIK_F11] & 0x80) != 0;
  inject_key(VK_F11, 0x57, false);
  settle();
  bool opened = false;
  for (int i = 0; i < 100 && !opened; ++i) {
    frame = game.frame_and_read(kExamplesColour, PresentWith::present);
    debug_view_connected(grace, banner);
    opened = frame.height > HELLO_DEBUG_Y + 20 &&
             close_to(frame.at(HELLO_DEBUG_X + 240, frame.height - HELLO_DEBUG_Y - 10), kDebugGround, 3) &&
             banner == HELLO_DEBUG_BANNER_OPEN;
    Sleep(10);
  }
  const UINT top = frame.height - HELLO_DEBUG_Y - HELLO_DEBUG_HEIGHT;
  const int ground = count_near(frame, HELLO_DEBUG_X, top, HELLO_DEBUG_X + HELLO_DEBUG_WIDTH,
                                frame.height - HELLO_DEBUG_Y, kDebugGround, 3);
  const int ink = count_near(frame, HELLO_DEBUG_X, top, HELLO_DEBUG_X + HELLO_DEBUG_WIDTH,
                             frame.height - HELLO_DEBUG_Y, Colour{255, 255, 255}, 40);
  const bool outside_clear = close_to(frame.at(HELLO_DEBUG_X - 2, top + 10), kExamplesColour, 0) &&
                             close_to(frame.at(HELLO_DEBUG_X + 10, top - 2), kExamplesColour, 0) &&
                             close_to(frame.at(HELLO_DEBUG_X + HELLO_DEBUG_WIDTH + 1, top + 10), kExamplesColour, 0);
  const std::size_t boxes_open = notice_boxes(frame).size();
  const int f11_messages = g_seen.key_down[VK_F11] + g_seen.key_up[VK_F11];
  check(opened && ground >= HELLO_DEBUG_WIDTH * HELLO_DEBUG_HEIGHT / 2 && ink >= 100 && outside_clear &&
            boxes_open == 2 && !f11_read && f11_messages == 0,
        "debug view opened",
        "F11 injected: the game's DirectInput %s it, its messages reaching the game %d; the panel %s at the bottom "
        "left (%d pixels of its ground, %d of its text's ink, the edges outside it %s); the banner \"%s\"; %zu "
        "notices drawn",
        f11_read ? "SAW" : "did not see", f11_messages, opened ? "drawn" : "NOT DRAWN", ground, ink,
        outside_clear ? "clear" : "NOT CLEAR", banner.c_str(), boxes_open);

  // F11 again closes it: the panel gone, the banner as it was, both notices drawn and nothing else.
  g_seen = Seen{};
  inject_key(VK_F11, 0x57, true);
  settle();
  inject_key(VK_F11, 0x57, false);
  settle();
  bool closed = false;
  for (int i = 0; i < 100 && !closed; ++i) {
    frame = game.frame_and_read(kExamplesColour, PresentWith::present);
    debug_view_connected(grace, banner);
    closed = banner == HELLO_DEBUG_BANNER_CLOSED &&
             notices_drawn(frame, kExamplesColour, {GRACEUI_TONE_NORMAL, GRACEUI_TONE_NORMAL}, false, why);
    Sleep(10);
  }
  check(closed && g_seen.key_down[VK_F11] == 0, "debug view closed",
        "F11 again: the banner \"%s\"; %s; F11's messages reaching the game %d", banner.c_str(), why.c_str(),
        g_seen.key_down[VK_F11]);
  grace.print_log();
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
  } else if (scenario == L"panels") {
    std::printf("-- scenario: panels, draw lists, textures, faces, input through ABI 1 and providers\n");
    run_panels(dll);
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
  } else if (scenario == L"decoy-window") {
    std::printf("-- scenario: a larger window found first, then the swap chain's\n");
    run_decoy_window(dll);
  } else if (scenario == L"atlas-full") {
    std::printf("-- scenario: the shared atlas runs out of room\n");
    run_atlas_full(dll);
  } else if (scenario == L"slow-first") {
    std::printf("-- scenario: a request while the first is still running\n");
    run_slow_first(dll);
  } else if (scenario == L"device-change") {
    std::printf("-- scenario: the game moves to a new device\n");
    run_device_change(dll);
  } else if (scenario == L"menu") {
    std::printf("-- scenario: the in-game menu, GraceUI's own space drawn by the host on F5 (D167)\n");
    run_menu(dll);
  } else if (scenario == L"pages") {
    std::printf("-- scenario: a mod's space across the ABI and the browser view's server (D169)\n");
    run_pages(dll);
  } else if (scenario == L"page-shots") {
    std::printf("-- scenario: a space's pages in game, for a look review (GRACE_SPACE_DIR, GRACE_SHOTS)\n");
    run_page_shots(dll);
  } else if (scenario == L"anti-cheat-flag") {
    std::printf("-- scenario: a provider's anti-cheat flag closes the browser view for the session (§7.2 item 1)\n");
    run_anti_cheat_later(dll, false);
  } else if (scenario == L"anti-cheat-recheck") {
    std::printf("-- scenario: the module re-check closes the browser view for the session (§7.2 item 1)\n");
    run_anti_cheat_later(dll, true);
  } else if (scenario == L"menu-away") {
    std::printf("-- scenario: the menu across an Alt+Tab, and Tab in it (G40, G41)\n");
    run_menu_away(dll);
  } else if (scenario == L"menu-recentred") {
    std::printf("-- scenario: the menu's pointer in a game that recentres its cursor (D232)\n");
    run_menu_recentred(dll);
  } else if (scenario == L"menu-mouse") {
    std::printf("-- scenario: the in-game menu by mouse (§5.5)\n");
    run_menu_mouse(dll);
  } else if (scenario == L"typing") {
    std::printf("-- scenario: GraceUI's own text field in game (G38 (a), D57)\n");
    run_typing(dll);
  } else if (scenario == L"master") {
    std::printf("-- scenario: master and detail in game (G38, D215)\n");
    run_master(dll);
  } else if (scenario == L"slow") {
    std::printf("-- scenario: a slow command in game (G38, D216)\n");
    run_slow(dll);
  } else if (scenario == L"editor") {
    std::printf("-- scenario: the editor block in game (G38, D217)\n");
    run_editor(dll);
  } else if (scenario == L"menu-space") {
    std::printf("-- scenario: the in-game menu on mods' spaces, with GraceUI's at the rail's foot (D171)\n");
    run_menu_space(dll);
  } else if (scenario == L"spike") {
    std::printf("-- scenario: the spike observer and the stand-in overlay, loaded before GraceUI (D175)\n");
    run_spike(dll, true);
  } else if (scenario == L"spike-after") {
    std::printf("-- scenario: the spike observer and the stand-in overlay, loaded after GraceUI's hooks (D175)\n");
    run_spike(dll, false);
  } else if (scenario == L"holder-events") {
    std::printf("-- scenario: a holder's events for its mod through ABI 1.1 (D176)\n");
    run_holder_events(dll);
  } else if (scenario == L"settings") {
    std::printf("-- scenario: GraceUI's settings kept between sessions (D179)\n");
    run_settings(dll);
  } else if (scenario == L"settings-write") {
    std::printf("-- scenario: the menu changes GraceUI's settings, which are kept (D179)\n");
    run_settings_write(dll);
  } else if (scenario == L"settings-read") {
    std::printf("-- scenario: GraceUI starts from the settings an earlier session kept (D179)\n");
    run_settings_read(dll);
  } else if (scenario == L"settings-bad") {
    std::printf("-- scenario: a settings file that is not GraceUI's (D179)\n");
    run_settings_bad(dll);
  } else if (scenario == L"free-port") {
    std::printf("-- scenario: a test build's free port for the browser view (D234)\n");
    run_free_port(dll);
  } else if (scenario == L"heap-guard") {
    std::printf("-- scenario: the test build's heap guard (D235)\n");
    run_heap_guard(dll);
    run_heap_guard_observer(dll);
  } else if (scenario == L"heap-guard-spike") {
    run_heap_guard_spike(dll);
  } else if (scenario == L"heap-guard-seen") {
    run_heap_guard_child(dll, true);
  } else if (scenario == L"heap-guard-end") {
    run_heap_guard_child(dll, false);
  } else if (scenario == L"pairing") {
    std::printf("-- scenario: another device at home, paired with a code shown in game (G9)\n");
    run_pairing(dll);
  } else if (scenario == L"pairing-write") {
    std::printf("-- scenario: Pair a device, a device's rights, Forget, and the switch (G9)\n");
    run_pairing_write(dll);
  } else if (scenario == L"pairing-read") {
    std::printf("-- scenario: the next session keeps the paired device (G9)\n");
    run_pairing_read(dll);
  } else if (scenario == L"quiet") {
    std::printf("-- scenario: a mod's notices quieted from the Mods page (G32, D181)\n");
    run_quiet(dll);
  } else if (scenario == L"quiet-write") {
    std::printf("-- scenario: the Mods page's switch quiets a mod's notices, which are kept (D181)\n");
    run_quiet_write(dll);
  } else if (scenario == L"quiet-read") {
    std::printf("-- scenario: the next session keeps the switch (D181)\n");
    run_quiet_read(dll);
  } else if (scenario == L"bindings") {
    std::printf("-- scenario: hotkeys rebound in the menu and kept, in two processes of their own (G37, D208 to D210)\n");
    run_bindings(dll);
  } else if (scenario == L"bindings-write") {
    std::printf("-- scenario: a key captured, a taken one moved, the menu's key moved, all kept (D208)\n");
    run_bindings_write(dll);
  } else if (scenario == L"bindings-read") {
    std::printf("-- scenario: the next session keeps them; every hotkey reset (D208)\n");
    run_bindings_read(dll);
  } else if (scenario == L"wake") {
    std::printf("-- scenario: a wait handle for each mod's events through ABI 1.5 (D212)\n");
    run_wake(dll);
  } else if (scenario == L"chords") {
    std::printf("-- scenario: pad chords for mods' hotkeys through ABI 1.3 (D204)\n");
    run_chords(dll);
  } else if (scenario == L"display") {
    std::printf("-- scenario: the display at connect and in its event, and scaled faces (D180)\n");
    run_display(dll);
  } else if (scenario == L"absent") {
    std::printf("-- scenario: graceui-sys with GraceUI not installed\n");
    run_absent();
  } else if (scenario == L"examples") {
    std::printf("-- scenario: hello-notice before GraceUI, hello-debug-view after\n");
    run_examples(dll, true);
  } else if (scenario == L"examples-reversed") {
    std::printf("-- scenario: hello-debug-view before GraceUI, hello-notice after\n");
    run_examples(dll, false);
  } else if (scenario == L"long-name") {
    std::printf("-- scenario: a second copy with a long name\n");
    run_long_name(dll);
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
      for (const wchar_t* child_scenario :
           {L"two-queues", L"input", L"input-ansi", L"abi", L"panels", L"copies", L"found-by-scan", L"anti-cheat",
            L"anti-cheat-parent", L"decoy-window", L"atlas-full", L"slow-first", L"long-name", L"device-change",
            L"examples", L"examples-reversed", L"absent", L"menu", L"menu-mouse", L"menu-recentred", L"menu-away", L"menu-space", L"typing", L"master", L"slow", L"editor", L"spike", L"spike-after",
            L"holder-events", L"settings", L"display", L"quiet", L"chords", L"bindings", L"wake", L"heap-guard"}) {
        run(child_scenario, dll, this_program());
      }
      // pages runs GraceUI from the build's GraceUI folder, whose graceui.toml lists the web page the browser loads.
      run(L"pages", folder_of_this_program() + L"..\\GraceUI\\graceui.dll", this_program());
      run(L"anti-cheat-flag", folder_of_this_program() + L"..\\GraceUI\\graceui.dll", this_program());
      run(L"anti-cheat-recheck", folder_of_this_program() + L"..\\GraceUI\\graceui.dll", this_program());
      run(L"pairing", folder_of_this_program() + L"..\\GraceUI\\graceui.dll", this_program());  // G9: the app's page
      run(L"free-port", folder_of_this_program() + L"..\\GraceUI\\graceui.dll", this_program());  // D234
      DeleteFileW(settings_file_of(folder_of_this_program() + L"..\\GraceUI\\graceui.dll").c_str());  // D179: none left
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
