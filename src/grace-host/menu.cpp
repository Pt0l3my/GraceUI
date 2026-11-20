// menu.cpp: the in-game menu (menu.h; §5.1, D79, D115, D167).
//
// Three threads touch it, and none waits for another: the window procedure pushes keys and the present thread the pad's
// new presses, each into a ring of its own (one producer each); GraceUI's own thread, woken by either, pops them, runs
// the page view and GraceUI's settings store, lays the page out and tessellates it into the back list of a draw list's
// triple buffer, and publishes it; the present thread takes the newest list for the holder of its generation. The
// holder itself is grace-input's (D77): the menu asks for input as any holder does, and the one close routine ends it,
// whoever ends it; the menu's own thread learns of a close at its next wake.
#include "menu.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "draw_atlas.h"
#include "grace/abi/drawlist.h"
#include "grace/abi/registry.h"
#include "grace/draw/tessellate.h"
#include "grace/input/devices.h"
#include "grace/input/queue.h"
#include "grace/pages/settings_store.h"
#include "grace/spaces/own_space.h"
#include "grace/tokens.h"
#include "grace/view/page_view.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "session.h"
#include "spaces.h"

namespace grace::host::menu {
namespace {

using grace::input::Kind;
using grace::input::Phase;
using grace::view::Key;

// The menu is the holder of kind menu that GraceUI itself asked for (owner 0); a test's holder of that kind is not.
constexpr bool ours(const grace::input::Holder& holder) noexcept {
  return holder.kind == Kind::menu && holder.owner == 0 && holder.phase == Phase::active;
}

struct MenuInput {
  Key key = Key::down;
  std::uint8_t shift = 0;
  std::uint32_t generation = 0;  // the holder it is for
};

constinit grace::input::SpscRing<MenuInput, 64> g_keys;  // the window procedure's
constinit grace::input::SpscRing<MenuInput, 64> g_pad;   // the present thread's
constinit std::atomic<grace::abi::DrawListBuffers*> g_lists{nullptr};  // made by GraceUI's thread, never freed
constinit std::atomic<bool> g_off{false};
constinit std::atomic<std::uint32_t> g_width{0};
constinit std::atomic<std::uint32_t> g_height{0};
constinit std::atomic<float> g_ui_scale{1.0f};  // ui.scale as the store holds it, for the next bake's scale

// The test exports' view (and nothing else reads it).
constinit std::atomic<std::uint64_t> g_published{0};
constinit std::atomic<std::uint64_t> g_frames{0};
constinit std::atomic<std::uint32_t> g_inputs{0};
constinit std::atomic<std::uint32_t> g_refusals{0};
constinit std::atomic<std::uint32_t> g_layout_us{0};
constinit std::atomic<std::uint32_t> g_vertices{0};
constinit std::atomic<std::uint32_t> g_indices{0};
constinit std::atomic<std::uint32_t> g_commands{0};
constinit std::atomic<float> g_scale{0.0f};

// The present thread's: the pad's buttons as last seen for the holder, so that only a new press counts, and the press
// that was held when the menu opened does nothing until it is released (§5.2).
struct PadSeen {
  std::uint32_t generation;
  std::uint16_t buttons;
  bool left_trigger;
  bool right_trigger;
  std::int8_t stick_x;
  std::int8_t stick_y;
};
constinit PadSeen g_pad_seen{};

constexpr float kStickPress = 16384.0f;  // half the left stick's travel: a press of the d-pad (EST)

// GraceUI's thread's: the page view, the space's settings and what was laid out last.
struct State {
  bool loaded = false;
  bool open = false;
  std::uint32_t generation = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t serial = 0;
  double ui_scale = 1.0;
  bool clear = false;  // ui.menu.theme is Grace Clear
  grace::view::PageView view;
  std::uint64_t revision = 0;  // GraceUI's own space's, as the menu last took it
  grace::view::DisplayList list;
  std::string page;  // as the test exports read it, under its lock
  std::string focused;
};
constinit State* g_state = nullptr;
constinit grace::abi::SpinLock g_view_lock;  // the test exports' copy of page and focused

std::int64_t qpc() noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

std::uint32_t micros_since(std::int64_t start) noexcept {
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart > 0 ? static_cast<std::uint32_t>((qpc() - start) * 1000000 / frequency.QuadPart) : 0;
}

// GraceUI's own space (D115), shared with the browser's views through one settings store (D169).
grace::spaces::OwnSpace& own_space() {
  grace::spaces::OwnSpace* own = spaces::own();
  if (own == nullptr) {
    throw std::runtime_error("GraceUI's own space does not load");
  }
  return *own;
}

void load(State& m) {
  grace::spaces::OwnSpace& own = own_space();
  std::string why;
  if (!m.view.load(own.pages(), own.settings(), why) || !m.view.open("graceui.menu")) {
    throw std::runtime_error("GraceUI's own space does not load");
  }
  m.loaded = true;
}

// What a changed setting does at once (its effect "now"): the scale, the theme. The rest are kept for their users.
void take_effects(State& m) {
  const grace::pages::SettingsStore& store = own_space().store();
  const grace::protocol::Json& scale = store.held("ui.scale");
  m.ui_scale = scale.is_number() ? scale.get<double>() : 1.0;
  g_ui_scale.store(static_cast<float>(m.ui_scale), std::memory_order_relaxed);
  m.clear = store.held("ui.menu.theme") == "clear";
  m.view.set_values(store.values());
  m.revision = own_space().revision();
}

std::string key_of(const grace::protocol::Cmd& command) {
  const grace::protocol::Json& args = command.args;
  for (const char* field : {"key", "target"}) {
    if (args.is_object() && args.contains(field) && args[field].is_string()) {
      return args[field].get<std::string>();
    }
  }
  return {};
}

// A command from the page view: GraceUI's own space answers it here, at its one entry point (invariant 3).
void run(State& m, const grace::protocol::Cmd& command) {
  if (command.name == "menu.close") {
    input::close_holder(grace::input::Closed::released, m.generation);
    return;
  }
  if (command.name == "settings.set" || command.name == "settings.reset") {
    const grace::pages::Outcome outcome = own_space().apply(command);  // the browser's views see it too
    if (!outcome.ok) {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      m.view.refused(key_of(command), outcome.detail);
    }
    take_effects(m);
    return;
  }
  if (command.name == "dashboard.disconnect") {
    spaces::disconnect_browsers();  // a new session cookie; every browser must load the page again (§7.2 item 17)
    log_line("the menu's Disconnect: every browser's session ended");
    return;
  }
  g_refusals.fetch_add(1, std::memory_order_relaxed);
  m.view.refused(key_of(command), "Not a command of GraceUI's.");
}

void take(State& m, const MenuInput& in, bool& dirty) {
  if (in.generation != m.generation) {
    return;  // pressed for an earlier holder
  }
  const grace::view::Action action = m.view.input(grace::view::Input{in.key, in.shift != 0});
  g_inputs.fetch_add(1, std::memory_order_relaxed);
  dirty = true;
  switch (action.kind) {
    case grace::view::Action::Kind::command:
      run(m, action.command);
      break;
    case grace::view::Action::Kind::close:
      input::close_holder(grace::input::Closed::released, m.generation);  // back from the rail (§5.3)
      break;
    case grace::view::Action::Kind::opened_page:
    case grace::view::Action::Kind::none:
      break;
  }
}

void publish(State& m) {
  const grace::view::Fonts* fonts = atlas::menu_fonts();
  grace::abi::DrawListBuffers* lists = g_lists.load(std::memory_order_acquire);
  if (fonts == nullptr || lists == nullptr || m.width == 0 || m.height == 0) {
    return;
  }
  const std::int64_t started = qpc();
  const float scale = static_cast<float>(m.height) / 1080.0f * static_cast<float>(m.ui_scale);  // §3.13
  m.view.layout(grace::view::Viewport{static_cast<float>(m.width), static_cast<float>(m.height), scale}, *fonts,
                m.list);
  grace::draw::Atlas atlas{grace::abi::kAtlasTexture, 0.0f, 0.0f};
  atlas::white_pixel(atlas.white_u, atlas.white_v);
  const grace::tokens::Palette& palette = m.clear ? grace::tokens::kClear.colour : grace::tokens::kGrace.colour;
  lists->writer_lock().lock();
  grace::abi::DrawListData& out = lists->back();
  const grace::draw::Stats stats = grace::draw::tessellate(m.list, palette, atlas, out);
  out.serial = ++m.serial;
  out.panel_generation = m.generation;  // drawn only for this holder
  out.atlas_generation = atlas::generation();
  out.placed_x = 0.0f;
  out.placed_y = 0.0f;
  lists->publish();
  lists->writer_lock().unlock();
  g_published.fetch_add(1, std::memory_order_relaxed);
  g_layout_us.store(micros_since(started), std::memory_order_relaxed);
  g_vertices.store(stats.vertices, std::memory_order_relaxed);
  g_indices.store(stats.indices, std::memory_order_relaxed);
  g_commands.store(stats.commands, std::memory_order_relaxed);
  g_scale.store(scale, std::memory_order_relaxed);
  g_view_lock.lock();
  m.page.assign(m.view.page());
  m.focused = m.view.focused();
  g_view_lock.unlock();
}

void drain() noexcept {
  MenuInput in;
  while (g_keys.pop(in)) {
  }
  while (g_pad.pop(in)) {
  }
}

void serve_menu() {
  if (g_lists.load(std::memory_order_acquire) == nullptr) {
    g_lists.store(new grace::abi::DrawListBuffers(), std::memory_order_release);
  }
  if (g_state == nullptr) {
    g_state = new State();
  }
  State& m = *g_state;
  const grace::input::Holder holder = input::capture().holder();
  if (!ours(holder)) {
    if (m.open) {
      m.open = false;
      m.view.cancel_confirmation();  // the close routine's part for the menu (§5.1): changes stay, the page is kept
    }
    drain();
    return;
  }
  if (!m.loaded) {
    load(m);
    take_effects(m);
  }
  bool dirty = false;
  if (m.revision != own_space().revision()) {
    take_effects(m);  // a browser changed a setting: the menu shows it, and its scale and theme follow (D169)
    dirty = true;
  }
  if (!m.open || m.generation != holder.generation) {
    m.open = true;
    m.generation = holder.generation;
    m.view.cancel_confirmation();
    dirty = true;
  }
  MenuInput in;
  for (int i = 0; i < 64 && g_keys.pop(in); ++i) {
    take(m, in, dirty);
  }
  for (int i = 0; i < 64 && g_pad.pop(in); ++i) {
    take(m, in, dirty);
  }
  const std::uint32_t width = g_width.load(std::memory_order_relaxed);
  const std::uint32_t height = g_height.load(std::memory_order_relaxed);
  if (width != m.width || height != m.height) {
    m.width = width;
    m.height = height;
    dirty = true;
  }
  if (dirty && m.open) {
    publish(m);
  }
}

void push_pad(Key key, std::uint32_t generation, bool& any) noexcept {
  any = g_pad.push(MenuInput{key, 0, generation}) || any;
}

// The present thread: the holder's pad, its new presses as the menu's inputs (§5.4).
void pad_inputs(std::uint32_t generation) noexcept {
  grace::input::HolderPad view;
  if (!input::pads().holder_view(generation, view)) {
    return;
  }
  const grace::input::Pad& pad = view.pad;
  PadSeen now{generation, pad.buttons, pad.left_trigger > grace::input::kTriggerThreshold,
              pad.right_trigger > grace::input::kTriggerThreshold,
              static_cast<std::int8_t>(pad.left_x > kStickPress ? 1 : pad.left_x < -kStickPress ? -1 : 0),
              static_cast<std::int8_t>(pad.left_y > kStickPress ? 1 : pad.left_y < -kStickPress ? -1 : 0)};
  const PadSeen before = g_pad_seen;
  g_pad_seen = now;
  if (before.generation != generation) {
    return;  // the first look for this holder: what is held now does nothing until it is released
  }
  const std::uint16_t pressed = static_cast<std::uint16_t>(now.buttons & ~before.buttons);
  struct Map {
    std::uint16_t mask;
    Key key;
  };
  static constexpr Map kButtons[] = {
      {0x0001, Key::up},           {0x0002, Key::down},    {0x0004, Key::left},     {0x0008, Key::right},
      {0x1000, Key::activate},     {0x2000, Key::back},    {0x0100, Key::previous_tab},
      {0x0200, Key::next_tab},     {0x4000, Key::reset},
  };
  bool any = false;
  for (const Map& map : kButtons) {
    if ((pressed & map.mask) != 0) {
      push_pad(map.key, generation, any);
    }
  }
  if (now.left_trigger && !before.left_trigger) {
    push_pad(Key::previous_page, generation, any);
  }
  if (now.right_trigger && !before.right_trigger) {
    push_pad(Key::next_page, generation, any);
  }
  if (now.stick_y != before.stick_y && now.stick_y != 0) {
    push_pad(now.stick_y > 0 ? Key::up : Key::down, generation, any);
  }
  if (now.stick_x != before.stick_x && now.stick_x != 0) {
    push_pad(now.stick_x > 0 ? Key::right : Key::left, generation, any);
  }
  if (any) {
    hooks::wake();
  }
}

}  // namespace

float bake_scale(std::uint32_t height) noexcept {
  const float scale = (height > 0 ? static_cast<float>(height) : 1080.0f) / 1080.0f *
                      g_ui_scale.load(std::memory_order_relaxed);
  return scale < 0.5f ? 0.5f : scale > 4.0f ? 4.0f : scale;
}

void open_requested() noexcept {
  if (g_off.load(std::memory_order_acquire)) {
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_WARNING, session::kOwnNoticeMs,
                             "GraceUI's menu is off for this session after a fault.");
    return;
  }
  if (atlas::menu_fonts() == nullptr) {
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_NORMAL, session::kOwnNoticeMs,
                             "Not now: GraceUI's menu is not ready yet.");
    return;
  }
  const grace::input::Request request = input::request(0, Kind::menu, grace::input::want_all);
  if (request.answer == grace::input::Answer::granted) {
    hooks::wake();
    return;
  }
  if (request.answer == grace::input::Answer::not_now) {
    using grace::input::NotNow;
    const char* why = request.why == NotNow::danger      ? "Not now: you are in danger."
                      : request.why == NotNow::loading   ? "Not now: the game is loading."
                      : request.why == NotNow::game_menu ? "Not now: a game menu is open."
                                                         : "Not now: GraceUI is not drawing yet.";
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_NORMAL, session::kOwnNoticeMs, why);
  }
}

bool key_down(std::uint32_t vk, bool shift, bool ctrl) noexcept {
  Key key{};
  switch (vk) {
    case VK_UP: key = Key::up; break;
    case VK_DOWN: key = Key::down; break;
    case VK_LEFT: key = Key::left; break;
    case VK_RIGHT: key = Key::right; break;
    case VK_TAB: key = shift ? Key::previous : Key::next; break;
    case VK_RETURN:
    case VK_SPACE: key = Key::activate; break;
    case VK_ESCAPE: key = Key::back; break;
    case 'Q': key = Key::previous_tab; break;
    case 'E': key = Key::next_tab; break;
    case VK_PRIOR: key = Key::previous_page; break;
    case VK_NEXT: key = Key::next_page; break;
    case 'R': key = Key::reset; break;
    default: return false;  // search (`/`, Ctrl+F) comes with GraceUI's text field (§5.6)
  }
  if (ctrl) {
    return false;
  }
  const std::uint32_t generation = input::capture().holder().generation;
  if (g_keys.push(MenuInput{key, static_cast<std::uint8_t>(shift ? 1 : 0), generation})) {
    hooks::wake();
  }
  return true;
}

grace::abi::DrawListData* frame(std::uint32_t width, std::uint32_t height) noexcept {
  if (g_width.load(std::memory_order_relaxed) != width || g_height.load(std::memory_order_relaxed) != height) {
    g_width.store(width, std::memory_order_relaxed);
    g_height.store(height, std::memory_order_relaxed);
    hooks::wake();  // the menu lays itself out again for the new size, if it is open
  }
  const grace::input::Holder holder = input::capture().holder();
  if (!ours(holder)) {
    g_pad_seen.generation = 0;
    return nullptr;
  }
  pad_inputs(holder.generation);
  grace::abi::DrawListBuffers* lists = g_lists.load(std::memory_order_acquire);
  if (lists == nullptr) {
    return nullptr;
  }
  grace::abi::DrawListData& data = lists->read();
  if (data.serial == 0 || data.panel_generation != holder.generation || data.commands.empty()) {
    return nullptr;  // the first list for this holder is still being laid out
  }
  g_frames.fetch_add(1, std::memory_order_relaxed);
  return &data;
}

void serve() noexcept {
  if (g_off.load(std::memory_order_acquire)) {
    drain();
    return;
  }
  try {
    serve_menu();
  } catch (...) {
    g_off.store(true, std::memory_order_release);
    log_line("the menu is off for this session: an exception in its layout or its commands");
    const grace::input::Holder holder = input::capture().holder();
    if (ours(holder)) {
      input::close_holder(grace::input::Closed::fault, holder.generation);  // §5.1: a fault in the menu
    }
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_view(TestView& out) noexcept {
  out = TestView{};
  const grace::input::Holder holder = input::capture().holder();
  out.open = ours(holder) ? 1 : 0;
  out.generation = out.open != 0 ? holder.generation : 0;
  out.lists = g_published.load(std::memory_order_relaxed);
  out.frames = g_frames.load(std::memory_order_relaxed);
  out.inputs = g_inputs.load(std::memory_order_relaxed);
  out.refusals = g_refusals.load(std::memory_order_relaxed);
  out.layout_us = g_layout_us.load(std::memory_order_relaxed);
  out.vertices = g_vertices.load(std::memory_order_relaxed);
  out.indices = g_indices.load(std::memory_order_relaxed);
  out.commands = g_commands.load(std::memory_order_relaxed);
  out.off = g_off.load(std::memory_order_relaxed) ? 1 : 0;
  out.scale = g_scale.load(std::memory_order_relaxed);
  out.faces = atlas::menu_fonts() != nullptr ? static_cast<std::uint32_t>(grace::tokens::kTypeCount) : 0;
  g_view_lock.lock();
  if (g_state != nullptr) {
    std::snprintf(out.page, sizeof out.page, "%s", g_state->page.c_str());
    std::snprintf(out.focused, sizeof out.focused, "%s", g_state->focused.c_str());
  }
  g_view_lock.unlock();
}

std::uint32_t test_setting(const char* key, char* out, std::uint32_t capacity) noexcept {
  // Read on the caller's thread while GraceUI's thread may be writing: a test reads it only once GraceUI is idle.
  if (key == nullptr || out == nullptr || capacity == 0) {
    return 0;
  }
  try {
    grace::spaces::OwnSpace* own = spaces::loaded_own();
    if (own == nullptr) {
      return 0;
    }
    const grace::protocol::Json values = own->store().values();
    if (!values.contains(key)) {
      return 0;
    }
    const std::string text = values[key].dump();
    const int written = std::snprintf(out, capacity, "%s", text.c_str());
    return written < 0 ? 0 : static_cast<std::uint32_t>(written);
  } catch (...) {
    return 0;
  }
}
#endif

}  // namespace grace::host::menu
