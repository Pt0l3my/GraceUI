// hooks.cpp: GraceUI's hooks (D102), the one place that creates any (invariant 16). The init thread (D71) waits for a
// window of the game's, reads the render half's five targets from the vtables of a short-lived device, queue and swap
// chain of its own (hooks_vtable.c), installs them with MinHook once, and exits; they are never removed (D107). Then
// the detours: Present and Present1 drive the render path (draw.cpp), ResizeBuffers and ResizeBuffers1 keep it off the
// buffers they replace, and ExecuteCommandLists notes which queues execute, to find the present queue (D116).
//
// SAFETY: every function here that the game reaches is a detour: noexcept, with its own work in a catch-all, and the
// original called with the arguments it was given, exactly once. The pointers the game hands the detours are used only
// for the length of the call, except two kinds, held as below: a bound swap chain, which its tag (SetPrivateDataInterface)
// tells us has ended before its memory can be reused (D118), and a queue, which is used only while it is being executed
// on, or with a reference of our own. The casts below turn function addresses into the void pointers MinHook takes.
#include "hooks.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "counters.h"
#include "draw.h"
#include "hooks_vtable.h"
#include "log.h"
#include "state.h"

namespace grace::host::hooks {
namespace {

// ---- The render half's hooks: D102's published list, the first five. The input half adds the game's window procedure,
// DirectInput's GetDeviceState and XInput's XInputGetState to this table, and nowhere else.
// Their order is hooks_vtable.h's (GRACE_HOST_HOOK_*), which the tables in install() follow.
constexpr int kHookCount = GRACE_HOST_RENDER_HOOKS;
constexpr const char* kHookNames[kHookCount] = {"IDXGISwapChain::Present", "IDXGISwapChain1::Present1",
                                                "IDXGISwapChain::ResizeBuffers", "IDXGISwapChain3::ResizeBuffers1",
                                                "ID3D12CommandQueue::ExecuteCommandLists"};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                                     const UINT*, IUnknown* const*);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

// The originals (MinHook's trampolines). MH_CreateHook writes each before MH_EnableHook makes any detour reachable.
constinit PresentFn g_present = nullptr;
constinit Present1Fn g_present1 = nullptr;
constinit ResizeBuffersFn g_resize_buffers = nullptr;
constinit ResizeBuffers1Fn g_resize_buffers1 = nullptr;
constinit ExecuteCommandListsFn g_execute_command_lists = nullptr;

// Per thread: how deep in Present calls it is (DXGI may implement one present by calling the other, and only the
// outermost draws) and which swap chain's original Present is running on it (D116's third way).
constinit thread_local int t_present_depth = 0;
constinit thread_local IDXGISwapChain* t_original_present = nullptr;

// The render path runs on one thread at a time; a Present on another thread meanwhile passes through: never a wait.
constinit std::atomic_flag g_busy{};

class Busy {
 public:
  Busy() noexcept : owned_(!g_busy.test_and_set(std::memory_order_acquire)) {}
  ~Busy() {
    if (owned_) {
      g_busy.clear(std::memory_order_release);
    }
  }
  Busy(const Busy&) = delete;
  Busy& operator=(const Busy&) = delete;
  bool owned() const noexcept { return owned_; }

 private:
  bool owned_;
};

// ---- Swap chain tags (D118). A tag is a COM object of ours that a swap chain holds as private data: DXGI releases it
// when the swap chain ends, which tells the host the swap chain is gone before its memory can be reused. A swap chain is
// classified once, at its first Present: bound (the one GraceUI draws on) or passed through.
enum class TagKind : std::uint32_t { free = 0, bound = 1, passed = 2 };

// {6b3f8c2e-91d4-4a57-b6e0-3c9d1f7a2e58}: GraceUI's own private-data id.
constexpr GUID kTagId = {0x6b3f8c2e, 0x91d4, 0x4a57, {0xb6, 0xe0, 0x3c, 0x9d, 0x1f, 0x7a, 0x2e, 0x58}};

class SwapChainTag final : public IUnknown {
 public:
  constexpr SwapChainTag() = default;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (id == __uuidof(IUnknown)) {
      AddRef();
      *object = static_cast<IUnknown*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (left == 0) {
      alive_.store(false, std::memory_order_release);  // the swap chain has let go of it: it has ended
    }
    return left;
  }

  bool usable() const noexcept { return !alive_.load(std::memory_order_acquire); }
  bool tags(IDXGISwapChain* chain) const noexcept {
    return alive_.load(std::memory_order_acquire) && chain_.load(std::memory_order_relaxed) == chain;
  }
  bool alive() const noexcept { return alive_.load(std::memory_order_acquire); }
  TagKind kind() const noexcept { return kind_.load(std::memory_order_relaxed); }

  bool attach(IDXGISwapChain* chain, TagKind kind) noexcept {
    chain_.store(chain, std::memory_order_relaxed);
    kind_.store(kind, std::memory_order_relaxed);
    alive_.store(true, std::memory_order_release);
    if (FAILED(chain->SetPrivateDataInterface(kTagId, this))) {
      alive_.store(false, std::memory_order_release);
      return false;
    }
    return true;
  }

 private:
  std::atomic<ULONG> refs_{0};
  std::atomic<bool> alive_{false};
  std::atomic<IDXGISwapChain*> chain_{nullptr};
  std::atomic<TagKind> kind_{TagKind::free};
};

constexpr int kTags = 8;
constinit SwapChainTag g_tags[kTags];
constinit SwapChainTag* g_bound_tag = nullptr;  // touched only under Busy

SwapChainTag* find_tag(IDXGISwapChain* chain) noexcept {
  for (SwapChainTag& tag : g_tags) {
    if (tag.tags(chain)) {
      return &tag;
    }
  }
  return nullptr;
}

SwapChainTag* free_tag() noexcept {
  for (SwapChainTag& tag : g_tags) {
    if (tag.usable()) {
      return &tag;
    }
  }
  return nullptr;
}

// ---- Finding the present queue (D116).

// Queues seen executing command lists: identities only, never dereferenced, except a queue in the middle of its own
// ExecuteCommandLists call. A queue joins the table at its first call and stays.
struct SeenQueue {
  std::atomic<ID3D12CommandQueue*> queue;
  std::atomic<std::uint32_t> kind;  // 0 while its entry is filled in, 1 direct, 2 another type
  std::atomic<ID3D12Device*> device;
  std::atomic<std::uint64_t> last_present;  // the present count when it last executed
};

constexpr int kSeenQueues = 16;
constinit SeenQueue g_seen[kSeenQueues]{};
constinit std::atomic<bool> g_seen_full{false};

constexpr std::uint32_t kDirect = 1;
constexpr std::uint32_t kOtherType = 2;

void fill_seen(SeenQueue& seen, ID3D12CommandQueue* queue, std::uint64_t now) noexcept {
  const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
  ID3D12Device* device = nullptr;
  if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device)))) {
    seen.device.store(device, std::memory_order_relaxed);
    device->Release();  // identity only
  }
  seen.last_present.store(now, std::memory_order_relaxed);
  const bool direct = desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
  seen.kind.store(direct ? kDirect : kOtherType, std::memory_order_release);
  if (direct) {
    state::direct_queues_seen.fetch_add(1, std::memory_order_relaxed);
  }
}

// Called from ExecuteCommandLists, on any thread, inside the call on `queue`: never waits, never allocates.
void note_queue(ID3D12CommandQueue* queue) noexcept {
  const std::uint64_t now = state::presents.load(std::memory_order_relaxed);
  for (SeenQueue& seen : g_seen) {
    ID3D12CommandQueue* current = seen.queue.load(std::memory_order_acquire);
    if (current == nullptr) {
      if (seen.queue.compare_exchange_strong(current, queue, std::memory_order_acq_rel)) {
        fill_seen(seen, queue, now);
        return;
      }
      // Another thread has just taken this entry; `current` now holds its queue.
    }
    if (current == queue) {
      seen.last_present.store(now, std::memory_order_relaxed);
      return;
    }
  }
  if (!g_seen_full.exchange(true, std::memory_order_relaxed)) {
    log_line("more than %d command queues executed; later ones are not noted", kSeenQueues);
  }
}

// Answers whether `queue` is a direct queue of `device`.
bool direct_queue_of(ID3D12CommandQueue* queue, ID3D12Device* device) noexcept {
  if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return false;
  }
  ID3D12Device* own = nullptr;
  if (FAILED(queue->GetDevice(IID_PPV_ARGS(&own)))) {
    return false;
  }
  own->Release();
  return own == device;
}

// The queue a ResizeBuffers1 call named, with a reference of ours, and the swap chain it was for (D116's second way).
constinit std::atomic<ID3D12CommandQueue*> g_named_queue{nullptr};
constinit std::atomic<IDXGISwapChain*> g_named_chain{nullptr};

// The direct queue last seen executing inside the original Present of a swap chain, with a reference of ours (D116's
// third way).
constinit std::atomic<ID3D12CommandQueue*> g_present_queue{nullptr};
constinit std::atomic<IDXGISwapChain*> g_present_chain{nullptr};

void hold_queue(std::atomic<ID3D12CommandQueue*>& slot, std::atomic<IDXGISwapChain*>& chain_slot,
                ID3D12CommandQueue* queue, IDXGISwapChain* chain) noexcept {
  queue->AddRef();
  chain_slot.store(chain, std::memory_order_relaxed);
  if (ID3D12CommandQueue* older = slot.exchange(queue, std::memory_order_acq_rel)) {
    older->Release();
  }
}

ID3D12CommandQueue* take_queue(std::atomic<ID3D12CommandQueue*>& slot, std::atomic<IDXGISwapChain*>& chain_slot,
                               IDXGISwapChain* chain) noexcept {
  if (slot.load(std::memory_order_acquire) == nullptr || chain_slot.load(std::memory_order_relaxed) != chain) {
    return nullptr;
  }
  return slot.exchange(nullptr, std::memory_order_acq_rel);
}

// The only direct queue of `device` seen executing, provided it executed since the previous Present and at least one
// whole frame has been watched; with a reference of ours. Two or more: none, for this way never guesses.
ID3D12CommandQueue* only_direct_queue(ID3D12Device* device) noexcept {
  const std::uint64_t now = state::presents.load(std::memory_order_relaxed);
  if (now < 3) {
    return nullptr;
  }
  ID3D12CommandQueue* found = nullptr;
  int count = 0;
  bool recent = false;
  for (const SeenQueue& seen : g_seen) {
    if (seen.kind.load(std::memory_order_acquire) != kDirect || seen.device.load(std::memory_order_relaxed) != device) {
      continue;
    }
    ++count;
    found = seen.queue.load(std::memory_order_relaxed);
    recent = seen.last_present.load(std::memory_order_relaxed) + 1 >= now;
  }
  static constinit bool told = false;
  if (count > 1 && !told) {
    told = true;
    log_line("%d direct queues execute on the swap chain's device: nothing is drawn until the game names one "
             "(ResizeBuffers1)",
             count);
  }
  if (count != 1 || !recent) {
    return nullptr;
  }
  found->AddRef();  // it executed within the last frame, on the game's word: it lives
  return found;
}

void offer_queue(ID3D12CommandQueue* queue, ID3D12Device* device, QueueMethod method) noexcept {
  if (direct_queue_of(queue, device)) {
    draw::set_queue(queue, method);
  } else {
    queue->Release();
  }
}

void find_present_queue(IDXGISwapChain* chain) noexcept {
  ID3D12Device* device = draw::device();
  if (device == nullptr) {
    return;
  }
  // 1. The swap chain itself: a Direct3D 12 swap chain's "device" is the queue it was created with, where the runtime
  //    keeps it so (Wine 9.0's and vkd3d-proton's answer with the device instead).
  ID3D12CommandQueue* queue = nullptr;
  if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&queue))) && queue != nullptr) {
    offer_queue(queue, device, QueueMethod::swap_chain);
    if (draw::has_queue()) {
      return;
    }
  }
  // 2. The game's own word: the queue it named in ResizeBuffers1.
  if ((queue = take_queue(g_named_queue, g_named_chain, chain)) != nullptr) {
    offer_queue(queue, device, QueueMethod::resize1);
    if (draw::has_queue()) {
      return;
    }
  }
  // 3. A direct queue the runtime executed inside the original Present of this swap chain, on its thread.
  if ((queue = take_queue(g_present_queue, g_present_chain, chain)) != nullptr) {
    offer_queue(queue, device, QueueMethod::present);
    if (draw::has_queue()) {
      return;
    }
  }
  // 4. The device's only direct queue seen executing.
  if ((queue = only_direct_queue(device)) != nullptr) {
    offer_queue(queue, device, QueueMethod::only_direct);
  }
}

// ---- Classifying a swap chain at its first Present.

bool window_of_this_process(HWND window) noexcept {
  const HWND root = GetAncestor(window, GA_ROOT);
  DWORD process = 0;
  GetWindowThreadProcessId(root != nullptr ? root : window, &process);
  return process == GetCurrentProcessId();
}

SwapChainTag* pass_through(SwapChainTag* tag, IDXGISwapChain* chain, const char* why) noexcept {
  state::passed_through.fetch_add(1, std::memory_order_relaxed);
  log_line("a swap chain passes through untouched: %s", why);
  return tag->attach(chain, TagKind::passed) ? tag : nullptr;
}

// Log lines that would otherwise repeat on every frame, each written once.
constinit bool g_told_second_chain = false;
constinit bool g_told_tag_refused = false;

SwapChainTag* classify(IDXGISwapChain* chain) {
  if (g_bound_tag != nullptr) {
    // Not tagged: once the bound swap chain ends, this one may be the game's, and is looked at again.
    if (!g_told_second_chain) {
      g_told_second_chain = true;
      log_line("a second Direct3D 12 swap chain passes through while one is bound");
    }
    return nullptr;
  }
  SwapChainTag* tag = free_tag();
  if (tag == nullptr) {
    return nullptr;  // every tag is in use: this swap chain passes through and is looked at again next time
  }
  IDXGISwapChain3* chain3 = nullptr;
  if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) {
    return pass_through(tag, chain, "no IDXGISwapChain3");
  }
  chain3->Release();  // the interface lives as long as the swap chain, which its tag watches
  ID3D12Resource* buffer = nullptr;
  if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
    return pass_through(tag, chain, "not Direct3D 12");
  }
  ID3D12Device* device = nullptr;
  const HRESULT got_device = buffer->GetDevice(IID_PPV_ARGS(&device));
  buffer->Release();
  if (FAILED(got_device)) {
    return pass_through(tag, chain, "its buffer names no device");
  }
  DXGI_SWAP_CHAIN_DESC1 desc{};
  HWND window = nullptr;
  if (FAILED(chain3->GetDesc1(&desc)) || FAILED(chain3->GetHwnd(&window)) || !window_of_this_process(window)) {
    device->Release();
    return pass_through(tag, chain, "not on a window of this process");
  }
  if (!tag->attach(chain, TagKind::bound)) {
    device->Release();
    if (!g_told_tag_refused) {
      g_told_tag_refused = true;
      log_line("a swap chain refused GraceUI's tag: it is not drawn on");
    }
    return nullptr;
  }
  g_bound_tag = tag;
  draw::bind(chain3, device, desc, window);
  return tag;
}

// ---- The detours.

// GraceUI's work before the original Present: under Busy, never waiting; answers whether `chain` is the bound swap
// chain (its frames are the ones whose cost the harness measures).
bool before_present(IDXGISwapChain* chain) {
  if (g_bound_tag != nullptr && !g_bound_tag->alive()) {
    g_bound_tag = nullptr;
    draw::unbind();
  }
  SwapChainTag* tag = find_tag(chain);
  if (tag == nullptr) {
    tag = classify(chain);
  }
  if (tag == nullptr || tag->kind() != TagKind::bound) {
    return false;
  }
  if (ID3D12CommandQueue* named = take_queue(g_named_queue, g_named_chain, chain)) {
    if (ID3D12Device* device = draw::device()) {
      offer_queue(named, device, QueueMethod::resize1);  // the game's word replaces any other way's answer
    } else {
      named->Release();
    }
  }
  if (!draw::has_queue()) {
    find_present_queue(chain);
  }
  draw::render();
  return true;
}

bool guarded_before_present(IDXGISwapChain* chain, UINT flags) noexcept {
  state::presents.fetch_add(1, std::memory_order_relaxed);
  if ((flags & DXGI_PRESENT_TEST) != 0) {
    return false;  // a test present shows nothing, so nothing is drawn
  }
  const counters::CountingScope counting;
  const Busy busy;
  if (!busy.owned()) {
    return false;
  }
  try {
    return before_present(chain);
  } catch (const ImGuiAssertion& failed) {
    log_line("a Dear ImGui assertion failed at line %d: %.80s", failed.line, failed.expression);
    draw::turn_off("a Dear ImGui assertion failed");
  } catch (...) {
    draw::turn_off("an exception in GraceUI's frame");
  }
  return false;
}

std::int64_t ticks() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
#else
  return 0;
#endif
}

void record_cost([[maybe_unused]] std::int64_t own_ticks, [[maybe_unused]] std::int64_t submit_ticks) noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  const auto ns = [&](std::int64_t ticks) -> std::uint64_t {
    const double value = static_cast<double>(ticks) * 1e9 / static_cast<double>(frequency.QuadPart);
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
  };
  counters::record_cost(ns(own_ticks), ns(submit_ticks));
#endif
}

std::int64_t take_submit_ticks() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  return counters::frame_submit_ticks.exchange(0, std::memory_order_relaxed);
#else
  return 0;
#endif
}

// What every Present detour does around the original call.
class PresentCall {
 public:
  PresentCall(IDXGISwapChain* chain, UINT flags) noexcept
      : outermost_(t_present_depth++ == 0), start_(ticks()), previous_(t_original_present) {
    if (outermost_) {
      take_submit_ticks();
      bound_ = guarded_before_present(chain, flags);
      submit_ = take_submit_ticks();
    }
    own_ = ticks() - start_;
    t_original_present = chain;
  }
  ~PresentCall() {
    t_original_present = previous_;
    --t_present_depth;
    if (outermost_ && bound_) {
      record_cost(own_, submit_);
    }
  }
  PresentCall(const PresentCall&) = delete;
  PresentCall& operator=(const PresentCall&) = delete;

 private:
  bool outermost_;
  bool bound_ = false;
  std::int64_t start_;
  std::int64_t own_ = 0;
  std::int64_t submit_ = 0;
  IDXGISwapChain* previous_;
};

HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* chain, UINT sync_interval, UINT flags) noexcept {
  const PresentCall call(chain, flags);
  return g_present(chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE present1_detour(IDXGISwapChain1* chain, UINT sync_interval, UINT flags,
                                          const DXGI_PRESENT_PARAMETERS* parameters) noexcept {
  const PresentCall call(chain, flags);
  return g_present1(chain, sync_interval, flags, parameters);
}

void before_resize(IDXGISwapChain* chain) noexcept {
  const SwapChainTag* tag = find_tag(chain);
  if (tag != nullptr && tag->kind() == TagKind::bound) {
    state::resizes.fetch_add(1, std::memory_order_relaxed);
    draw::mark_buffers_stale();
  }
}

HRESULT STDMETHODCALLTYPE resize_buffers_detour(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                                DXGI_FORMAT format, UINT flags) noexcept {
  before_resize(chain);
  return g_resize_buffers(chain, count, width, height, format, flags);
}

// D116's second way: the queues the game names for the swap chain's buffers. One queue for every buffer is a present
// queue GraceUI can draw on; queues that differ (several adapters' nodes) are not taken.
void note_named_queues(IDXGISwapChain3* chain, UINT count, IUnknown* const* queues) noexcept {
  if (queues == nullptr || queues[0] == nullptr) {
    return;
  }
  if (count == 0) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    if (FAILED(chain->GetDesc1(&desc))) {
      return;
    }
    count = desc.BufferCount;
  }
  for (UINT i = 1; i < count; ++i) {
    if (queues[i] != queues[0]) {
      return;
    }
  }
  ID3D12CommandQueue* queue = nullptr;
  if (FAILED(queues[0]->QueryInterface(IID_PPV_ARGS(&queue)))) {
    return;
  }
  hold_queue(g_named_queue, g_named_chain, queue, chain);
  queue->Release();  // hold_queue took its own reference
}

HRESULT STDMETHODCALLTYPE resize_buffers1_detour(IDXGISwapChain3* chain, UINT count, UINT width, UINT height,
                                                 DXGI_FORMAT format, UINT flags, const UINT* node_masks,
                                                 IUnknown* const* queues) noexcept {
  note_named_queues(chain, count, queues);
  before_resize(chain);
  return g_resize_buffers1(chain, count, width, height, format, flags, node_masks, queues);
}

void STDMETHODCALLTYPE execute_command_lists_detour(ID3D12CommandQueue* queue, UINT count,
                                                    ID3D12CommandList* const* lists) noexcept {
  {
    const counters::CountingScope counting;
    note_queue(queue);
    IDXGISwapChain* const presenting = t_original_present;
    if (presenting != nullptr && state::queue_method.load(std::memory_order_relaxed) == QueueMethod::none &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
      hold_queue(g_present_queue, g_present_chain, queue, presenting);
    }
  }
  g_execute_command_lists(queue, count, lists);
}

// ---- The init thread.

void give_up(const char* why) noexcept {
  state::host.store(HostState::failed, std::memory_order_release);
  log_line("the host stays idle: %s", why);
}

struct WindowSearch {
  DWORD process;
  HWND best;
  LONG best_area;
};

BOOL CALLBACK consider_window(HWND window, LPARAM context) {
  // SAFETY: `context` is the WindowSearch that find_game_window passes to EnumWindows, alive for the whole call.
  WindowSearch* search = reinterpret_cast<WindowSearch*>(context);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process != search->process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
    return TRUE;
  }
  RECT client{};
  if (!GetClientRect(window, &client)) {
    return TRUE;
  }
  const LONG area = (client.right - client.left) * (client.bottom - client.top);
  if (area > search->best_area) {
    search->best = window;
    search->best_area = area;
  }
  return TRUE;
}

// The game's window, game-agnostically: this process's largest visible top-level window that no other window owns.
HWND find_game_window() noexcept {
  WindowSearch search{GetCurrentProcessId(), nullptr, 0};
  // SAFETY: EnumWindows hands the pointer back to consider_window only, during this call.
  EnumWindows(&consider_window, reinterpret_cast<LPARAM>(&search));
  return search.best;
}

HWND wait_for_game_window() noexcept {
  constexpr int kAttempts = 1200;  // 100 ms apart: 120 s (EST)
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    if (const HWND window = find_game_window()) {
      return window;
    }
    Sleep(100);
  }
  return nullptr;
}

void install(const grace_host_fn (&targets)[kHookCount]) noexcept {
  if (state::hook_install_runs.fetch_add(1, std::memory_order_acq_rel) != 0) {
    return;  // once per process: D72's mutex lets one copy here, and this guard lets it here once
  }
  for (int i = 0; i < kHookCount; ++i) {
    if (targets[i] == nullptr) {
      give_up("a hook target was not found");
      return;
    }
    for (int j = 0; j < i; ++j) {
      if (targets[i] == targets[j]) {
        give_up("two hook targets share one address");
        return;
      }
    }
  }
  if (MH_Initialize() != MH_OK) {
    give_up("MinHook could not start");
    return;
  }
  // SAFETY: each detour has its target's exact signature; MinHook stores each trampoline in the matching original.
  void* const detours[kHookCount] = {
      reinterpret_cast<void*>(&present_detour), reinterpret_cast<void*>(&present1_detour),
      reinterpret_cast<void*>(&resize_buffers_detour), reinterpret_cast<void*>(&resize_buffers1_detour),
      reinterpret_cast<void*>(&execute_command_lists_detour)};
  // SAFETY: MinHook writes a function address into each of these function-pointer variables, of the right type.
  void** const originals[kHookCount] = {
      reinterpret_cast<void**>(&g_present), reinterpret_cast<void**>(&g_present1),
      reinterpret_cast<void**>(&g_resize_buffers), reinterpret_cast<void**>(&g_resize_buffers1),
      reinterpret_cast<void**>(&g_execute_command_lists)};
  int created = 0;
  for (; created < kHookCount; ++created) {
    // SAFETY: the target is a function of the runtime's, read from a vtable by hooks_vtable.c.
    void* const target = reinterpret_cast<void*>(targets[created]);
    if (MH_CreateHook(target, detours[created], originals[created]) != MH_OK) {
      log_line("MinHook refused %s", kHookNames[created]);
      break;
    }
  }
  if (created != kHookCount) {
    // None is enabled yet: removing the ones created unhooks nothing.
    for (int i = 0; i < created; ++i) {
      // SAFETY: as above.
      MH_RemoveHook(reinterpret_cast<void*>(targets[i]));
    }
    give_up("not every hook could be created");
    return;
  }
  // All at once, with the process's other threads held still by MinHook while their code is patched. Never undone.
  if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
    give_up("MinHook could not enable the hooks");
    return;
  }
  state::hooks_installed.store(kHookCount, std::memory_order_relaxed);
  state::host.store(HostState::hooked, std::memory_order_release);
  log_line("%d hooks installed: Present, Present1, ResizeBuffers, ResizeBuffers1, ExecuteCommandLists", kHookCount);
}

void run_init() noexcept {
  // S10.4.b adds the anti-cheat check here, before anything else (D88); until then nothing is checked.
  if (wait_for_game_window() == nullptr) {
    give_up("no window of this process appeared within 120 s");
    return;
  }
  grace_host_fn targets[kHookCount] = {};
  if (!grace_host_read_hook_targets(targets)) {
    give_up("the hook targets could not be read from a Direct3D 12 swap chain of GraceUI's own");
    return;
  }
  install(targets);
}

}  // namespace

DWORD WINAPI init_thread_main(LPVOID) noexcept {
  try {
    run_init();
  } catch (...) {
    give_up("an exception in the init thread");
  }
  return 0;
}

}  // namespace grace::host::hooks
