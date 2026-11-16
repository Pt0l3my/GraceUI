// panels.cpp: the panels mods draw (panels.h).
#include "grace/abi/panels.h"

#include <cmath>

namespace grace::abi {

bool valid_panel(const PanelDesc& desc) noexcept {
  const bool layer = desc.layer == GRACEUI_LAYER_HUD || desc.layer == GRACEUI_LAYER_PANEL ||
                     desc.layer == GRACEUI_LAYER_MODAL;
  const bool anchor = desc.anchor <= GRACEUI_ANCHOR_BOTTOM_RIGHT;
  const bool flags = (desc.flags & ~std::uint32_t{GRACEUI_PANEL_TAKES_INPUT}) == 0;
  const auto offset = [](float value) { return std::isfinite(value) && std::fabs(value) <= kPanelSideMax; };
  const auto side = [](float value) { return std::isfinite(value) && value >= 1.0f && value <= kPanelSideMax; };
  return layer && anchor && flags && offset(desc.x) && offset(desc.y) && side(desc.width) && side(desc.height);
}

Rect place(const PanelDesc& desc, float display_width, float display_height) noexcept {
  // The anchor's column (0 left, 1 centre, 2 right) and row (0 top, 1 middle, 2 bottom).
  const std::uint32_t column = desc.anchor % 3;
  const std::uint32_t row = desc.anchor / 3;
  const auto start = [](std::uint32_t where, float offset, float size, float display) {
    switch (where) {
      case 0: return offset;
      case 1: return (display - size) * 0.5f + offset;
      default: return display - size - offset;
    }
  };
  Rect rect;
  rect.x0 = start(column, desc.x, desc.width, display_width);
  rect.y0 = start(row, desc.y, desc.height, display_height);
  rect.x1 = rect.x0 + desc.width;
  rect.y1 = rect.y0 + desc.height;
  return rect;
}

std::uint32_t Panels::slot_of(std::uint64_t id) const noexcept {
  const std::uint64_t index = (id & 0xFFu);
  if (index == 0 || index > kMaxPanels) {
    return kMaxPanels;
  }
  const Slot& slot = slots_[index - 1];
  const std::uint64_t generation = id >> 8;
  if (!slot.alive.load(std::memory_order_acquire) || generation != slot.generation.load(std::memory_order_relaxed)) {
    return kMaxPanels;
  }
  return static_cast<std::uint32_t>(index - 1);
}

PanelResult Panels::create(GraceUiConsumer owner, const PanelDesc& desc, std::uint64_t& id) noexcept {
  if (owner == 0 || !valid_panel(desc)) {
    return PanelResult::invalid;
  }
  const SpinGuard guard(lock_);
  std::uint32_t owned = 0;
  std::uint32_t free = kMaxPanels;
  for (std::uint32_t i = 0; i < kMaxPanels; ++i) {
    const Slot& slot = slots_[i];
    if (!slot.alive.load(std::memory_order_relaxed)) {
      free = free == kMaxPanels ? i : free;
    } else if (slot.owner.load(std::memory_order_relaxed) == owner) {
      ++owned;
    }
  }
  if (owned >= kPanelsPerMod || free == kMaxPanels) {
    return PanelResult::full;
  }
  Slot& slot = slots_[free];
  // The sequence turns odd while the slot is written: a reader that meets it, or meets it changed, skips the slot.
  slot.sequence.fetch_add(1, std::memory_order_acq_rel);
  std::atomic_thread_fence(std::memory_order_release);
  const std::uint32_t generation = slot.generation.load(std::memory_order_relaxed) + 1;
  slot.generation.store(generation, std::memory_order_relaxed);
  slot.owner.store(owner, std::memory_order_relaxed);
  slot.layer.store(desc.layer, std::memory_order_relaxed);
  slot.anchor.store(desc.anchor, std::memory_order_relaxed);
  slot.flags.store(desc.flags, std::memory_order_relaxed);
  slot.x.store(desc.x, std::memory_order_relaxed);
  slot.y.store(desc.y, std::memory_order_relaxed);
  slot.width.store(desc.width, std::memory_order_relaxed);
  slot.height.store(desc.height, std::memory_order_relaxed);
  slot.visible.store(false, std::memory_order_relaxed);  // panels start hidden (D76)
  slot.alive.store(true, std::memory_order_relaxed);
  slot.sequence.fetch_add(1, std::memory_order_release);
  id = panel_id(free, generation);
  return PanelResult::ok;
}

PanelResult Panels::set_visible(GraceUiConsumer owner, std::uint64_t id, bool visible) noexcept {
  const SpinGuard guard(lock_);
  const std::uint32_t index = slot_of(id);
  if (index == kMaxPanels || slots_[index].owner.load(std::memory_order_relaxed) != owner) {
    return PanelResult::unknown;
  }
  slots_[index].visible.store(visible, std::memory_order_release);
  return PanelResult::ok;
}

PanelResult Panels::find(GraceUiConsumer owner, std::uint64_t id, PanelDesc& desc, std::uint32_t& index) const noexcept {
  const SpinGuard guard(lock_);
  index = slot_of(id);
  if (index == kMaxPanels || slots_[index].owner.load(std::memory_order_relaxed) != owner) {
    return PanelResult::unknown;
  }
  const Slot& slot = slots_[index];
  desc.layer = slot.layer.load(std::memory_order_relaxed);
  desc.anchor = slot.anchor.load(std::memory_order_relaxed);
  desc.flags = slot.flags.load(std::memory_order_relaxed);
  desc.x = slot.x.load(std::memory_order_relaxed);
  desc.y = slot.y.load(std::memory_order_relaxed);
  desc.width = slot.width.load(std::memory_order_relaxed);
  desc.height = slot.height.load(std::memory_order_relaxed);
  return PanelResult::ok;
}

bool Panels::alive(std::uint32_t index, std::uint64_t id) const noexcept {
  const SpinGuard guard(lock_);
  return index < kMaxPanels && slot_of(id) == index;
}

std::uint32_t Panels::remove_owner(GraceUiConsumer owner) noexcept {
  const SpinGuard guard(lock_);
  std::uint32_t removed = 0;
  for (Slot& slot : slots_) {
    if (!slot.alive.load(std::memory_order_relaxed) || slot.owner.load(std::memory_order_relaxed) != owner) {
      continue;
    }
    slot.sequence.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    slot.visible.store(false, std::memory_order_relaxed);
    slot.alive.store(false, std::memory_order_relaxed);
    slot.owner.store(0, std::memory_order_relaxed);
    slot.sequence.fetch_add(1, std::memory_order_release);
    ++removed;
  }
  return removed;
}

std::uint32_t Panels::count() const noexcept {
  std::uint32_t count = 0;
  for (const Slot& slot : slots_) {
    count += slot.alive.load(std::memory_order_acquire) ? 1u : 0u;
  }
  return count;
}

std::uint32_t Panels::visible() const noexcept {
  std::uint32_t count = 0;
  for (const Slot& slot : slots_) {
    count += slot.alive.load(std::memory_order_acquire) && slot.visible.load(std::memory_order_acquire) ? 1u : 0u;
  }
  return count;
}

bool Panels::view(std::uint32_t index, PanelView& out) const noexcept {
  if (index >= kMaxPanels) {
    return false;
  }
  const Slot& slot = slots_[index];
  const std::uint32_t before = slot.sequence.load(std::memory_order_acquire);
  if ((before & 1u) != 0 || !slot.alive.load(std::memory_order_relaxed)) {
    return false;
  }
  out.owner = slot.owner.load(std::memory_order_relaxed);
  out.id = panel_id(index, slot.generation.load(std::memory_order_relaxed));
  out.desc.layer = slot.layer.load(std::memory_order_relaxed);
  out.desc.anchor = slot.anchor.load(std::memory_order_relaxed);
  out.desc.flags = slot.flags.load(std::memory_order_relaxed);
  out.desc.x = slot.x.load(std::memory_order_relaxed);
  out.desc.y = slot.y.load(std::memory_order_relaxed);
  out.desc.width = slot.width.load(std::memory_order_relaxed);
  out.desc.height = slot.height.load(std::memory_order_relaxed);
  out.visible = slot.visible.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_acquire);
  return slot.sequence.load(std::memory_order_relaxed) == before;
}

}  // namespace grace::abi
