// hooks.cpp: GraceUI's hooks (D102), the one place that installs any (invariant 16). The init thread (D71) waits for a
// window of the game's, reads the render half's five targets from the vtables of a short-lived device, queue and swap
// chain of its own (hooks_vtable.c) and the input half's from the DirectInput and XInput DLLs already loaded
// (hooks_input.cpp), installs them all with MinHook from one table, at once, then filters the game's window procedure
// (D125); nothing is ever removed (D107). It then stays as GraceUI's own thread, asleep until woken: it bakes the
// shared atlas (draw.cpp), moves the filter to the bound swap chain's window when that is another, and passes the log
// to the debugger, none of which a game thread may wait on. Then the render half's detours: Present and Present1 drive the
// render path (draw.cpp), ResizeBuffers and ResizeBuffers1 keep it off the buffers they replace, and ExecuteCommandLists
// notes which queues execute, to find the present queue (D116). The input half's are hooks_input.cpp's.
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

#include "MinHook.h"
#include "counters.h"
#include "draw.h"
#include "draw_alloc.h"
#include "draw_atlas.h"
#include "hooks_input.h"
#include "hooks_vtable.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "spaces.h"
#include "state.h"
#include "steam.h"

namespace grace::host::hooks {
namespace {

// ---- The one table (D102, D120): every hook GraceUI installs is an entry of it, named from D102's published list
// (tools/check_safety.py reads these names): the render half's five, whose order is hooks_vtable.h's
// (GRACE_HOST_HOOK_*), then the input half's. The window procedure's is a filter, not a MinHook hook (D125), made once
// after the others are enabled.
enum class HookName : int {
  present = 0,
  present1 = 1,
  resize_buffers = 2,
  resize_buffers1 = 3,
  execute_command_lists = 4,
  window_procedure = 5,
  get_device_state = 6,
  xinput_get_state = 7,
};
constexpr const char* kHookNames[] = {"IDXGISwapChain::Present", "IDXGISwapChain1::Present1",
                                      "IDXGISwapChain::ResizeBuffers", "IDXGISwapChain3::ResizeBuffers1",
                                      "ID3D12CommandQueue::ExecuteCommandLists", "the game's window procedure",
                                      "IDirectInputDevice8::GetDeviceState", "XInputGetState"};

constexpr int kRenderHooks = GRACE_HOST_RENDER_HOOKS;
constexpr int kMaxHooks = kRenderHooks + hooks_input::kDirectInputTargets + hooks_input::kXInputDlls;

struct Hook {
  HookName name;
  void* target;
  void* detour;
  void** original;
  const char* module;  // where it was found, for the log: an XInput DLL's name, or null
};

const char* name_of(HookName name) noexcept { return kHookNames[static_cast<int>(name)]; }

// What install() made, in order, the window procedure's filter last: for the log and the test exports.
constinit Hook g_installed[kMaxHooks + 1] = {};
constinit std::atomic<int> g_installed_count{0};
constinit std::atomic<std::uint32_t> g_present_prologue{0};  // 0 unknown, 1 plain, 2 a jump (D86)

// Whether a function's first bytes already hold a jump: another hook's, most likely (D86: a cue, never a refusal).
bool starts_with_jump(const void* target) noexcept {
  // SAFETY: the first 13 bytes of a function's code in a loaded module, read as MinHook itself reads them.
  const auto* b = static_cast<const std::uint8_t*>(target);
  return b[0] == 0xE9 || b[0] == 0xEB ||                                              // jmp rel32, jmp rel8
         (b[0] == 0xFF && b[1] == 0x25) ||                                           // jmp [rip+disp32]
         (b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0) ||         // mov rax, imm64; jmp rax
         (b[0] == 0x49 && b[1] == 0xBB && b[10] == 0x41 && b[11] == 0xFF && b[12] == 0xE3) ||  // mov r11; jmp r11
         (b[0] == 0x68 && b[5] == 0xC3);                                             // push imm32; ret
}

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

// Presents within which a queue counts as one the game still uses (EST): a queue the game released, or replaced on a
// resize, falls out of D116's fourth way once it has not executed for this long, rather than standing for the session.
constexpr std::uint64_t kQueueSeenPresents = 120;

// The only direct queue of `device` seen executing lately, provided it executed since the previous Present and at
// least one whole frame has been watched: an identity only, never dereferenced, for no reference of ours keeps it (it
// may have been released since its last call). Two or more: none, for this way never guesses.
ID3D12CommandQueue* only_direct_queue(ID3D12Device* device) noexcept {
  const std::uint64_t now = state::presents.load(std::memory_order_relaxed);
  if (now < 3) {
    return nullptr;
  }
  ID3D12CommandQueue* found = nullptr;
  int count = 0;
  bool recent = false;
  for (const SeenQueue& seen : g_seen) {
    const std::uint64_t last = seen.last_present.load(std::memory_order_relaxed);
    if (seen.kind.load(std::memory_order_acquire) != kDirect || seen.device.load(std::memory_order_relaxed) != device ||
        last + kQueueSeenPresents < now) {
      continue;
    }
    ++count;
    found = seen.queue.load(std::memory_order_relaxed);
    recent = last + 1 >= now;
  }
  static constinit bool told = false;
  if (count > 1 && !told) {
    told = true;
    log_line("%d direct queues execute on the swap chain's device: nothing is drawn until the game names one "
             "(ResizeBuffers1)",
             count);
  }
  return count == 1 && recent ? found : nullptr;
}

// D116's fourth way, made sound (L7): way 4 names a candidate by identity; the ExecuteCommandLists detour takes a
// reference to it inside the candidate's own call, when it certainly lives, and the next Present takes that.
constinit std::atomic<ID3D12CommandQueue*> g_candidate{nullptr};
constinit std::atomic<ID3D12CommandQueue*> g_confirmed{nullptr};

ID3D12CommandQueue* take_confirmed(ID3D12CommandQueue* candidate) noexcept {
  ID3D12CommandQueue* queue = g_confirmed.exchange(nullptr, std::memory_order_acq_rel);
  if (queue != nullptr && queue != candidate) {
    queue->Release();
    return nullptr;
  }
  return queue;
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
  // 4. The device's only direct queue seen executing, once a call of its own has given GraceUI a reference.
  ID3D12CommandQueue* const candidate = only_direct_queue(device);
  g_candidate.store(candidate, std::memory_order_release);
  if (candidate != nullptr && (queue = take_confirmed(candidate)) != nullptr) {
    offer_queue(queue, device, QueueMethod::only_direct);
  }
}

// The queues held for D116's ways, let go once nothing will be drawn (L3).
void drop_held_queues() noexcept {
  for (std::atomic<ID3D12CommandQueue*>* slot : {&g_named_queue, &g_present_queue, &g_confirmed}) {
    if (ID3D12CommandQueue* held = slot->exchange(nullptr, std::memory_order_acq_rel)) {
      held->Release();
    }
  }
  g_candidate.store(nullptr, std::memory_order_release);
}

// ---- Classifying a swap chain at its first Present.

bool window_of_this_process(HWND window) noexcept {
  const HWND root = GetAncestor(window, GA_ROOT);
  DWORD process = 0;
  GetWindowThreadProcessId(root != nullptr ? root : window, &process);
  return process == GetCurrentProcessId();
}

// The bound swap chain's window (its top-level window) is the game's, whatever the init thread guessed when it looked
// first (D120: a console, a splash or a loader window may have come first, M2): when it is not the window filtered,
// GraceUI's thread moves the filter there. Present thread; never waits.
constinit std::atomic<HWND> g_window_wanted{nullptr};

void want_window(HWND window) noexcept {
  const HWND root = GetAncestor(window, GA_ROOT);
  const HWND top = root != nullptr ? root : window;
  if (top != input::window() || !input::window_filtered()) {
    g_window_wanted.store(top, std::memory_order_release);
    wake();
  }
}

SwapChainTag* pass_through(SwapChainTag* tag, IDXGISwapChain* chain, const char* why) noexcept {
  state::passed_through.fetch_add(1, std::memory_order_relaxed);
  log_line("a swap chain passes through untouched: %s", why);
  return tag->attach(chain, TagKind::passed) ? tag : nullptr;
}

// Once the host has failed (give_up), the hooks that are live anyway pass everything through (L1): MinHook enables its
// hooks one by one and stops at the first that fails, leaving the earlier ones enabled, and GraceUI never unhooks.
bool host_failed() noexcept { return state::host.load(std::memory_order_acquire) == HostState::failed; }

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
  state::reach_frame_step(2);  // D230
  want_window(window);
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
  if (state::render_off.load(std::memory_order_relaxed)) {
    drop_held_queues();  // L3: no reference of GraceUI's keeps a queue, or its device, alive once nothing is drawn
  }
  input::tick();  // the player's exit and the watchdog, once a frame as well as on every input
  return true;
}

bool guarded_before_present(IDXGISwapChain* chain, UINT flags) noexcept {
  state::presents.fetch_add(1, std::memory_order_relaxed);
  state::reach_frame_step(1);  // D230
  if ((flags & DXGI_PRESENT_TEST) != 0) {
    return false;  // a test present shows nothing, so nothing is drawn
  }
  if (host_failed()) {
    return false;  // MinHook enabled some hooks before one failed (L1): they pass every call through, untouched
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
      if (state::frame_step.load(std::memory_order_relaxed) == 10) {
        state::reach_frame_step(11);  // the game's Present returned after GraceUI's first frame (D230)
      }
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
  if (queues == nullptr || queues[0] == nullptr || host_failed() || state::render_off.load(std::memory_order_relaxed)) {
    return;  // nothing will be drawn: no queue is held (L1, L3)
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
    if (state::queue_method.load(std::memory_order_relaxed) == QueueMethod::none && !host_failed() &&
        !state::render_off.load(std::memory_order_relaxed)) {
      IDXGISwapChain* const presenting = t_original_present;
      if (presenting != nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        hold_queue(g_present_queue, g_present_chain, queue, presenting);
      }
      if (queue == g_candidate.load(std::memory_order_acquire)) {
        // Inside its own call the candidate lives: the reference taken here is sound (L7).
        queue->AddRef();
        if (ID3D12CommandQueue* older = g_confirmed.exchange(queue, std::memory_order_acq_rel)) {
          older->Release();
        }
      }
    }
  }
  g_execute_command_lists(queue, count, lists);
}

// ---- The init thread.

void give_up(const char* why) noexcept {
  state::host.store(HostState::failed, std::memory_order_release);
  log_line("the host stays idle: %s", why);
  // Any hook already live passes everything through (L1), and the input half lets every key reach the game, the menu
  // key included (L4): nothing could open a menu now.
  input::turn_off("the host stays idle");
}

struct WindowSearch {
  DWORD process;
  HWND best;
  LONG best_area;
};

// A console's window: conhost names the console's client as its window's owning process, so a mod loader's console
// would otherwise pass for the game's (M2); a pseudo-console's is hidden, and checked all the same.
bool console_window(HWND window) noexcept {
  if (window == GetConsoleWindow()) {
    return true;
  }
  wchar_t name[32] = {};
  const int length = GetClassNameW(window, name, static_cast<int>(sizeof name / sizeof name[0]));
  return length > 0 && (std::wcscmp(name, L"ConsoleWindowClass") == 0 || std::wcscmp(name, L"PseudoConsoleWindow") == 0);
}

BOOL CALLBACK consider_window(HWND window, LPARAM context) {
  // SAFETY: `context` is the WindowSearch that find_game_window passes to EnumWindows, alive for the whole call.
  WindowSearch* search = reinterpret_cast<WindowSearch*>(context);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process != search->process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr ||
      console_window(window)) {
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
    log_drain();
    Sleep(100);
  }
  return nullptr;
}

void install(const grace_host_fn (&render)[kRenderHooks], const hooks_input::Targets& input_targets,
             HWND window) noexcept {
  if (state::hook_install_runs.fetch_add(1, std::memory_order_acq_rel) != 0) {
    return;  // once per process: D72's mutex lets one copy here, and this guard lets it here once
  }
  // SAFETY: each detour has its target's exact signature; MinHook stores each trampoline in the matching original, a
  // function-pointer variable of the right type.
  const Hook render_hooks[kRenderHooks] = {
      {HookName::present, nullptr, reinterpret_cast<void*>(&present_detour), reinterpret_cast<void**>(&g_present),
       nullptr},
      {HookName::present1, nullptr, reinterpret_cast<void*>(&present1_detour), reinterpret_cast<void**>(&g_present1),
       nullptr},
      {HookName::resize_buffers, nullptr, reinterpret_cast<void*>(&resize_buffers_detour),
       reinterpret_cast<void**>(&g_resize_buffers), nullptr},
      {HookName::resize_buffers1, nullptr, reinterpret_cast<void*>(&resize_buffers1_detour),
       reinterpret_cast<void**>(&g_resize_buffers1), nullptr},
      {HookName::execute_command_lists, nullptr, reinterpret_cast<void*>(&execute_command_lists_detour),
       reinterpret_cast<void**>(&g_execute_command_lists), nullptr}};
  Hook table[kMaxHooks] = {};
  int count = 0;
  for (int i = 0; i < kRenderHooks; ++i) {
    if (render[i] == nullptr) {
      give_up("a hook target was not found");
      return;
    }
    table[count] = render_hooks[i];
    // SAFETY: the target is a function of the runtime's, read from a vtable by hooks_vtable.c.
    table[count++].target = reinterpret_cast<void*>(render[i]);
    // Its module stays loaded for the session (L8), as the input half's do: ExecuteCommandLists lives in the runtime's
    // core DLL, which d3d12.dll loads itself and could otherwise unload and load again elsewhere.
    hooks_input::pin_module_of(table[count - 1].target);
  }
  for (int slot = 0; slot < hooks_input::kDirectInputTargets; ++slot) {
    if (input_targets.get_device_state[slot] != nullptr) {
      table[count++] = Hook{HookName::get_device_state, input_targets.get_device_state[slot],
                            hooks_input::get_device_state_detour(slot), hooks_input::get_device_state_original(slot),
                            nullptr};
    }
  }
  for (int slot = 0; slot < hooks_input::kXInputDlls; ++slot) {
    if (input_targets.xinput_get_state[slot] != nullptr) {
      table[count++] = Hook{HookName::xinput_get_state, input_targets.xinput_get_state[slot],
                            hooks_input::xinput_detour(slot), hooks_input::xinput_original(slot),
                            hooks_input::kXInputNamesNarrow[slot]};
    }
  }
  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < i; ++j) {
      if (table[i].target == table[j].target) {
        give_up("two hook targets share one address");
        return;
      }
    }
  }
  for (int i = 0; i < count; ++i) {
    if (table[i].name == HookName::present) {
      const bool jump = starts_with_jump(table[i].target);
      g_present_prologue.store(jump ? 2u : 1u, std::memory_order_relaxed);
      if (jump) {
        log_line("Present already starts with a jump: another overlay may have hooked it first (D86); GraceUI hooks "
                 "it all the same");
      }
    }
  }
  if (MH_Initialize() != MH_OK) {
    give_up("MinHook could not start");
    return;
  }
  int created = 0;
  for (; created < count; ++created) {
    if (MH_CreateHook(table[created].target, table[created].detour, table[created].original) != MH_OK) {
      log_line("MinHook refused %s", name_of(table[created].name));
      break;
    }
  }
  if (created != count) {
    // None is enabled yet: removing the ones created unhooks nothing.
    for (int i = 0; i < created; ++i) {
      MH_RemoveHook(table[i].target);
    }
    give_up("not every hook could be created");
    return;
  }
  // All at once, with the process's other threads held still by MinHook while their code is patched. Never undone.
  if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
    give_up("MinHook could not enable the hooks");
    return;
  }
  std::uint32_t dinput_hooks = 0;
  std::uint32_t xinput_hooks = 0;
  for (int i = 0; i < count; ++i) {
    g_installed[i] = table[i];
    dinput_hooks += table[i].name == HookName::get_device_state ? 1u : 0u;
    xinput_hooks += table[i].name == HookName::xinput_get_state ? 1u : 0u;
  }
  // The filter on the game's window procedure, once (D125). Without it the render half still runs, but no holder can
  // have input (the input half answers not_now).
  const bool filtered = hooks_input::filter_window(window);
  if (filtered) {
    g_installed[count++] = Hook{HookName::window_procedure, nullptr, nullptr, nullptr, nullptr};
  }
  g_installed_count.store(count, std::memory_order_release);
  state::render_hooks.store(kRenderHooks, std::memory_order_relaxed);
  state::dinput_hooks.store(dinput_hooks, std::memory_order_relaxed);
  state::xinput_hooks.store(xinput_hooks, std::memory_order_relaxed);
  state::hooks_installed.store(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
  state::init_step.store(7, std::memory_order_release);  // done (D220), before the host reads as hooked
  state::host.store(HostState::hooked, std::memory_order_release);
  log_line("%d hooks installed: the render half's %d, GetDeviceState %u, XInputGetState %u, the window procedure %u",
           count, kRenderHooks, dinput_hooks, xinput_hooks, filtered ? 1u : 0u);
  for (int i = 0; i < count; ++i) {
    if (g_installed[i].module != nullptr) {
      log_line("XInputGetState hooked in %s", g_installed[i].module);
    }
  }
}

// ---- GraceUI's own thread, after the init (D120 as amended).

constinit HANDLE g_wake = nullptr;  // an auto-reset event, made before the thread starts; never closed

// The window filter follows the bound swap chain's window (M2), if that is not the one filtered.
void move_filter() noexcept {
  if (state::host.load(std::memory_order_acquire) != HostState::hooked) {
    return;  // the install is not done (a Present may come between the hooks' enabling and the filter): left for later
  }
  if (state::render_off.load(std::memory_order_acquire)) {
    g_window_wanted.store(nullptr, std::memory_order_release);
    return;  // nothing is drawn, and input is off: no window needs the filter now
  }
  const HWND wanted = g_window_wanted.exchange(nullptr, std::memory_order_acq_rel);
  if (wanted == nullptr || (wanted == input::window() && input::window_filtered()) || !IsWindow(wanted) ||
      !window_of_this_process(wanted)) {
    return;
  }
  const bool first = !input::window_filtered();
  if (!hooks_input::filter_window(wanted) || !first) {
    return;
  }
  // The first filter could not be made (a console's window, M2): this one is the window procedure's hook.
  const int count = g_installed_count.load(std::memory_order_acquire);
  if (count < kMaxHooks + 1) {
    g_installed[count] = Hook{HookName::window_procedure, nullptr, nullptr, nullptr, nullptr};
    g_installed_count.store(count + 1, std::memory_order_release);
    state::hooks_installed.fetch_add(1, std::memory_order_relaxed);
  }
}

[[noreturn]] void serve() noexcept {
  for (;;) {
    if (g_wake != nullptr) {
      // About once a second by itself while the browser view's server runs: its commands' deadlines (D169); in a test
      // build, at least once in each of the heap guard's periods (D235).
#if defined(GRACEUI_TEST_EXPORTS)
      const DWORD idle = draw_alloc::check_period_ms();
#else
      const DWORD idle = INFINITE;
#endif
      // While the menu waits on a deadline of the open page's (D274), until then.
      const DWORD timer = menu::wants_timer() ? menu::timer_wait_ms() : 1000;
      WaitForSingleObject(g_wake, spaces::wants_timer() || menu::wants_timer() ? timer : idle);
    } else {
      Sleep(50);
    }
#if defined(GRACEUI_TEST_EXPORTS)
    draw_alloc::check_heap_when_due();
#endif
    log_drain();
    draw::serve_bake();
    draw::serve_present();  // the mods whose on_present callbacks the present thread suspended (D259)
    move_filter();
    spaces::serve();  // the spaces and the browser view's server (D169), before the menu reads GraceUI's settings
    menu::serve();    // the menu's layout and commands (D167)
    steam::follow_holder();  // a Steam keyboard closes once its holder no longer holds input (D276, invariant 7)
    log_drain();
  }
}

void run_init() noexcept {
  // The anti-cheat check ran in the first request, before this thread started (D133); in its notice-only mode the
  // hooks go in all the same, input off from the start (D88).
  const auto step = [](std::uint32_t n) { state::init_step.store(n, std::memory_order_release); };
  step(1);
  atlas::load_own_fonts();  // GraceUI's notices' face, read here so that warm-up reads no file (§3.16)
  step(2);
  const HWND window = wait_for_game_window();
  if (window == nullptr) {
    give_up("no window of this process appeared within 120 s");
    return;
  }
  step(3);
  if (!grace_host_bind_d3d()) {
    give_up("Direct3D 12, DXGI or the shader compiler (d3dcompiler_47.dll) could not be found in System32");
    return;
  }
  grace_host_fn render[kRenderHooks] = {};
  step(4);
  if (!grace_host_read_hook_targets(render)) {
    give_up("the hook targets could not be read from a Direct3D 12 swap chain of GraceUI's own");
    return;
  }
  hooks_input::Targets input_targets;
  step(5);
  hooks_input::find_targets(input_targets);
  step(6);
  install(render, input_targets, window);  // 7 once it has hooked; a give-up in it leaves 6
}

}  // namespace

DWORD WINAPI init_thread_main(LPVOID) noexcept {
  try {
    run_init();
  } catch (...) {
    give_up("an exception in the init thread");
  }
  serve();  // whatever the init's outcome: the log still reaches the debugger
}

bool prepare_wake() noexcept {
  g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  return g_wake != nullptr;
}

void wake() noexcept {
  if (g_wake != nullptr) {
    SetEvent(g_wake);
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
bool installed_hook(std::uint32_t index, const char*& name, const char*& module) noexcept {
  if (index >= static_cast<std::uint32_t>(g_installed_count.load(std::memory_order_acquire))) {
    return false;
  }
  name = name_of(g_installed[index].name);
  module = g_installed[index].module;
  return true;
}

std::uint32_t present_prologue() noexcept { return g_present_prologue.load(std::memory_order_relaxed); }

namespace {

static_assert(sizeof(void*) == 8, "the jumps followed are x64's");

// Whether `size` bytes from `at` are committed and readable now, across regions if need be (D240).
bool readable(std::uintptr_t at, std::size_t size) noexcept {
  constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  while (size > 0) {
    MEMORY_BASIC_INFORMATION info{};
    // SAFETY: VirtualQuery takes any address and only describes the region that holds it.
    if (VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof info) != sizeof info ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (info.Protect & kReadable) == 0) {
      return false;
    }
    // SAFETY: the region's base as a number, for the arithmetic of its end only.
    const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    const std::size_t here = end - at < size ? end - at : size;
    at += here;
    size -= here;
  }
  return true;
}

template <typename T>
T read_at(std::uintptr_t at) noexcept {
  T value{};
  // SAFETY: `at` was found readable for these bytes just before; code is read as MinHook itself reads it.
  std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
  return value;
}

// Follows the jumps at a function's entry, at most four, of the forms starts_with_jump knows but the push and ret, and
// answers where they end; `through` says whether the chain reached `detour` (a detour behind a linker's thunk counts).
std::uintptr_t follow(std::uintptr_t at, std::uintptr_t detour, std::uint32_t& jumps, bool& through) noexcept {
  jumps = 0;
  through = detour != 0 && at == detour;
  for (int step = 0; step < 4 && !through && readable(at, 13); ++step) {
    const auto b0 = read_at<std::uint8_t>(at);
    const auto b1 = read_at<std::uint8_t>(at + 1);
    std::uintptr_t next = 0;
    if (b0 == 0xE9) {  // jmp rel32
      next = at + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read_at<std::int32_t>(at + 1)));
    } else if (b0 == 0xEB) {  // jmp rel8: MinHook's short jump to the five bytes above a function too short to patch
      next = at + 2 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(static_cast<std::int8_t>(b1)));
    } else if (b0 == 0xFF && b1 == 0x25) {  // jmp [rip+disp32]: MinHook's relay, an import's thunk
      const std::uintptr_t slot =
          at + 6 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read_at<std::int32_t>(at + 2)));
      if (!readable(slot, 8)) {
        break;
      }
      next = read_at<std::uintptr_t>(slot);
    } else if ((b0 == 0x48 && b1 == 0xB8 && read_at<std::uint8_t>(at + 10) == 0xFF &&
                read_at<std::uint8_t>(at + 11) == 0xE0) ||  // mov rax, imm64; jmp rax
               (b0 == 0x49 && b1 == 0xBB && read_at<std::uint8_t>(at + 10) == 0x41 &&
                read_at<std::uint8_t>(at + 11) == 0xFF && read_at<std::uint8_t>(at + 12) == 0xE3)) {  // mov r11
      next = read_at<std::uintptr_t>(at + 2);
    } else {
      break;
    }
    at = next;
    ++jumps;
    through = detour != 0 && at == detour;
  }
  return at;
}

// The module that holds `at`, answered as its handle, with its base name, UTF-8, in `out`; 0 and "" if no module does
// (memory of no image, or none at all).
std::uintptr_t module_of(std::uintptr_t at, char* out, std::uint32_t capacity) noexcept {
  out[0] = '\0';
  HMODULE module = nullptr;
  // SAFETY: GetModuleHandleExW's FROM_ADDRESS form takes the address in its name argument's type, and only finds the
  // module by it.
  if (at == 0 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCWSTR>(at), &module)) {
    return 0;
  }
  // SAFETY: a module's handle is its base address; answered as a number, for a test to compare with its own handle.
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  constexpr DWORD kUnits = 1024;
  wchar_t path[kUnits];
  const DWORD length = GetModuleFileNameW(module, path, kUnits);
  if (length == 0 || length >= kUnits) {
    return base;
  }
  const wchar_t* name = path;
  for (DWORD i = 0; i < length; ++i) {
    name = path[i] == L'\\' || path[i] == L'/' ? path + i + 1 : name;
  }
  if (WideCharToMultiByte(CP_UTF8, 0, name, -1, out, static_cast<int>(capacity), nullptr, nullptr) <= 0) {
    out[0] = '\0';  // too long for `capacity`: no name rather than part of one
  }
  return base;
}

}  // namespace

bool hook_lead(std::uint32_t index, bool& here, std::uint32_t& jumps, std::uintptr_t& base, char* module,
               std::uint32_t capacity) noexcept {
  if (capacity == 0 || index >= static_cast<std::uint32_t>(g_installed_count.load(std::memory_order_acquire))) {
    return false;
  }
  const Hook& hook = g_installed[index];
  std::uintptr_t end = 0;
  if (hook.name == HookName::window_procedure) {
    const void* procedure = nullptr;
    if (!hooks_input::window_lead(procedure, here)) {
      here = false;
    }
    jumps = 0;
    // SAFETY: the procedure's address as a number, to find its module.
    end = reinterpret_cast<std::uintptr_t>(procedure);
  } else {
    // SAFETY: the hook's target and detour as numbers: code addresses MinHook was given, read for their jumps only.
    end = follow(reinterpret_cast<std::uintptr_t>(hook.target), reinterpret_cast<std::uintptr_t>(hook.detour), jumps,
                 here);
  }
  base = module_of(end, module, capacity);
  return true;
}
#endif

}  // namespace grace::host::hooks
