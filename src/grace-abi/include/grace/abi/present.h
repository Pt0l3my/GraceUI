// present.h: the opt-in on_present callbacks (D71, D259): a mod's panel drawn each frame by a callback of the mod's on
// the present thread, for what must follow every frame (EldenNet's subtitle fades and world labels). Pure: no Win32 and
// no Dear ImGui; grace-host calls the callbacks, times them and draws what they give.
//
// A hook is made on the mod's thread, with the room for the lists it will give reserved there, and is freed there only
// once the present thread can no longer hold it: the present thread counts each frame in and out (an epoch, odd while
// a frame is being drawn), and a mod's thread that took a hook out waits, if a frame is being drawn, until that frame
// ends, however long it takes, unless the present thread itself has gone (grace-host's `alive` says so, as at the
// process's exit), when the hook is left allocated rather than freed under it. So set() and remove_owner() return only
// once the old callback is not running and can never be called again, and the present thread never waits for anything.
// The budget (0.5 ms a frame for a mod's callbacks together, EST) is weighed on the present thread alone; a mod over it
// for 120 frames running, or whose callback threw, is suspended for as long as its number is its, its id kept, and
// end_frame() names it once, for grace-host to tell the player from its own thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "grace/abi/consumers.h"
#include "grace/abi/drawlist.h"
#include "grace/abi/panels.h"
#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kPresentHooksPerMod = GRACEUI_PRESENT_HOOKS_PER_MOD;  // (EST)
inline constexpr std::uint32_t kPresentHooksMax = GRACEUI_PRESENT_HOOKS_MAX;        // (EST)
inline constexpr std::int64_t kPresentBudgetUs = GRACEUI_PRESENT_BUDGET_US;          // a mod's, each frame (EST)
inline constexpr std::uint32_t kPresentOverFrames = GRACEUI_PRESENT_OVER_FRAMES;     // frames running over it
// Whether the thread drawing frames still runs (grace-host's): a mod's thread waits for a frame to end only while it
// does. Null: it is taken to run.
using PresentAlive = bool (*)() noexcept;

struct PresentHookDesc {
  GraceUiPresentCallback callback = nullptr;  // null: clear the panel's hook
  void* user = nullptr;
  std::uint32_t vertex_capacity = 0;   // 1 to kDrawVerticesMax
  std::uint32_t index_capacity = 0;    // 1 to kDrawIndicesMax
  std::uint32_t command_capacity = 0;  // 1 to kDrawCommandsMax
};

// A hook GraceUI takes: a callback, and capacities within D76's bounds of one submission.
bool valid_present_hook(const PresentHookDesc& desc) noexcept;

// One panel's hook. The present thread reads it, and writes only `list` and `last_refused`.
struct PresentHook {
  GraceUiPresentCallback callback = nullptr;
  void* user = nullptr;
  GraceUiConsumer owner = 0;
  std::uint32_t mod = 0;     // the mod's number (consumers.h), 1 to kMaxConsumers
  std::uint64_t panel = 0;   // the panel's id: a slot reused by another panel never runs this hook
  char id[GRACEUI_ID_MAX + 1] = {};  // the mod's id, for the present thread's log lines
  std::uint32_t vertex_capacity = 0;
  std::uint32_t index_capacity = 0;
  std::uint32_t command_capacity = 0;
  // What draws may name (D76), fixed when the hook is set: setting one closes the mod's registration window.
  std::uint64_t textures[GRACEUI_TEXTURES_PER_MOD] = {};
  std::uint32_t texture_count = 0;
  // The present thread's copy of the list the callback gave, reserved to the capacities when the hook was made, so
  // that copying into it never allocates (invariant 10).
  DrawListData list;
};

// A list a callback gave fits its hook: its counts within the hook's capacities (and so within D76's bounds) and its
// stride one GraceUI reads (list_shape_fits).
constexpr bool present_list_fits(const PresentHook& hook, std::uint64_t vertices, std::uint64_t indices,
                                 std::uint64_t commands, std::uint64_t command_size) noexcept {
  return vertices <= hook.vertex_capacity && indices <= hook.index_capacity && commands <= hook.command_capacity &&
         list_shape_fits(vertices, indices, commands, command_size);
}

enum class PresentSet : std::uint8_t {
  ok = 0,
  invalid = 1,    // the hook's capacities or its callback, or a panel slot out of range
  full = 2,       // kPresentHooksPerMod for the mod, or kPresentHooksMax in all
  suspended = 3,  // the mod's callbacks are suspended: it may clear a hook, not set one
};

class PresentHooks {
 public:
  PresentHooks() = default;
  ~PresentHooks();
  PresentHooks(const PresentHooks&) = delete;
  PresentHooks& operator=(const PresentHooks&) = delete;

  // ---- Mods' threads (never the present thread: grace-host refuses a call from inside a callback).

  // Sets panel slot `index`'s hook (`panel` its id, the mod's own, as grace-host found it), replacing any; or, with a
  // null callback, clears it. Allocates here; may wait for the frame being drawn to end. The hook's textures are
  // `textures` (the mod's, copied). On return the slot's earlier callback is not running and is never called again.
  PresentSet set(GraceUiConsumer owner, std::uint32_t mod, std::uint64_t panel, std::uint32_t index,
                 const PresentHookDesc& desc, const std::uint64_t* textures, std::uint32_t texture_count,
                 std::string_view id = {});
  // Every hook of `owner` cleared, as set() clears one; and, with `mod` (its number, or 0 to leave the number's state
  // alone), the number's budget and suspension forgotten, its next mod starting afresh. Answers how many, and whether
  // that connection was suspended, with its id: once a frame being drawn has ended, so none is missed.
  struct Removed {
    std::uint32_t count = 0;
    bool suspended = false;
    char id[GRACEUI_ID_MAX + 1] = {};
  };
  Removed remove_owner(GraceUiConsumer owner, std::uint32_t mod) noexcept;
  void set_alive(PresentAlive alive) noexcept { alive_.store(alive, std::memory_order_release); }

  std::uint32_t count() const noexcept;               // hooks set
  // Whether slot `index` has a hook for panel `panel` whose mod is not suspended: the present thread's question before
  // a frame (is there anything to draw), which reads no hook, only what its slot keeps beside it.
  bool wants_frame(std::uint32_t index, std::uint64_t panel) const noexcept;
  bool suspended(std::uint32_t mod) const noexcept;   // the mod's callbacks are suspended

  // ---- The present thread, one frame at a time: begin_frame(), hook() and the reports, then end_frame().

  void begin_frame() noexcept;
  // Slot `index`'s hook for this frame, if it is set for panel `panel` and its mod is not suspended; valid until
  // end_frame().
  PresentHook* hook(std::uint32_t index, std::uint64_t panel) noexcept;
  // The time `hook`'s callback took (nanoseconds, by grace-host's clock).
  void spent(const PresentHook& hook, std::int64_t ns) noexcept;
  // `hook`'s callback let an exception out: its mod is suspended at once.
  void faulted(const PresentHook& hook) noexcept;
  // Weighs each mod whose callbacks ran against the budget, and ends the frame. Answers the mods suspended in this
  // frame, as a mask of their numbers (bit mod - 1), each named once; grace-host tells the player from its own thread.
  std::uint32_t end_frame() noexcept;
  std::uint64_t frames() const noexcept { return frames_; }  // frames begun
  // What the mod's callbacks took together in the frame before this one (ns; 0 if none ran then): told to them, so
  // that a mod sees its cost as its budget weighs it.
  std::int64_t spent_before(std::uint32_t mod) const noexcept {
    return mod >= 1 && mod <= kMaxConsumers ? spent_before_[mod - 1] : 0;
  }

  // How a mod was suspended, for grace-host's line: by the budget, or by an exception; and the connection it was, so
  // that a number given to another mod since is not named in its place.
  bool suspended_by_fault(std::uint32_t mod) const noexcept;
  GraceUiConsumer suspended_owner(std::uint32_t mod) const noexcept;
  // The suspended mod's id, kept when it was suspended (false if the number is not suspended).
  bool suspended_id(std::uint32_t mod, char (&out)[GRACEUI_ID_MAX + 1]) const noexcept;

 private:
  void retire(PresentHook* old) noexcept;  // a mod's thread: waits for the frame being drawn, then frees `old`
  bool wait_for_frame() noexcept;          // ... the wait alone: false if the present thread went mid-frame
  void suspend(std::uint32_t m, const PresentHook* hook, bool by_fault) noexcept;  // the present thread's

  mutable SpinLock lock_;  // mods' threads only: set() and remove_owner()
  std::atomic<PresentHook*> slots_[kMaxPanels] = {};
  // What each slot's hook is for, written before its pointer and read without it (wants_frame).
  std::atomic<std::uint32_t> slot_mod_[kMaxPanels] = {};
  std::atomic<std::uint64_t> slot_panel_[kMaxPanels] = {};
  std::atomic<PresentAlive> alive_{nullptr};
  std::atomic<std::uint64_t> epoch_{0};  // odd while the present thread draws a frame
  std::atomic<bool> suspended_[kMaxConsumers] = {};
  std::atomic<bool> by_fault_[kMaxConsumers] = {};
  std::atomic<bool> fresh_[kMaxConsumers] = {};  // the number's mod has gone: the present thread forgets its count
  std::atomic<GraceUiConsumer> suspended_owner_[kMaxConsumers] = {};
  char suspended_id_[kMaxConsumers][GRACEUI_ID_MAX + 1] = {};  // written by the present thread before suspended_
  // The present thread's own.
  std::uint64_t frames_ = 0;
  std::int64_t spent_ns_[kMaxConsumers] = {};
  std::int64_t spent_before_[kMaxConsumers] = {};
  std::uint32_t over_[kMaxConsumers] = {};
  const PresentHook* seen_[kMaxConsumers] = {};  // a hook of the number's that ran this frame (held until its end)
  std::uint32_t ran_ = 0;        // mods whose callbacks ran this frame (bit mod - 1)
  std::uint32_t newly_ = 0;      // mods suspended this frame
};

}  // namespace grace::abi
