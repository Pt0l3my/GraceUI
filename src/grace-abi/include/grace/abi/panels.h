// panels.h: the panels mods draw (D76): created by a connected mod on a layer, with an anchor, bounds and whether it may
// take input; hidden at first; shown and hidden by its mod; gone when its mod disconnects. Each holds its draw lists
// (drawlist.h). Pure: mods' threads create, show, hide and submit; the present thread reads each panel's state and its
// newest list, and never waits.
#pragma once

#include <atomic>
#include <cstdint>

#include "grace/abi/drawlist.h"
#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kMaxPanels = GRACEUI_PANELS_MAX;        // in all (EST)
inline constexpr std::uint32_t kPanelsPerMod = GRACEUI_PANELS_PER_MOD;  // a mod's (EST)
inline constexpr float kPanelSideMax = 16384.0f;                        // a panel's width, height and offsets

// The layers in drawing order (D76): hud < panel < (GraceUI's menu) < modal < (notices).
enum class Layer : std::uint32_t { hud = GRACEUI_LAYER_HUD, panel = GRACEUI_LAYER_PANEL, modal = GRACEUI_LAYER_MODAL };

struct PanelDesc {
  std::uint32_t layer = GRACEUI_LAYER_PANEL;
  std::uint32_t anchor = GRACEUI_ANCHOR_TOP_LEFT;
  std::uint32_t flags = 0;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// A panel's description is one GraceUI takes: a known layer, anchor and flags; finite offsets and a size of 1 to
// kPanelSideMax pixels.
bool valid_panel(const PanelDesc& desc) noexcept;

struct Rect {
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
};

// Where a panel is on a display of this size (D76): its anchor names a point of the display and the same point of the
// panel, and x and y move it inwards from the anchor's edges (from the left edge for a left anchor, from the right edge
// leftwards for a right one, from the centre rightwards and downwards for a centred one).
Rect place(const PanelDesc& desc, float display_width, float display_height) noexcept;

// A panel's id: its slot's generation above, its slot (1 to kMaxPanels) in the low byte; never 0.
constexpr std::uint64_t panel_id(std::uint32_t index, std::uint32_t generation) noexcept {
  return (static_cast<std::uint64_t>(generation) << 8) | (index + 1);
}

enum class PanelResult : std::uint8_t { ok = 0, invalid = 1, full = 2, unknown = 3 };

// What the present thread sees of one panel in a frame.
struct PanelView {
  std::uint64_t id = 0;
  GraceUiConsumer owner = 0;
  PanelDesc desc{};
  bool visible = false;
};

class Panels {
 public:
  Panels() = default;
  Panels(const Panels&) = delete;
  Panels& operator=(const Panels&) = delete;

  // Mods' threads. `owner` is the mod's handle (consumers.h): a panel answers only to the handle that made it.
  PanelResult create(GraceUiConsumer owner, const PanelDesc& desc, std::uint64_t& id) noexcept;
  PanelResult set_visible(GraceUiConsumer owner, std::uint64_t id, bool visible) noexcept;
  // The panel's description and its slot, if `owner` made it.
  PanelResult find(GraceUiConsumer owner, std::uint64_t id, PanelDesc& desc, std::uint32_t& index) const noexcept;
  // Every panel of `owner` is hidden and gone (its mod disconnected); answers how many.
  std::uint32_t remove_owner(GraceUiConsumer owner) noexcept;
  // The panel's draw lists; the writer takes writer_lock() and checks the panel is still `id`'s before it publishes.
  DrawListBuffers& lists(std::uint32_t index) noexcept { return slots_[index].lists; }
  bool alive(std::uint32_t index, std::uint64_t id) const noexcept;

  std::uint32_t count() const noexcept;    // panels that exist
  std::uint32_t visible() const noexcept;  // ... and are shown

  // The present thread: a consistent view of slot `index` (false if it holds no panel, or one being made or removed
  // at this moment, which is skipped for this frame).
  bool view(std::uint32_t index, PanelView& out) const noexcept;

 private:
  // A slot's state: its generation, odd while the slot is being made or removed (a sequence lock the reader never
  // waits on), and whether it is shown.
  struct Slot {
    std::atomic<std::uint32_t> sequence{0};  // even: stable; odd: being written
    std::atomic<std::uint32_t> generation{0};
    std::atomic<bool> alive{false};
    std::atomic<bool> visible{false};
    std::atomic<GraceUiConsumer> owner{0};
    std::atomic<std::uint32_t> layer{0};
    std::atomic<std::uint32_t> anchor{0};
    std::atomic<std::uint32_t> flags{0};
    std::atomic<float> x{0.0f};
    std::atomic<float> y{0.0f};
    std::atomic<float> width{0.0f};
    std::atomic<float> height{0.0f};
    DrawListBuffers lists;
  };

  std::uint32_t slot_of(std::uint64_t id) const noexcept;  // kMaxPanels if none

  mutable SpinLock lock_;  // mods' threads only
  Slot slots_[kMaxPanels];
};

}  // namespace grace::abi
