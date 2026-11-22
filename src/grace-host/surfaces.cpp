// surfaces.cpp: S10.4.c's slots of ABI 1 behind the DLL's abi files (host.h's abi1 functions): textures and faces in
// their registration windows and budgets, glyph tables, panels and their draw lists, input through the one holder, and
// the world-state providers' claims. The rules are grace-abi's (panels.h, drawlist.h, registry.h, world.h); this file
// applies them to the session and logs, once, what a mod is refused. Nothing here reads a mod's memory: the DLL's abi
// files copy it, through the functions they pass.
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

#include "display.h"
#include "draw_atlas.h"
#include "grace/abi/drawlist.h"
#include "grace/abi/panels.h"
#include "grace/abi/registry.h"
#include "grace/abi/text.h"
#include "grace/abi/world.h"
#include "grace/host/host.h"
#include "grace/spaces/registry.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "session.h"
#include "spaces.h"
#include "state.h"

namespace grace::host::abi1 {
namespace {

// What every S10.4.c slot answers before its own work: the mod's number, or 0 with the status in `status`.
std::uint32_t admitted(GraceUiConsumer consumer, GraceUiStatus& status) noexcept {
  const std::uint32_t owner = session::consumers().owner_of(consumer);
  if (owner == 0) {
    status = GRACEUI_NOT_CONNECTED;
    return 0;
  }
  if (session::inactive() != grace::abi::Inactive::none) {
    status = GRACEUI_INACTIVE;  // the notice-only mode draws no panel and takes no input (D88)
    return 0;
  }
  status = GRACEUI_OK;
  return owner;
}

// The mod's id, for a log line or a holder's name.
void id_of(std::uint32_t owner, char (&out)[GRACEUI_ID_MAX + 1]) noexcept {
  grace::abi::ConsumerInfo info;
  if (session::consumers().info_of(owner, info)) {
    std::memcpy(out, info.id, sizeof out);
  } else {
    std::memcpy(out, "a mod", 6);
  }
}

// A refused draw list is logged once for each connection of a mod (D76).
constinit std::atomic<GraceUiConsumer> g_list_refusal_logged[grace::abi::kMaxConsumers] = {};

void log_list_refusal(GraceUiConsumer consumer, std::uint32_t owner, const char* why) noexcept {
  state::lists_refused.fetch_add(1, std::memory_order_relaxed);
  std::atomic<GraceUiConsumer>& logged = g_list_refusal_logged[(owner - 1) % grace::abi::kMaxConsumers];
  if (logged.exchange(consumer, std::memory_order_relaxed) == consumer) {
    return;
  }
  char id[GRACEUI_ID_MAX + 1];
  id_of(owner, id);
  log_line("a draw list of %s was refused: %s (logged once for this mod)", id, why);
}

// After the World changed: the input half reads the newest combined claims itself (never `flags`, which another
// provider's call may already have replaced), so whichever provider settles last decides (D143, invariant 9).
void apply_world(std::uint32_t flags) noexcept {
  input::world_changed();
  if ((flags & GRACEUI_WORLD_ANTI_CHEAT) != 0) {
    spaces::close_for_anti_cheat("a world-state provider reports an anti-cheat");  // D78, D81, §7.2 item 1
  }
}

// A holder's event as its mod reads it (D176): the pointer moved from the back buffer's pixels into the holding
// panel's own, from its top-left corner.
static_assert(static_cast<std::uint32_t>(grace::input::MailKind::key) == GRACEUI_EVENT_KEY &&
              static_cast<std::uint32_t>(grace::input::MailKind::character) == GRACEUI_EVENT_CHARACTER &&
              static_cast<std::uint32_t>(grace::input::MailKind::pointer) == GRACEUI_EVENT_POINTER &&
              static_cast<std::uint32_t>(grace::input::MailKind::ended) == GRACEUI_EVENT_INPUT_ENDED &&
              static_cast<std::uint32_t>(grace::input::MailKind::pad) == GRACEUI_EVENT_PAD);
static_assert(static_cast<std::uint32_t>(grace::input::Closed::released) == GRACEUI_CLOSED_RELEASED &&
              static_cast<std::uint32_t>(grace::input::Closed::menu_key) == GRACEUI_CLOSED_MENU_KEY &&
              static_cast<std::uint32_t>(grace::input::Closed::danger) == GRACEUI_CLOSED_DANGER &&
              static_cast<std::uint32_t>(grace::input::Closed::loading) == GRACEUI_CLOSED_LOADING &&
              static_cast<std::uint32_t>(grace::input::Closed::focus_lost) == GRACEUI_CLOSED_FOCUS_LOST &&
              static_cast<std::uint32_t>(grace::input::Closed::no_render) == GRACEUI_CLOSED_NO_RENDER &&
              static_cast<std::uint32_t>(grace::input::Closed::fault) == GRACEUI_CLOSED_FAULT &&
              static_cast<std::uint32_t>(grace::input::Closed::player) == GRACEUI_CLOSED_PLAYER);

void fill_holder_event(GraceUiConsumer consumer, const grace::input::Mail& mail, std::uint32_t dropped,
                       GraceUiEvent& out) noexcept {
  out.kind = static_cast<std::uint32_t>(mail.kind);
  out.key = mail.key;
  out.modifiers = mail.modifiers;
  out.pressed = mail.pressed;
  out.repeat = mail.repeat;
  out.code_point = mail.code_point;
  out.button = mail.button;
  out.button_event = mail.button_event;
  out.pad = mail.pad;
  out.why = mail.why;
  out.dropped = dropped;
  out.wheel_x = mail.wheel_x;
  out.wheel_y = mail.wheel_y;
  if (mail.kind != grace::input::MailKind::pointer) {
    return;
  }
  std::uint32_t generation = 0;
  std::uint64_t panel = 0;
  grace::abi::PanelDesc desc;
  std::uint32_t index = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  input::buffer_size(width, height);
  float x0 = 0.0f;
  float y0 = 0.0f;
  if (session::input_panel(generation, panel) && generation == mail.generation &&
      session::panels().find(consumer, panel, desc, index) == grace::abi::PanelResult::ok) {
    const grace::abi::Rect rect = grace::abi::place(desc, static_cast<float>(width), static_cast<float>(height));
    x0 = rect.x0;
    y0 = rect.y0;
  }
  out.x = mail.x - x0;
  out.y = mail.y - y0;
}

}  // namespace

GraceUiStatus register_texture(GraceUiConsumer consumer, std::uint32_t width, std::uint32_t height, CopyPixels copy,
                               const void* context, std::uint64_t& id) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  grace::abi::Registry& registry = session::registry();
  registry.sweep();
  std::uint32_t slot = 0;
  switch (registry.begin_texture(consumer, width, height, slot)) {
    case grace::abi::Registered::ok: break;
    case grace::abi::Registered::invalid:
      refused("register_texture", "a size of 0 or beyond 4096 pixels");
      return GRACEUI_REFUSED;
    case grace::abi::Registered::late:
      refused("register_texture", "a texture after the mod's first submit (its registration window, D76)");
      return GRACEUI_TOO_LATE;
    case grace::abi::Registered::full:
      refused("register_texture", "over the mod's texture budget or the session's");
      return GRACEUI_FULL;
  }
  try {
    std::vector<unsigned char>& pixels = registry.texture_pixels(slot);
    pixels.resize(std::size_t{width} * height * 4);
    copy(context, pixels.data());
  } catch (...) {
    registry.abort_texture(slot);
    fault("register_texture");
    return GRACEUI_FAULT;
  }
  id = registry.commit_texture(slot);
  return GRACEUI_OK;
}

GraceUiStatus register_face(GraceUiConsumer consumer, float pixel_height, bool scaled, std::uint64_t data_length,
                            std::uint64_t range_count, CopyFace copy, const void* context, std::uint32_t& id) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  if (data_length < 12 || data_length > grace::abi::kFaceBytesMax || range_count > grace::abi::kFaceRangesMax) {
    refused("register_face", "a font file of no size or over 4 MiB, or more ranges than a face takes");
    return GRACEUI_REFUSED;
  }
  grace::abi::ConsumerInfo info;
  const bool eldennet = session::consumers().info_of(owner, info) && std::string_view(info.id) == "eldennet";
  grace::abi::Registry& registry = session::registry();
  std::uint32_t slot = 0;
  switch (registry.begin_face(consumer, eldennet ? grace::abi::kFacesEldenNet : grace::abi::kFacesPerMod,
                              pixel_height, slot, scaled)) {
    case grace::abi::Registered::ok: break;
    case grace::abi::Registered::invalid:
      refused("register_face", "a size outside 6 to 128 pixels");
      return GRACEUI_REFUSED;
    case grace::abi::Registered::late:
      refused("register_face", "a face after the overlay's warm-up: the atlas is baked once (D80)");
      return GRACEUI_TOO_LATE;
    case grace::abi::Registered::full:
      refused("register_face", "over the mod's faces or the session's");
      return GRACEUI_FULL;
  }
  try {
    std::vector<unsigned char>& data = registry.face_data(slot);
    std::vector<std::uint32_t>& ranges = registry.face_ranges(slot);
    data.resize(static_cast<std::size_t>(data_length));
    ranges.assign(range_count == 0 ? 0 : static_cast<std::size_t>(range_count) * 2 + 1, 0u);
    copy(context, data.data(), range_count == 0 ? nullptr : ranges.data());
    // The checks run on GraceUI's own copy, which the mod can no longer change.
    if (!grace::abi::valid_font(data.data(), data.size()) ||
        (range_count != 0 && !grace::abi::valid_ranges(ranges.data(), range_count))) {
      registry.abort_face(slot);
      refused("register_face", "not a TrueType or OpenType file, or ranges a face does not take");
      return GRACEUI_REFUSED;
    }
    if (range_count != 0) {
      ranges.back() = 0;  // Dear ImGui's form: the pairs, then 0
    }
    // Its room in the shared atlas: the glyphs its font has in its ranges, at its size (D139 as amended): the bake can
    // then never ask Dear ImGui for more than its largest texture holds.
    const std::uint64_t glyphs =
        grace::abi::mapped_code_points(data.data(), data.size(), range_count == 0 ? nullptr : ranges.data());
    if (registry.reserve_area(slot, grace::abi::face_area(glyphs, pixel_height), eldennet) !=
        grace::abi::Registered::ok) {
      registry.abort_face(slot);
      refused("register_face", "a face larger than the room left in the shared atlas (its glyphs at its size)");
      return GRACEUI_FULL;
    }
  } catch (...) {
    registry.abort_face(slot);
    fault("register_face");
    return GRACEUI_FAULT;
  }
  id = registry.commit_face(slot);
  if (id == 0) {
    registry.abort_face(slot);
    refused("register_face", "a face after the overlay's warm-up: the atlas is baked once (D80)");
    return GRACEUI_TOO_LATE;
  }
  return GRACEUI_OK;
}

GraceUiStatus glyph_table(GraceUiConsumer consumer, std::uint32_t face_id, GraceUiGlyphTable& out) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  if (atlas::generation() == 0) {
    return GRACEUI_NOT_READY;
  }
  if (!atlas::glyph_table(face_id, out)) {
    refused("glyph_table", "a face the shared atlas does not hold");
    return GRACEUI_REFUSED;
  }
  return GRACEUI_OK;
}

GraceUiStatus create_panel(GraceUiConsumer consumer, const grace::abi::PanelDesc& desc, std::uint64_t& id) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  switch (session::panels().create(consumer, desc, id)) {
    case grace::abi::PanelResult::ok: return GRACEUI_OK;
    case grace::abi::PanelResult::full:
      refused("create_panel", "the mod's panels or the session's are as many as GraceUI takes");
      return GRACEUI_FULL;
    default:
      refused("create_panel", "an unknown layer, anchor or flag, or bounds that are not finite or 1 to 16384 pixels");
      return GRACEUI_REFUSED;
  }
}

GraceUiStatus show_panel(GraceUiConsumer consumer, std::uint64_t panel, bool visible) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  if (session::panels().set_visible(consumer, panel, visible) != grace::abi::PanelResult::ok) {
    refused(visible ? "show_panel" : "hide_panel", "a panel that is not the mod's");
    return GRACEUI_REFUSED;
  }
  // A hidden view holds no input (D77). Hidden above under the panels' lock, its holder read below: request_input
  // publishes the holder's panel and then asks under the same lock whether it is shown, so one of the two ends it.
  std::uint32_t generation = 0;
  std::uint64_t holding = 0;
  if (!visible && session::input_panel(generation, holding) && holding == panel) {
    input::close_holder(grace::input::Closed::released, generation);
  }
  return GRACEUI_OK;
}

GraceUiStatus submit(GraceUiConsumer consumer, std::uint64_t panel, const ListShape& shape, CopyList copy,
                     const void* context) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  grace::abi::Panels& panels = session::panels();
  grace::abi::PanelDesc desc;
  std::uint32_t index = 0;
  if (panels.find(consumer, panel, desc, index) != grace::abi::PanelResult::ok) {
    refused("submit", "a panel that is not the mod's");
    return GRACEUI_REFUSED;
  }
  grace::abi::Registry& registry = session::registry();
  registry.close_window(consumer);  // the mod's first submit ends its registration window (D76)
  registry.sweep();
  // The bounds, before anything is copied.
  if (!grace::abi::list_shape_fits(shape.vertex_count, shape.index_count, shape.command_count, shape.command_size)) {
    log_list_refusal(consumer, owner, grace::abi::refusal_name(grace::abi::Refusal::too_large));
    return GRACEUI_REFUSED;
  }
  if (!shape.arrays) {
    log_list_refusal(consumer, owner, "a null array for a count that is not 0");
    return GRACEUI_REFUSED;
  }
  grace::abi::DrawContext checks;
  checks.width = desc.width;
  checks.height = desc.height;
  std::uint64_t textures[grace::abi::kTexturesPerMod];
  checks.texture_count = registry.owned(consumer, textures);
  checks.textures = textures;
  checks.atlas = grace::abi::kAtlasTexture;
  checks.atlas_generation = atlas::generation();

  grace::abi::DrawListBuffers& lists = panels.lists(index);
  const grace::abi::SpinGuard guard(lists.writer_lock());
  grace::abi::DrawListData& back = lists.back();
  try {
    back.vertices.resize(shape.vertex_count);
    back.indices.resize(shape.index_count);
    back.commands.resize(shape.command_count);
    copy(context, back);
  } catch (...) {
    fault("submit");
    return GRACEUI_FAULT;
  }
  back.atlas_generation = shape.atlas_generation;
  back.panel_generation = static_cast<std::uint32_t>(panel >> 8);
  back.placed_x = 0.0f;
  back.placed_y = 0.0f;
  const grace::abi::Refusal refusal = grace::abi::check_draw_list(back, checks);
  if (refusal != grace::abi::Refusal::none) {
    log_list_refusal(consumer, owner, grace::abi::refusal_name(refusal));
    return GRACEUI_REFUSED;  // the panel keeps its last list
  }
  if (!panels.alive(index, panel)) {
    return GRACEUI_REFUSED;  // its mod disconnected meanwhile, on another thread
  }
  lists.publish();
  state::lists_submitted.fetch_add(1, std::memory_order_relaxed);
  return GRACEUI_OK;
}

GraceUiStatus request_input(GraceUiConsumer consumer, std::uint32_t kind, std::uint32_t wants, std::uint64_t panel,
                            std::uint32_t flags, GraceUiInputAnswer& out) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  grace::abi::Panels& panels = session::panels();
  grace::abi::PanelDesc desc;
  std::uint32_t index = 0;
  const bool modal = kind == GRACEUI_HOLDER_MODAL;
  if ((kind != GRACEUI_HOLDER_PANEL && !modal) || wants == 0 || (wants & ~std::uint32_t{7}) != 0 ||
      (flags & ~std::uint32_t{GRACEUI_INPUT_EVENTS}) != 0 ||
      panels.find(consumer, panel, desc, index) != grace::abi::PanelResult::ok ||
      (desc.flags & GRACEUI_PANEL_TAKES_INPUT) == 0 || modal != (desc.layer == GRACEUI_LAYER_MODAL)) {
    refused("request_input", "a request that names no panel of the mod's that may take input, or a kind, wants or "
                             "flags that do not suit it");
    return GRACEUI_REFUSED;
  }
  grace::abi::PanelView view;
  if (!panels.view(index, view) || !view.visible) {
    out.answer = GRACEUI_INPUT_NOT_NOW;
    out.why = GRACEUI_NOT_NOW_NOT_READY;  // a hidden view cannot be shown holding input
    return GRACEUI_OK;
  }
  const grace::input::Request answer =
      input::request(static_cast<std::uint16_t>(owner), modal ? grace::input::Kind::modal : grace::input::Kind::panel,
                     static_cast<std::uint8_t>(wants));
  out.answer = static_cast<std::uint32_t>(answer.answer);
  switch (answer.answer) {
    case grace::input::Answer::granted:
      session::set_input_panel(answer.holder.generation, panel);
      if ((flags & GRACEUI_INPUT_EVENTS) != 0) {
        // Its holder's events go to it from now (D176); a request that ends at once below ends them too.
        input::holder_mail()->enable(static_cast<std::uint16_t>(owner), consumer, answer.holder.generation);
        input::holder_mail_enabled(answer.holder.generation);
      }
      // The panel may have been hidden or removed (its mod's other thread, or its disconnect) between the check above
      // and the grant, and that thread then found no holder of it to end: asked again here, under the panels' lock.
      if (!panels.shown(consumer, panel)) {
        input::close_holder(grace::input::Closed::released, answer.holder.generation);
        out.answer = GRACEUI_INPUT_NOT_NOW;
        out.why = GRACEUI_NOT_NOW_NOT_READY;
      }
      break;
    case grace::input::Answer::refused: {
      out.holder_kind = static_cast<std::uint32_t>(answer.holder.kind);
      char id[GRACEUI_ID_MAX + 1];
      if (answer.holder.kind == grace::input::Kind::menu) {
        std::memcpy(id, "graceui", 8);
      } else {
        id_of(answer.holder.owner, id);
      }
      std::memcpy(out.holder, id, sizeof id < sizeof out.holder ? sizeof id : sizeof out.holder);
      out.holder[sizeof out.holder - 1] = '\0';
      break;
    }
    case grace::input::Answer::not_now:
      out.why = static_cast<std::uint32_t>(answer.why) <= GRACEUI_NOT_NOW_GAME_MENU
                    ? static_cast<std::uint32_t>(answer.why)
                    : GRACEUI_NOT_NOW_NOT_READY;
      break;
  }
  return GRACEUI_OK;
}

GraceUiStatus release_input(GraceUiConsumer consumer) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  input::release(static_cast<std::uint16_t>(owner));  // nothing if it does not hold input
  return GRACEUI_OK;
}

GraceUiStatus set_caret(GraceUiConsumer consumer, std::uint64_t panel, float x, float y, float height) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  grace::abi::PanelDesc desc;
  std::uint32_t index = 0;
  const bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(height) && height >= 0.0f &&
                      std::fabs(x) < 1.0e6f && std::fabs(y) < 1.0e6f && height < 1.0e6f;
  if (!finite || session::panels().find(consumer, panel, desc, index) != grace::abi::PanelResult::ok) {
    refused("set_caret", "a caret that is not finite, or a panel not the mod's");
    return GRACEUI_REFUSED;
  }
  if (input::capture().holder().owner != owner) {
    return GRACEUI_OK;  // only the holder's caret counts: a mod's field has none while it does not hold input
  }
  if (height <= 0.0f) {
    input::set_mod_caret(static_cast<std::uint16_t>(owner), 0, 0, 0);
    return GRACEUI_OK;
  }
  std::uint32_t width = 0;
  std::uint32_t buffer_height = 0;
  input::buffer_size(width, buffer_height);
  const grace::abi::Rect rect = grace::abi::place(desc, static_cast<float>(width), static_cast<float>(buffer_height));
  input::set_mod_caret(static_cast<std::uint16_t>(owner), static_cast<std::int32_t>(std::lround(rect.x0 + x)),
                       static_cast<std::int32_t>(std::lround(rect.y0 + y)),
                       static_cast<std::int32_t>(std::lround(height)));
  return GRACEUI_OK;
}

GraceUiStatus set_world_state(GraceUiConsumer consumer, std::uint32_t flags) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  std::uint32_t combined = 0;
  if (!session::world().set(consumer, flags, combined)) {
    refused("set_world_state", "a flag GraceUI does not know");
    return GRACEUI_REFUSED;
  }
  apply_world(combined);
  return GRACEUI_OK;
}

namespace {

// A virtual key's place on the keyboard, as DirectInput and the window's messages name it (grace::input::Key): kNoKey
// for 0, and for a virtual key that is a mouse button or has no scan code on this keyboard layout.
grace::input::Key key_of_virtual(std::uint32_t vk) noexcept {
  switch (vk) {
    case 0: return grace::input::kNoKey;
    case VK_LBUTTON: case VK_RBUTTON: case VK_MBUTTON: case VK_XBUTTON1: case VK_XBUTTON2: case VK_CANCEL:
      return grace::input::kNoKey;
    case VK_PAUSE: return 0xC5;    // DIK_PAUSE, as key_from_message names it
    case VK_NUMLOCK: return 0x45;  // DIK_NUMLOCK
    default: break;
  }
  if (vk > 0xFE) {
    return grace::input::kNoKey;
  }
  const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
  if ((scan & 0x7Fu) == 0) {
    return grace::input::kNoKey;
  }
  const bool extended = (scan & 0xFF00u) == 0xE000u || (scan & 0xFF00u) == 0xE100u;
  return static_cast<grace::input::Key>((scan & 0x7Fu) | (extended ? 0x80u : 0u));
}

// An action's id within its mod: 1 to 32 of a-z, 0-9 and '-'.
bool valid_action(std::string_view action) noexcept {
  if (action.empty() || action.size() > grace::input::kActionMax) {
    return false;
  }
  for (const char c : action) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return false;
    }
  }
  return true;
}

}  // namespace

GraceUiStatus register_hotkey(GraceUiConsumer consumer, std::uint32_t key, std::uint32_t modifiers,
                              std::uint32_t pad_chord, std::string_view action, std::string_view name,
                              std::uint32_t& id) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  if (pad_chord != 0) {
    // A mod built before ABI 1.3 meant nothing by the field's halves, and was answered not_yet: it still is (D204).
    grace::abi::ConsumerInfo info;
    if (!session::consumers().info_of(owner, info) || info.abi_minor < 3) {
      return GRACEUI_NOT_YET;
    }
  }
  const grace::input::Key place = key_of_virtual(key);
  if (!valid_action(action) || !grace::abi::valid_name(name) || (key != 0 && place == grace::input::kNoKey) ||
      modifiers > 0xFFu) {
    refused("register_hotkey", "an action that is not an id, a name that is not a display name, or a key with no "
                               "place on the keyboard");
    return GRACEUI_REFUSED;
  }
  std::uint32_t added_id = 0;
  grace::input::Binding binding;
  const grace::input::Added added = input::hotkeys().add(static_cast<std::uint16_t>(owner), action, place,
                                                         static_cast<std::uint8_t>(modifiers), pad_chord,
                                                         input::menu_key(), added_id, &binding, name);
  switch (added) {
    case grace::input::Added::bound:
      break;
    case grace::input::Added::unbound:
      if ((key != 0 && !binding.key) || (pad_chord != 0 && !binding.chord)) {
        char mod_id[GRACEUI_ID_MAX + 1];
        id_of(owner, mod_id);
        const char* what = key != 0 && !binding.key && pad_chord != 0 && !binding.chord ? "its default key and chord are"
                           : key != 0 && !binding.key                                    ? "its default key is"
                                                                                         : "its default chord is";
        log_line("%s's hotkey %.*s is unbound: %s taken", mod_id, static_cast<int>(action.size()), action.data(), what);
      }
      break;
    case grace::input::Added::invalid:
      refused("register_hotkey", "a reserved key (Escape, a modifier, Alt+F4), an unknown modifier, a pad chord that "
                                 "is not two different buttons, or an action the mod has already");
      return GRACEUI_REFUSED;
    case grace::input::Added::full:
      return GRACEUI_FULL;
  }
  if (session::consumers().owner_of(consumer) != owner) {
    // The mod disconnected meanwhile, on another thread, and its clean-up may have run before the hotkey was added.
    input::hotkeys().remove(added_id);
    return GRACEUI_NOT_CONNECTED;
  }
  id = added_id;
  hooks::wake();  // the Hotkeys page lists it (D206)
  return added == grace::input::Added::unbound ? GRACEUI_UNBOUND : GRACEUI_OK;  // D164: the mod learns which
}

void display_at_connect(GraceUiConnectAnswer& out) noexcept {
  const display::Snapshot now = display::read();
  out.ui_scale = now.ui_scale;
  out.ui_scale_default = now.ui_scale_default;
  out.display_width = now.width;
  out.display_height = now.height;
}

GraceUiStatus poll(GraceUiConsumer consumer, GraceUiEvent& out, bool protocol, bool holder, bool display_ok) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  try {
    grace::input::Mail mail;
    std::uint32_t dropped = 0;
    if (holder && input::holder_mail()->take(static_cast<std::uint16_t>(owner), consumer, mail, dropped)) {
      fill_holder_event(consumer, mail, dropped, out);
      return GRACEUI_OK;
    }
    // The display, once a revision (D180): after warm-up, and after each change of its size or ui.scale.
    if (display::Snapshot now; display_ok && display::take(owner, now)) {
      out.kind = GRACEUI_EVENT_DISPLAY;
      out.display_width = now.width;
      out.display_height = now.height;
      out.ui_scale = now.ui_scale;
      out.ui_scale_default = now.ui_scale_default;
      out.scale = now.scale;
      out.content_scale = now.content_scale;
      out.faces_scale = session::registry().scaled_faces_of(consumer) ? now.faces_scale : 0.0f;
      return GRACEUI_OK;
    }
  } catch (...) {
    fault("poll");
    return GRACEUI_FAULT;
  }
  grace::input::Press press;
  if (input::hotkeys().take(static_cast<std::uint16_t>(owner), press)) {
    out.kind = GRACEUI_EVENT_HOTKEY;
    out.hotkey_id = press.id;
    out.count = press.count;
    return GRACEUI_OK;
  }
  out.kind = GRACEUI_EVENT_NONE;
  if (!protocol) {
    return GRACEUI_OK;  // a 0.1 mod's event: hotkeys alone
  }
  try {
    grace::spaces::EventKind kind = grace::spaces::EventKind::none;
    std::uint64_t reply = 0;
    std::uint64_t length = 0;
    const grace::spaces::Status polled =
        spaces::registry().poll(consumer, out.frame, out.frame_capacity, kind, reply, length);
    out.kind = static_cast<std::uint32_t>(kind);
    out.reply = reply;
    out.frame_length = length;
    return polled == grace::spaces::Status::full ? GRACEUI_FULL : GRACEUI_OK;
  } catch (...) {
    fault("poll");
    return GRACEUI_FAULT;
  }
}

namespace {

GraceUiStatus status_of(grace::spaces::Status status) noexcept {
  switch (status) {
    case grace::spaces::Status::ok: return GRACEUI_OK;
    case grace::spaces::Status::duplicate: return GRACEUI_DUPLICATE_ID;
    case grace::spaces::Status::full: return GRACEUI_FULL;
    case grace::spaces::Status::refused: break;
  }
  return GRACEUI_REFUSED;
}

}  // namespace

GraceUiStatus register_pages(GraceUiConsumer consumer, std::uint32_t views, std::uint32_t version,
                             std::string_view pages, std::string_view settings, std::string_view increment) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  const std::uint32_t owner = admitted(consumer, status);
  if (owner == 0) {
    return status;
  }
  if (session::notice_only()) {
    return GRACEUI_INACTIVE;  // no server beside an anti-cheat (D81), and no menu
  }
  try {
    char mod_id[GRACEUI_ID_MAX + 1];
    id_of(owner, mod_id);
    grace::spaces::Registration registration;
    registration.views = views;
    registration.version = std::to_string(GRACEUI_VERSION_MAJOR(version)) + "." +
                           std::to_string(GRACEUI_VERSION_MINOR(version)) + "." +
                           std::to_string(GRACEUI_VERSION_PATCH(version));
    registration.increment = std::string(increment);
    registration.pages = pages;
    registration.settings = settings;
    std::string why;
    const grace::spaces::Status added = spaces::registry().add(consumer, mod_id, registration, why);
    if (added != grace::spaces::Status::ok) {
      log_line("%s's register_pages refused: %s", mod_id, why.c_str());
      return status_of(added);
    }
    if (session::consumers().owner_of(consumer) != owner) {
      spaces::registry().remove_owner(consumer);  // the mod disconnected meanwhile, on another thread
      return GRACEUI_NOT_CONNECTED;
    }
    log_line("%s registered its space (%s%s)", mod_id, (views & GRACEUI_VIEW_GAME) != 0 ? "the game's menu" : "",
             (views & GRACEUI_VIEW_BROWSER) != 0 ? ((views & GRACEUI_VIEW_GAME) != 0 ? " and the browser" : "the browser")
                                                 : "");
    hooks::wake();  // GraceUI's own thread starts the browser view's server, if it is wanted now
    return GRACEUI_OK;
  } catch (...) {
    fault("register_pages");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus publish(GraceUiConsumer consumer, std::string_view frame) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  try {
    std::string why;
    const grace::spaces::Status published = spaces::registry().publish(consumer, frame, why);
    if (published != grace::spaces::Status::ok) {
      refused("publish", "a frame that is not a state or rows of the mod's own space, of a topic it declares");
    }
    return status_of(published);
  } catch (...) {
    fault("publish");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus answer(GraceUiConsumer consumer, std::uint64_t reply, std::string_view frame) noexcept {
  GraceUiStatus status = GRACEUI_OK;
  if (admitted(consumer, status) == 0) {
    return status;
  }
  try {
    std::string why;
    const grace::spaces::Status answered = spaces::registry().answer(consumer, reply, frame, why);
    if (answered != grace::spaces::Status::ok) {
      refused("answer", "not a result frame, or a reply that waits for no answer");
    }
    return status_of(answered);
  } catch (...) {
    fault("answer");
    return GRACEUI_FAULT;
  }
}

void forget(GraceUiConsumer consumer, std::uint32_t owner) noexcept {
  // The panels first, under their lock, then the holder: a request_input of the mod's on another thread either sees
  // its panel gone after its grant (and ends its own holder) or was granted before this reads the holder.
  session::panels().remove_owner(consumer);
  const grace::input::Holder holder = input::capture().holder();
  if (holder.holds() && holder.owner == owner && holder.kind != grace::input::Kind::menu) {
    input::close_holder(grace::input::Closed::released, holder.generation);
  }
  session::registry().retire_owner(consumer);
  spaces::registry().remove_owner(consumer);  // its space leaves the browser view, its commands' replies dropped
  input::hotkeys().remove_owner(static_cast<std::uint16_t>(owner));
  apply_world(session::world().remove(consumer));
}

}  // namespace grace::host::abi1
