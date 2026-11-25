// present.cpp: the opt-in on_present callbacks' store and budget (present.h, D259).
#include "grace/abi/present.h"

#include <chrono>
#include <cstring>
#include <new>
#include <thread>

namespace grace::abi {

bool valid_present_hook(const PresentHookDesc& desc) noexcept {
  return desc.callback != nullptr && desc.vertex_capacity >= 1 && desc.vertex_capacity <= kDrawVerticesMax &&
         desc.index_capacity >= 1 && desc.index_capacity <= kDrawIndicesMax && desc.command_capacity >= 1 &&
         desc.command_capacity <= kDrawCommandsMax;
}

PresentHooks::~PresentHooks() {
  for (std::atomic<PresentHook*>& slot : slots_) {
    delete slot.exchange(nullptr);
  }
}

PresentSet PresentHooks::set(GraceUiConsumer owner, std::uint32_t mod, std::uint64_t panel, std::uint32_t index,
                             const PresentHookDesc& desc, const std::uint64_t* textures,
                             std::uint32_t texture_count, std::string_view id) {
  if (index >= kMaxPanels || mod == 0 || mod > kMaxConsumers ||
      (desc.callback != nullptr && !valid_present_hook(desc))) {
    return PresentSet::invalid;
  }
  if (desc.callback != nullptr && suspended(mod)) {
    return PresentSet::suspended;  // a suspended mod sets none, though it may clear one
  }
  PresentHook* made = nullptr;
  if (desc.callback != nullptr) {
    // Made and its room reserved here, on the mod's thread, before the lock: the present thread only copies into it.
    made = new PresentHook();
    made->callback = desc.callback;
    made->user = desc.user;
    made->owner = owner;
    made->mod = mod;
    made->panel = panel;
    for (std::size_t i = 0; i < id.size() && i < GRACEUI_ID_MAX; ++i) {
      made->id[i] = id[i];
    }
    made->vertex_capacity = desc.vertex_capacity;
    made->index_capacity = desc.index_capacity;
    made->command_capacity = desc.command_capacity;
    for (std::uint32_t i = 0; i < texture_count && i < GRACEUI_TEXTURES_PER_MOD; ++i) {
      made->textures[made->texture_count++] = textures[i];
    }
    try {
      made->list.vertices.reserve(desc.vertex_capacity);
      made->list.indices.reserve(desc.index_capacity);
      made->list.commands.reserve(desc.command_capacity);
    } catch (...) {
      delete made;
      throw;
    }
  }
  PresentHook* old = nullptr;
  {
    const SpinGuard guard(lock_);
    PresentHook* const there = slots_[index].load(std::memory_order_relaxed);
    if (made != nullptr && there == nullptr) {
      // A new hook, not a replacement: within the mod's count and the session's.
      std::uint32_t mine = 0;
      std::uint32_t all = 0;
      for (const std::atomic<PresentHook*>& slot : slots_) {
        const PresentHook* hook = slot.load(std::memory_order_relaxed);
        if (hook != nullptr) {
          ++all;
          mine += hook->owner == owner ? 1 : 0;
        }
      }
      if (mine >= kPresentHooksPerMod || all >= kPresentHooksMax) {
        delete made;
        return PresentSet::full;
      }
    }
    if (made != nullptr) {
      slot_mod_[index].store(mod, std::memory_order_relaxed);
      slot_panel_[index].store(panel, std::memory_order_relaxed);
    }
    old = slots_[index].exchange(made, std::memory_order_seq_cst);  // publishes the two above with it
  }
  if (old != nullptr) {
    retire(old);
  }
  return PresentSet::ok;
}

PresentHooks::Removed PresentHooks::remove_owner(GraceUiConsumer owner, std::uint32_t mod) noexcept {
  Removed out;
  PresentHook* removed[kMaxPanels] = {};
  {
    const SpinGuard guard(lock_);
    for (std::atomic<PresentHook*>& slot : slots_) {
      PresentHook* hook = slot.load(std::memory_order_relaxed);
      if (hook != nullptr && hook->owner == owner) {
        removed[out.count++] = slot.exchange(nullptr, std::memory_order_seq_cst);
      }
    }
  }
  // Waited for once, whether or not a hook was taken out: a frame being drawn now may still suspend the mod.
  wait_for_frame();
  for (std::uint32_t i = 0; i < out.count; ++i) {
    retire(removed[i]);  // the frame they could be held in has ended: freed at once
  }
  if (mod >= 1 && mod <= kMaxConsumers) {
    const std::uint32_t m = mod - 1;
    if (suspended_[m].load(std::memory_order_acquire) &&
        suspended_owner_[m].load(std::memory_order_relaxed) == owner) {
      out.suspended = true;
      std::memcpy(out.id, suspended_id_[m], sizeof out.id);
      out.id[GRACEUI_ID_MAX] = '\0';
    }
    // Nothing of the present thread's sets these for the mod that has gone: a frame that ran its callbacks has ended.
    // Its budget's count is the present thread's, which forgets it at its next frame's end.
    suspended_[m].store(false, std::memory_order_release);
    by_fault_[m].store(false, std::memory_order_release);
    suspended_owner_[m].store(0, std::memory_order_relaxed);
    fresh_[m].store(true, std::memory_order_release);
  }
  return out;
}

bool PresentHooks::wait_for_frame() noexcept {
  // Read after a hook was taken out of its slot (both seq_cst): a frame begun after this read finds the slot's new
  // hook, so only the frame being drawn now, if one is, can hold the old.
  const std::uint64_t epoch = epoch_.load(std::memory_order_seq_cst);
  if ((epoch & 1u) == 0) {
    return true;
  }
  for (std::uint32_t spins = 0; epoch_.load(std::memory_order_seq_cst) == epoch; ++spins) {
    if (spins < 64) {
      std::this_thread::yield();
      continue;
    }
    const PresentAlive alive = alive_.load(std::memory_order_acquire);
    if (alive != nullptr && !alive()) {
      return false;  // the present thread has gone mid-frame: its frame never ends
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

void PresentHooks::retire(PresentHook* old) noexcept {
  if (wait_for_frame()) {
    delete old;
  }
  // Else the present thread went mid-frame, perhaps holding `old`: it is left allocated, never freed under it.
}

std::uint32_t PresentHooks::count() const noexcept {
  std::uint32_t all = 0;
  for (const std::atomic<PresentHook*>& slot : slots_) {
    all += slot.load(std::memory_order_acquire) != nullptr ? 1 : 0;
  }
  return all;
}

bool PresentHooks::wants_frame(std::uint32_t index, std::uint64_t panel) const noexcept {
  if (index >= kMaxPanels || slots_[index].load(std::memory_order_acquire) == nullptr) {
    return false;
  }
  return slot_panel_[index].load(std::memory_order_relaxed) == panel &&
         !suspended(slot_mod_[index].load(std::memory_order_relaxed));
}

bool PresentHooks::suspended(std::uint32_t mod) const noexcept {
  return mod >= 1 && mod <= kMaxConsumers && suspended_[mod - 1].load(std::memory_order_acquire);
}

bool PresentHooks::suspended_by_fault(std::uint32_t mod) const noexcept {
  return mod >= 1 && mod <= kMaxConsumers && by_fault_[mod - 1].load(std::memory_order_acquire);
}

GraceUiConsumer PresentHooks::suspended_owner(std::uint32_t mod) const noexcept {
  return mod >= 1 && mod <= kMaxConsumers && suspended_[mod - 1].load(std::memory_order_acquire)
             ? suspended_owner_[mod - 1].load(std::memory_order_relaxed)
             : 0;
}

bool PresentHooks::suspended_id(std::uint32_t mod, char (&out)[GRACEUI_ID_MAX + 1]) const noexcept {
  if (!suspended(mod)) {
    return false;
  }
  std::memcpy(out, suspended_id_[mod - 1], sizeof out);
  out[GRACEUI_ID_MAX] = '\0';
  return true;
}

void PresentHooks::begin_frame() noexcept {
  epoch_.fetch_add(1, std::memory_order_seq_cst);  // odd: a frame is being drawn
  ++frames_;
  ran_ = 0;
}

PresentHook* PresentHooks::hook(std::uint32_t index, std::uint64_t panel) noexcept {
  if (index >= kMaxPanels) {
    return nullptr;
  }
  PresentHook* hook = slots_[index].load(std::memory_order_seq_cst);
  if (hook == nullptr || hook->panel != panel || hook->mod == 0 || hook->mod > kMaxConsumers ||
      suspended_[hook->mod - 1].load(std::memory_order_acquire)) {
    return nullptr;
  }
  return hook;
}

void PresentHooks::spent(const PresentHook& hook, std::int64_t ns) noexcept {
  if (hook.mod >= 1 && hook.mod <= kMaxConsumers) {
    const std::uint32_t m = hook.mod - 1;
    spent_ns_[m] += ns > 0 ? ns : 0;
    seen_[m] = &hook;
    ran_ |= 1u << m;
  }
}

void PresentHooks::suspend(std::uint32_t m, const PresentHook* hook, bool by_fault) noexcept {
  // The id and the connection first, then the flag that publishes them (release), so that a reader that sees the flag
  // sees them whole. `hook` ran in this frame, which has not ended: it is still allocated.
  if (hook != nullptr) {
    std::memcpy(suspended_id_[m], hook->id, sizeof suspended_id_[m]);
    suspended_owner_[m].store(hook->owner, std::memory_order_relaxed);
  }
  by_fault_[m].store(by_fault, std::memory_order_relaxed);
  suspended_[m].store(true, std::memory_order_release);
  newly_ |= 1u << m;
}

void PresentHooks::faulted(const PresentHook& hook) noexcept {
  if (hook.mod >= 1 && hook.mod <= kMaxConsumers && !suspended_[hook.mod - 1].load(std::memory_order_acquire)) {
    suspend(hook.mod - 1, &hook, true);
  }
}

std::uint32_t PresentHooks::end_frame() noexcept {
  for (std::uint32_t m = 0; m < kMaxConsumers; ++m) {
    const std::uint32_t bit = 1u << m;
    if (fresh_[m].load(std::memory_order_relaxed) && fresh_[m].exchange(false, std::memory_order_acquire)) {
      over_[m] = 0;  // the number's mod has gone since the last frame: a new one's run starts afresh
    }
    if ((ran_ & bit) == 0) {
      over_[m] = 0;  // a frame it did not run in ends its run of frames over (and forgets a mod that has gone)
      spent_before_[m] = 0;
      continue;
    }
    over_[m] = spent_ns_[m] > kPresentBudgetUs * 1000 ? over_[m] + 1 : 0;
    spent_before_[m] = spent_ns_[m];
    spent_ns_[m] = 0;
    if (over_[m] >= kPresentOverFrames && !suspended_[m].load(std::memory_order_acquire)) {
      suspend(m, seen_[m], false);
    }
    seen_[m] = nullptr;
  }
  const std::uint32_t newly = newly_;
  newly_ = 0;
  ran_ = 0;
  epoch_.fetch_add(1, std::memory_order_seq_cst);  // even: the frame has ended, and nothing of it is held
  return newly;
}

}  // namespace grace::abi
