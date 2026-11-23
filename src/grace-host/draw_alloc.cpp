// draw_alloc.cpp: in a test build, every allocation and free that graceui.dll's own code makes (its operator new and
// delete, which Dear ImGui's DX12 backend also uses, and Dear ImGui's allocator) goes through the heap guard (D235,
// grace-guard), and is counted while a detour runs GraceUI's work on that thread, for the harness's allocation test
// (invariant 10). A breach the guard finds is recorded for GraceUI_Test_GetHeapGuard, logged, and raised as
// GRACEUI_TEST_HEAP_GUARD_BREACH, which a handler of GraceUI's own, last in the process's list, ends the run with: the
// spike observer's recorder, first in that list, writes it down before. A release build compiles nothing here:
// graceui.dll then uses the CRT's own functions and Dear ImGui's defaults.
//
// SAFETY: the casts here turn a frame's address and a module's handle into numbers, to tell whether a frame lies in
// graceui.dll and at what offset (GetModuleHandleExW reads the address only to find its module); and the probe's writes
// past a block's end, before its start and after its free are the guard's own bytes, which it owns until it frees them.
#if defined(GRACEUI_TEST_EXPORTS)

#include "draw_alloc.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <new>

#include "counters.h"
#include "grace/guard/heap_guard.h"
#include "log.h"

#if defined(_MSC_VER)
#define GU_NOINLINE __declspec(noinline)
#else
#define GU_NOINLINE __attribute__((noinline))
#endif

namespace grace::host::counters {
constinit thread_local bool t_counting = false;
}  // namespace grace::host::counters

namespace {

using grace::guard::Breach;
using grace::guard::Found;
using grace::guard::kMakerFrames;
using grace::host::counters::t_counting;

constinit grace::guard::Guard g_guard;

constexpr DWORD kCheckDefaultMs = 5000;  // every few seconds, as Engine asked (D235)
constexpr DWORD kCheckLeastMs = 100;
constexpr DWORD kCheckMostMs = 600000;
constinit std::atomic<DWORD> g_check_period{kCheckDefaultMs};
constinit std::atomic<std::uint64_t> g_last_check{0};  // GetTickCount64 at the last whole check

// The first breach of the session, raised: written once, then published.
constinit std::atomic<bool> g_breach_taken{false};
constinit Found g_breach{};
constinit std::atomic<bool> g_breach_published{false};

// The harness's probes (GraceUI_Test_HeapGuardProbe): a breach on the probing thread, or in the probe's own block on
// any thread, is recorded as the probe's and not raised, unless the probe asked for the real thing.
constinit thread_local bool t_probe = false;
constinit std::atomic<std::uint64_t> g_probe_serial{0};
constinit std::atomic<bool> g_probe_raises{false};
constinit Found g_probe_found{};
constinit std::atomic<std::uint32_t> g_probe_breach{0};

constinit thread_local bool t_reporting = false;  // a breach met while one is being reported: the first is enough

// graceui.dll's image, read once at the first request (begin_guard), so that a breach takes no lock to name a frame
// (GetModuleHandleExW would take the loader's).
constinit std::atomic<std::uintptr_t> g_own_base{0};
constinit std::atomic<std::uintptr_t> g_own_end{0};

void read_own_image() noexcept {
  HMODULE module = nullptr;
  // SAFETY: the address of a function in this module, read only to find the module.
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&read_own_image), &module)) {
    return;
  }
  // SAFETY: a loaded module's handle is its image's base, which begins with its DOS header; e_lfanew gives its NT
  // headers, inside the image, whose optional header holds the image's size.
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const unsigned char*>(module) +
                                                              dos->e_lfanew);
  // SAFETY: the image's base, as a number, to compare frames with.
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  g_own_end.store(base + nt->OptionalHeader.SizeOfImage, std::memory_order_relaxed);
  g_own_base.store(base, std::memory_order_release);
}

// A frame as the log names it: graceui.dll's offset, or "another module" (no names of other programs, PV3; the
// observer's record names every frame's module by its kind).
void frame_text(const void* frame, char (&out)[40]) noexcept {
  // SAFETY: a frame's address, as a number, compared with graceui.dll's image.
  const auto address = reinterpret_cast<std::uintptr_t>(frame);
  const std::uintptr_t base = g_own_base.load(std::memory_order_acquire);
  if (frame == nullptr) {
    std::snprintf(out, sizeof out, "-");
  } else if (base != 0 && address >= base && address < g_own_end.load(std::memory_order_relaxed)) {
    std::snprintf(out, sizeof out, "graceui.dll+0x%llx", static_cast<unsigned long long>(address - base));
  } else {
    std::snprintf(out, sizeof out, "another module");
  }
}

LONG CALLBACK end_on_breach(EXCEPTION_POINTERS* pointers) {
  if (pointers->ExceptionRecord->ExceptionCode == GRACEUI_TEST_HEAP_GUARD_BREACH) {
    // Last in the list: every handler before it (the spike observer's recorder) has seen it. Nothing catches this.
    RaiseFailFastException(pointers->ExceptionRecord, pointers->ContextRecord, 0);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void on_breach(const Found& found) noexcept {
  if (found.breach == Breach::none || t_reporting) {
    return;
  }
  t_reporting = true;
  const std::uint64_t probe_serial = g_probe_serial.load(std::memory_order_acquire);
  // The probe's own block, or (on the probing thread) a pointer that was never a block: never another block's breach.
  const bool probe = !g_probe_raises.load(std::memory_order_acquire) &&
                     ((found.serial != 0 && found.serial == probe_serial) || (t_probe && found.breach == Breach::not_ours));
  if (probe) {
    if (g_probe_breach.load(std::memory_order_acquire) == 0) {
      g_probe_found = found;
      g_probe_breach.store(static_cast<std::uint32_t>(found.breach), std::memory_order_release);
    }
    t_reporting = false;
    return;
  }
  if (!g_breach_taken.exchange(true, std::memory_order_acq_rel)) {
    g_breach = found;
    g_breach_published.store(true, std::memory_order_release);
    char made[kMakerFrames][40];
    for (std::size_t i = 0; i < kMakerFrames; ++i) {
      frame_text(found.maker[i], made[i]);
    }
    grace::host::log_line("heap guard: a block %s (%llu bytes, block %llu), made at %s, called from %s, then %s",
                          grace::guard::breach_name(found.breach), static_cast<unsigned long long>(found.size),
                          static_cast<unsigned long long>(found.serial), made[0], made[1], made[2]);
  }
  const ULONG_PTR details[2] = {static_cast<ULONG_PTR>(found.breach), static_cast<ULONG_PTR>(found.size)};
  RaiseException(GRACEUI_TEST_HEAP_GUARD_BREACH, EXCEPTION_NONCONTINUABLE, 2, details);
  t_reporting = false;  // reached only if a handler resumed it, which a noncontinuable exception refuses
}

// The block's maker: `caller`, the return address of operator new (or of Dear ImGui's allocator), and the two frames
// above it on the stack. The stack is read from here and `caller` found in it, so that neither an inlined operator new
// nor a tail call into this function can shift the frames; if it is not found, `caller` alone is kept.
constexpr DWORD kFramesRead = 8;

GU_NOINLINE void* guarded_alloc(std::size_t size, const void* caller) noexcept {
  if (t_counting) {
    grace::host::counters::allocations.fetch_add(1, std::memory_order_relaxed);
  }
  void* frames[kFramesRead] = {};
  const USHORT read = RtlCaptureStackBackTrace(1, kFramesRead, frames, nullptr);
  const void* maker[kMakerFrames] = {caller, nullptr, nullptr};
  for (USHORT i = 0; i < read; ++i) {
    if (frames[i] == caller) {
      for (std::size_t k = 1; k < kMakerFrames && i + k < read; ++k) {
        maker[k] = frames[i + k];
      }
      break;
    }
  }
  const void* const (&kept)[kMakerFrames] = maker;
  return g_guard.allocate(size, kept);
}

void guarded_free(void* block) noexcept {
  if (block == nullptr) {
    return;
  }
  if (t_counting) {
    grace::host::counters::frees.fetch_add(1, std::memory_order_relaxed);
  }
  on_breach(g_guard.release(block));
}

void fill_found(const Found& found, GraceUiTestHeapFound& out) noexcept {
  out.breach = static_cast<std::uint32_t>(found.breach);
  out.size = found.size;
  out.serial = found.serial;
  for (std::size_t i = 0; i < kMakerFrames; ++i) {
    // SAFETY: a frame's address, given as a number.
    out.maker[i] = reinterpret_cast<std::uintptr_t>(found.maker[i]);
  }
}

}  // namespace

namespace grace::host::draw_alloc {

// SAFETY: Dear ImGui calls these in pairs, with a block from imgui_alloc or null; the guard does the rest.
GU_NOINLINE void* imgui_alloc(std::size_t size, void* /*user*/) noexcept {
  return guarded_alloc(size, __builtin_return_address(0));
}
void imgui_free(void* block, void* /*user*/) noexcept { guarded_free(block); }

void begin_guard() noexcept {
  wchar_t text[16] = {};
  const DWORD length = GetEnvironmentVariableW(L"GRACEUI_TEST_HEAP_CHECK_MS", text, 16);
  if (length > 0 && length < 16) {
    wchar_t* end = nullptr;
    const unsigned long ms = std::wcstoul(text, &end, 10);
    if (end != nullptr && *end == L'\0' && ms >= kCheckLeastMs && ms <= kCheckMostMs) {
      g_check_period.store(static_cast<DWORD>(ms), std::memory_order_relaxed);
    } else {
      log_line("GRACEUI_TEST_HEAP_CHECK_MS is not a number of ms from %lu to %lu: the heap guard checks every %lu ms",
               static_cast<unsigned long>(kCheckLeastMs), static_cast<unsigned long>(kCheckMostMs),
               static_cast<unsigned long>(kCheckDefaultMs));
    }
  }
  read_own_image();
  AddVectoredExceptionHandler(0, &end_on_breach);  // last in the list, made now so that a breach makes nothing
  log_line("the heap guard watches GraceUI's own blocks (a test build), every block checked every %lu ms",
           static_cast<unsigned long>(g_check_period.load(std::memory_order_relaxed)));
}

DWORD check_period_ms() noexcept { return g_check_period.load(std::memory_order_relaxed); }

void check_heap_when_due() noexcept {
  const std::uint64_t now = GetTickCount64();
  const std::uint64_t last = g_last_check.load(std::memory_order_relaxed);
  if (last != 0 && now - last < g_check_period.load(std::memory_order_relaxed)) {
    return;
  }
  on_breach(g_guard.check_all());
  g_last_check.store(GetTickCount64(), std::memory_order_relaxed);
}

void fill_heap_guard(GraceUiTestHeapGuard& out) noexcept {
  grace::guard::Stats stats;
  out.stats_read = g_guard.try_stats(stats) ? 1 : 0;  // never waits: an exception handler may be asking
  out.check_period_ms = g_check_period.load(std::memory_order_relaxed);
  out.live_blocks = stats.live_blocks;
  out.live_bytes = stats.live_bytes;
  out.quarantined_blocks = stats.quarantined_blocks;
  out.quarantined_bytes = stats.quarantined_bytes;
  out.damaged_blocks = stats.damaged_blocks;
  out.made = stats.made;
  out.checks = stats.checks;
  out.blocks_checked = stats.blocks_checked;
  out.breaches = stats.breaches;
  out.last_check_ms = g_last_check.load(std::memory_order_relaxed);
  if (g_breach_published.load(std::memory_order_acquire)) {
    fill_found(g_breach, out.breach);
  }
  if (g_probe_breach.load(std::memory_order_acquire) != 0) {
    fill_found(g_probe_found, out.probe);
  }
}

// The probe's block, made as operator new makes one: out of line, so that its maker is the probe, in graceui.dll,
// wherever the probe itself is inlined.
GU_NOINLINE void* probe_alloc() noexcept { return guarded_alloc(24, __builtin_return_address(0)); }

std::uint32_t heap_guard_probe(std::uint32_t kind, std::uint32_t raise) noexcept {
  if (kind < 1 || kind > 5) {
    return 0;
  }
  g_probe_breach.store(0, std::memory_order_release);
  g_probe_raises.store(raise != 0, std::memory_order_release);
  t_probe = true;
  // Through the guard's own functions, which operator new and delete call: the block is opaque to the compiler, which
  // could otherwise act on the writes below as on writes outside a 24-byte object.
  auto* block = static_cast<unsigned char*>(probe_alloc());
  if (block == nullptr) {
    t_probe = false;
    return 0;
  }
  g_probe_serial.store(g_guard.serial_of(block), std::memory_order_release);
  volatile unsigned char* bytes = block;  // each write is made as written
  switch (kind) {
    case 1:
      bytes[-1] = 0;  // the front guard's last byte
      guarded_free(block);
      break;
    case 2:
      bytes[24] = 0;  // the tail guard's first byte
      guarded_free(block);
      break;
    case 3:
      guarded_free(block);
      bytes[3] = 0;  // a kept block, still the guard's
      on_breach(g_guard.check_all());
      // GraceUI's thread may have found it first, its record a moment behind: waited for, briefly.
      for (int i = 0; i < 500 && g_probe_breach.load(std::memory_order_acquire) == 0; ++i) {
        Sleep(1);
      }
      break;
    case 4:
      guarded_free(block);
      guarded_free(block);
      break;
    default: {
      guarded_free(block);
      void* other = std::calloc(256, 1);  // malloc's own, never the guard's
      if (other != nullptr) {
        guarded_free(static_cast<unsigned char*>(other) + 128);
        std::free(other);
      }
      break;
    }
  }
  t_probe = false;
  g_probe_serial.store(0, std::memory_order_release);
  g_probe_raises.store(false, std::memory_order_release);
  return g_probe_breach.load(std::memory_order_acquire);
}

}  // namespace grace::host::draw_alloc

// SAFETY: graceui.dll's own replacements of the global allocation functions (its module only: the CRT is static). They
// keep the standard contract: new throws std::bad_alloc when there is no room, delete accepts null. Each gives the guard
// its own return address, its caller's place, as the block's maker.
GU_NOINLINE void* operator new(std::size_t size) {
  if (void* block = guarded_alloc(size, __builtin_return_address(0))) {
    return block;
  }
  throw std::bad_alloc();
}
GU_NOINLINE void* operator new[](std::size_t size) {
  if (void* block = guarded_alloc(size, __builtin_return_address(0))) {
    return block;
  }
  throw std::bad_alloc();
}
GU_NOINLINE void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return guarded_alloc(size, __builtin_return_address(0));
}
GU_NOINLINE void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return guarded_alloc(size, __builtin_return_address(0));
}
void operator delete(void* block) noexcept { guarded_free(block); }
void operator delete[](void* block) noexcept { guarded_free(block); }
void operator delete(void* block, std::size_t) noexcept { guarded_free(block); }
void operator delete[](void* block, std::size_t) noexcept { guarded_free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { guarded_free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { guarded_free(block); }

#endif  // GRACEUI_TEST_EXPORTS
