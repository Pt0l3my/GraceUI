// input.cpp: the input half's shared state and its present-thread side (input.h). The rules are grace-input's; the
// window procedure's filter and the input detours that feed them are hooks_input.cpp's.
#include "input.h"

#include <algorithm>
#include <atomic>

#include "counters.h"
#include "grace/abi/world.h"
#include "grace/input/queue.h"
#include "hooks.h"
#include "imgui.h"
#include "imgui_internal.h"  // ImGuiContext's input queues: emptied between holders and reserved at warm-up
#include "log.h"
#include "menu.h"
#include "session.h"
#include "state.h"
#include "wake.h"

// Dear ImGui's Win32 backend maps a key message to an ImGuiKey with this function, which it leaves non-static for
// others to use; GraceUI calls it on the present thread with the virtual key and the scan code the message carried.
ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM wParam, LPARAM lParam);

namespace grace::host::input {
namespace {

using grace::input::Holder;
using grace::input::Kind;
using grace::input::Phase;

constinit grace::input::Capture g_capture;
constinit grace::input::KeyboardLedger g_keyboard;
constinit grace::input::MouseLedger g_mouse;
constinit grace::input::PadLedger g_pads;
constinit grace::input::Hotkeys g_hotkeys;
constinit grace::input::PadChords g_chords;
constinit grace::input::KeptBindings g_kept_bindings;
constinit grace::input::MessageRules g_messages{g_capture, g_keyboard, g_mouse, &g_hotkeys};
constinit grace::input::SpscRing<HolderEvent, 256> g_events;

// The game's window and what the filter learns of it.
constinit std::atomic<HWND> g_window{nullptr};
constinit std::atomic<bool> g_filtered{false};
constinit std::atomic<bool> g_unicode{true};
constinit std::atomic<std::int32_t> g_client_width{0};
constinit std::atomic<std::int32_t> g_client_height{0};
constinit std::atomic<std::uint32_t> g_keyboard_code_page{0};
constinit std::atomic<std::uint32_t> g_forced_code_page{0};

// The back buffer's size, as the render path last drew it (for the caret's place in client pixels).
constinit std::atomic<std::uint32_t> g_buffer_width{0};
constinit std::atomic<std::uint32_t> g_buffer_height{0};

// The holder's caret, in back-buffer pixels: height 0 while none is reported.
constinit std::atomic<std::int32_t> g_caret_x{0};
constinit std::atomic<std::int32_t> g_caret_y{0};
constinit std::atomic<std::int32_t> g_caret_height{0};
constinit std::atomic<std::uint16_t> g_caret_owner{0};  // the mod whose caret it is (D176); 0: GraceUI's own or a test's
constinit std::atomic<grace::input::HolderMail*> g_mail{nullptr};
constinit std::atomic<float> g_pointer_x{0.0f};  // the pointer's last position, in the back buffer's pixels (D176)
constinit std::atomic<float> g_pointer_y{0.0f};

// The mouse's motion while a holder has it (D232): counts gathered by source on the threads that see them, taken by the
// present thread once a frame; when each source last gave some; and the pointer they move, in client pixels (the
// present thread's alone; -1 until a holder's start or a position places it).
constinit std::atomic<std::int32_t> g_motion_raw_x{0};
constinit std::atomic<std::int32_t> g_motion_raw_y{0};
constinit std::atomic<std::int32_t> g_motion_dinput_x{0};
constinit std::atomic<std::int32_t> g_motion_dinput_y{0};
constinit std::atomic<std::int64_t> g_motion_raw_at{-1000000};
constinit std::atomic<std::int64_t> g_motion_dinput_at{-1000000};
constexpr std::int64_t kMotionLiveMs = 1000;  // which source to read: raw input's, if it came within this, else DirectInput's
// The holder whose pointer the mouse's motion drives (D236): set by its first motion and kept for as long as it holds,
// since a game that recentres its cursor sends WM_MOUSEMOVE at its centre whenever it sets it, still or not; 0 for none.
constinit std::atomic<std::uint32_t> g_motion_holder{0};

constinit std::atomic<bool> g_menu_away{false};        // G40: let go at the focus's loss, kept for its return
constinit std::atomic<std::int64_t> g_menu_return{0};  // when the focus came back while away; 0 while it has not
constexpr std::int64_t kMenuReturnMs = 5000;           // the frames the menu waits for to take input again (EST)
float g_motion_x = -1.0f;
float g_motion_y = -1.0f;

// The world-state providers' claims (D78) are read where they are kept, the session's World, whose combined word is
// written under its lock: never a copy, so that the last provider to report decides, whatever order their threads run
// in (D143). A test build's own claims are or-ed in.
#if defined(GRACEUI_TEST_EXPORTS)
constinit std::atomic<std::uint32_t> g_test_world{0};
#endif

constinit std::atomic<bool> g_off{false};
constinit std::atomic<std::uint32_t> g_last_granted{0};

// The cursor's confinement before a holder took the mouse (§5.2), put back at the close. Set only after the cursor is
// confined, so that a close that finds it unset leaves the granting thread to undo its own confinement.
constinit std::atomic<bool> g_clipped{false};
constinit std::atomic<std::int32_t> g_saved_clip[4]{};

// The present thread's: what it last fed Dear ImGui, and the holder's modifier keys as Dear ImGui last heard them (one
// bit each: left and right Shift, Ctrl, Alt and the Windows keys, in that order).
struct Fed {
  std::uint32_t generation;
  bool active;
  std::uint8_t modifiers;
};
constinit Fed g_fed{};

constexpr int kEventsPerFrame = 64;    // the holder's events handed to Dear ImGui in one frame at most
constexpr int kImGuiQueueLimit = 128;  // ... while Dear ImGui's own queue holds fewer than this (it keeps what it
                                       // defers, one change of a key or button per frame, D107)
constexpr int kImGuiReserve = 256;     // its queues' room, reserved at warm-up: the two limits above stay within it

const char* kind_name(Kind kind) noexcept {
  switch (kind) {
    case Kind::menu: return "the menu";
    case Kind::panel: return "a panel";
    case Kind::modal: return "a modal";
    case Kind::none: break;
  }
  return "nobody";
}

const char* closed_name(grace::input::Closed why) noexcept {
  using grace::input::Closed;
  switch (why) {
    case Closed::released: return "released";
    case Closed::menu_key: return "the menu key";
    case Closed::danger: return "danger";
    case Closed::loading: return "loading";
    case Closed::focus_lost: return "focus lost";
    case Closed::no_render: return "2 s without a frame";
    case Closed::fault: return "a fault";
    case Closed::player: return "the player's exit";
    case Closed::none: break;
  }
  return "none";
}

// Confines the cursor to the window's client area while a holder has the mouse (§5.2), keeping the clip it replaces.
// Answers whether it confined it.
bool confine_cursor(bool first) noexcept {
  const HWND target = g_window.load(std::memory_order_acquire);
  RECT client{};
  if (target == nullptr || !GetClientRect(target, &client)) {
    return false;
  }
  POINT corners[2] = {{client.left, client.top}, {client.right, client.bottom}};
  if (!ClientToScreen(target, &corners[0]) || !ClientToScreen(target, &corners[1])) {
    return false;
  }
  if (first) {
    RECT saved{};
    if (!GetClipCursor(&saved)) {
      return false;
    }
    g_saved_clip[0].store(saved.left, std::memory_order_relaxed);
    g_saved_clip[1].store(saved.top, std::memory_order_relaxed);
    g_saved_clip[2].store(saved.right, std::memory_order_relaxed);
    g_saved_clip[3].store(saved.bottom, std::memory_order_relaxed);
  }
  const RECT clip{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
  ClipCursor(&clip);
  if (first) {
    g_clipped.store(true, std::memory_order_seq_cst);
  }
  return true;
}

void restore_cursor() noexcept {
  if (!g_clipped.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  const RECT saved{g_saved_clip[0].load(std::memory_order_relaxed), g_saved_clip[1].load(std::memory_order_relaxed),
                   g_saved_clip[2].load(std::memory_order_relaxed), g_saved_clip[3].load(std::memory_order_relaxed)};
  ClipCursor(&saved);
}

bool ready() noexcept {
  const std::int64_t last = g_capture.last_render();
  return g_filtered.load(std::memory_order_acquire) && !g_off.load(std::memory_order_acquire) &&
         state::bound.load(std::memory_order_relaxed) && state::renderer_ready.load(std::memory_order_relaxed) &&
         state::atlas_ready.load(std::memory_order_relaxed) && !state::render_off.load(std::memory_order_relaxed) &&
         last != 0 && now_ms() - last <= grace::input::kNoRenderMs;
}

std::uint32_t world_flags() noexcept {
  std::uint32_t flags = session::world().combined();
#if defined(GRACEUI_TEST_EXPORTS)
  flags |= g_test_world.load(std::memory_order_acquire);
#endif
  return flags;
}

// The holder's conditions as they are now (grace::input::ReadConditions): read before a grant and again after it, and
// by every writer's settle() (D129, D143).
grace::input::Conditions read_conditions(const void*) noexcept {
  const std::uint32_t flags = world_flags();
  grace::input::Conditions conditions;
  conditions.ready = ready();
  conditions.danger = (flags & GRACEUI_WORLD_DANGER) != 0;
  conditions.loading = (flags & GRACEUI_WORLD_LOADING) != 0;
  conditions.game_menu = (flags & GRACEUI_WORLD_GAME_MENU) != 0;
  conditions.off = g_off.load(std::memory_order_acquire) || state::render_off.load(std::memory_order_acquire);
  return conditions;
}

// ---- Feeding Dear ImGui (present thread).

// Dear ImGui forgets the last holder's input: its own queue emptied (keeping its room), every key and button released.
void forget_input(ImGuiIO& io) noexcept {
  ImGui::GetCurrentContext()->InputEventsQueue.resize(0);
  io.ClearInputKeys();
  io.ClearInputMouse();
  g_fed = Fed{};
}

ImGuiKey imgui_key(const HolderEvent& event) noexcept {
  switch (event.key) {
    case 0x2A: return ImGuiKey_LeftShift;
    case 0x36: return ImGuiKey_RightShift;
    case 0x1D: return ImGuiKey_LeftCtrl;
    case 0x9D: return ImGuiKey_RightCtrl;
    case 0x38: return ImGuiKey_LeftAlt;
    case 0xB8: return ImGuiKey_RightAlt;
    case 0xDB: return ImGuiKey_LeftSuper;
    case 0xDC: return ImGuiKey_RightSuper;
    default: break;
  }
  const LPARAM lparam = (LPARAM{event.key & 0x7F} << 16) | ((event.key & 0x80) != 0 ? LPARAM{1} << 24 : 0);
  return ImGui_ImplWin32_KeyEventToImGuiKey(static_cast<WPARAM>(event.vk), lparam);
}

// A modifier's state as Dear ImGui must hear it (ImGuiMod_*), from the left and right keys the holder saw: GetKeyState
// would answer for the present thread, which need not be the window's.
void feed_modifier(ImGuiIO& io, ImGuiKey key, bool down) noexcept {
  unsigned bit = 0;
  ImGuiKey mod = ImGuiKey_None;
  switch (key) {
    case ImGuiKey_LeftShift: bit = 0; mod = ImGuiMod_Shift; break;
    case ImGuiKey_RightShift: bit = 1; mod = ImGuiMod_Shift; break;
    case ImGuiKey_LeftCtrl: bit = 2; mod = ImGuiMod_Ctrl; break;
    case ImGuiKey_RightCtrl: bit = 3; mod = ImGuiMod_Ctrl; break;
    case ImGuiKey_LeftAlt: bit = 4; mod = ImGuiMod_Alt; break;
    case ImGuiKey_RightAlt: bit = 5; mod = ImGuiMod_Alt; break;
    case ImGuiKey_LeftSuper: bit = 6; mod = ImGuiMod_Super; break;
    case ImGuiKey_RightSuper: bit = 7; mod = ImGuiMod_Super; break;
    default: return;
  }
  const unsigned modifiers = down ? g_fed.modifiers | (1u << bit) : g_fed.modifiers & ~(1u << bit);
  g_fed.modifiers = static_cast<std::uint8_t>(modifiers);
  io.AddKeyEvent(mod, (modifiers & (3u << (bit & ~1u))) != 0);
}

float scaled(std::int32_t value, std::uint32_t buffer, std::int32_t client) noexcept {
  return client > 0 ? static_cast<float>(value) * static_cast<float>(buffer) / static_cast<float>(client)
                    : static_cast<float>(value);
}

void feed(ImGuiIO& io, const HolderEvent& event, std::uint32_t width, std::uint32_t height) noexcept {
  switch (event.type) {
    case HolderEvent::Type::key: {
      const ImGuiKey key = imgui_key(event);
      if (key == ImGuiKey_None) {
        return;
      }
      feed_modifier(io, key, event.down != 0);
      io.AddKeyEvent(key, event.down != 0);
      return;
    }
    case HolderEvent::Type::character:
      io.AddInputCharacter(static_cast<unsigned int>(event.a));
      return;
    case HolderEvent::Type::mouse_move:
      io.AddMousePosEvent(scaled(event.a, width, g_client_width.load(std::memory_order_relaxed)),
                          scaled(event.b, height, g_client_height.load(std::memory_order_relaxed)));
      return;
    case HolderEvent::Type::mouse_button:
      if (event.key < ImGuiMouseButton_COUNT) {
        io.AddMouseButtonEvent(event.key, event.down != 0);
      }
      return;
    case HolderEvent::Type::wheel:
      io.AddMouseWheelEvent(-static_cast<float>(event.b) / 120.0f, static_cast<float>(event.a) / 120.0f);
      return;
    case HolderEvent::Type::none: return;
  }
}

// The menu's own mouse (§5.5): the menu holder's events, in the pixels Dear ImGui is fed (the Grace pointer's), to
// GraceUI's thread. Its last position, for a button or the wheel, which carry none: the present thread's.
constinit float g_menu_x = -1.0f;
constinit float g_menu_y = -1.0f;

bool menu_holds(const Holder& holder) noexcept { return holder.kind == Kind::menu && holder.owner == 0; }

void to_menu(const HolderEvent& event, std::uint32_t width, std::uint32_t height) noexcept {
  switch (event.type) {
    case HolderEvent::Type::mouse_move:
      g_menu_x = scaled(event.a, width, g_client_width.load(std::memory_order_relaxed));
      g_menu_y = scaled(event.b, height, g_client_height.load(std::memory_order_relaxed));
      menu::pointer(0, g_menu_x, g_menu_y, 0.0f, event.generation);
      return;
    case HolderEvent::Type::mouse_button:
      if (event.key == 0 && g_menu_x >= 0.0f) {  // the left button; the others do nothing in the menu
        menu::pointer(event.down != 0 ? 1 : 2, g_menu_x, g_menu_y, 0.0f, event.generation);
      }
      return;
    case HolderEvent::Type::wheel:
      if (event.a != 0 && g_menu_x >= 0.0f) {
        menu::pointer(3, g_menu_x, g_menu_y, static_cast<float>(event.a) / 120.0f, event.generation);
      }
      return;
    case HolderEvent::Type::key:
    case HolderEvent::Type::character:
    case HolderEvent::Type::none:
      return;
  }
}

// The holder's pad, as XInput's detour last saw it with the opening press held back, into Dear ImGui's pad keys (its
// Win32 backend's own gamepad code is compiled out, §5.4).
void feed_pad(ImGuiIO& io, std::uint32_t generation) noexcept {
  grace::input::HolderPad view;
  if (!g_pads.holder_view(generation, view)) {
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    return;
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const grace::input::Pad& pad = view.pad;
  const auto button = [&](ImGuiKey key, std::uint16_t mask) { io.AddKeyEvent(key, (pad.buttons & mask) != 0); };
  const auto analog = [&](ImGuiKey key, float value, float rest, float full) {
    float amount = (value - rest) / (full - rest);
    amount = amount < 0.0f ? 0.0f : amount > 1.0f ? 1.0f : amount;
    io.AddKeyAnalogEvent(key, amount > 0.10f, amount);
  };
  button(ImGuiKey_GamepadStart, 0x0010);
  button(ImGuiKey_GamepadBack, 0x0020);
  button(ImGuiKey_GamepadFaceLeft, 0x4000);   // X
  button(ImGuiKey_GamepadFaceRight, 0x2000);  // B
  button(ImGuiKey_GamepadFaceUp, 0x8000);     // Y
  button(ImGuiKey_GamepadFaceDown, 0x1000);   // A
  button(ImGuiKey_GamepadDpadLeft, 0x0004);
  button(ImGuiKey_GamepadDpadRight, 0x0008);
  button(ImGuiKey_GamepadDpadUp, 0x0001);
  button(ImGuiKey_GamepadDpadDown, 0x0002);
  button(ImGuiKey_GamepadL1, 0x0100);
  button(ImGuiKey_GamepadR1, 0x0200);
  button(ImGuiKey_GamepadL3, 0x0040);
  button(ImGuiKey_GamepadR3, 0x0080);
  const float threshold = grace::input::kTriggerThreshold;
  analog(ImGuiKey_GamepadL2, pad.left_trigger, threshold, 255.0f);
  analog(ImGuiKey_GamepadR2, pad.right_trigger, threshold, 255.0f);
  const float left = grace::input::kLeftStickDeadzone;
  const float right = grace::input::kRightStickDeadzone;
  analog(ImGuiKey_GamepadLStickLeft, pad.left_x, -left, -32768.0f);
  analog(ImGuiKey_GamepadLStickRight, pad.left_x, left, 32767.0f);
  analog(ImGuiKey_GamepadLStickUp, pad.left_y, left, 32767.0f);
  analog(ImGuiKey_GamepadLStickDown, pad.left_y, -left, -32768.0f);
  analog(ImGuiKey_GamepadRStickLeft, pad.right_x, -right, -32768.0f);
  analog(ImGuiKey_GamepadRStickRight, pad.right_x, right, 32767.0f);
  analog(ImGuiKey_GamepadRStickUp, pad.right_y, right, 32767.0f);
  analog(ImGuiKey_GamepadRStickDown, pad.right_y, -right, -32768.0f);
}

}  // namespace

grace::input::Capture& capture() noexcept { return g_capture; }
grace::input::KeyboardLedger& keyboard() noexcept { return g_keyboard; }
grace::input::MouseLedger& mouse() noexcept { return g_mouse; }
grace::input::PadLedger& pads() noexcept { return g_pads; }
grace::input::MessageRules& messages() noexcept { return g_messages; }
grace::input::Hotkeys& hotkeys() noexcept { return g_hotkeys; }
grace::input::PadChords& chords() noexcept { return g_chords; }

grace::input::Aside chord_aside() noexcept {
  if (g_capture.holder().holds()) {
    return grace::input::Aside::holder;
  }
  // Exactly when request_input would answer not_now (D204, proposal 0005 §3), or in the notice-only mode.
  const grace::input::Conditions now = read_conditions(nullptr);
  if (!now.ready || now.off || now.danger || now.loading || now.game_menu || session::notice_only()) {
    return grace::input::Aside::not_now;
  }
  return grace::input::Aside::none;
}

grace::input::KeyBits hidden_keys() noexcept {
  grace::input::KeyBits hidden = g_hotkeys.bare() | g_hotkeys.held();
  hidden.set(menu_key());
  return hidden;
}

std::int64_t now_ms() noexcept { return static_cast<std::int64_t>(GetTickCount64()); }

grace::input::Key menu_key() noexcept { return g_messages.menu_key(); }

void set_menu_key(grace::input::Key key) noexcept { g_messages.set_menu_key(key); }

grace::input::KeptBindings& kept_bindings() noexcept { return g_kept_bindings; }

namespace {

// The holder's event as its mod gets it (D176): a pointer's position in the back buffer's pixels, the wheel in notches.
grace::input::Mail mail_of(const HolderEvent& event) noexcept {
  grace::input::Mail mail;
  mail.generation = event.generation;
  mail.x = g_pointer_x.load(std::memory_order_relaxed);
  mail.y = g_pointer_y.load(std::memory_order_relaxed);
  switch (event.type) {
    case HolderEvent::Type::key:
      mail.kind = grace::input::MailKind::key;
      mail.key = event.vk;
      mail.modifiers = event.extra & 0x7u;
      mail.pressed = event.down;
      mail.repeat = (event.extra & kExtraRepeat) != 0 ? 1 : 0;
      break;
    case HolderEvent::Type::character:
      mail.kind = grace::input::MailKind::character;
      mail.code_point = static_cast<std::uint32_t>(event.a);
      break;
    case HolderEvent::Type::mouse_move: {
      const float x = scaled(event.a, g_buffer_width.load(std::memory_order_relaxed),
                             g_client_width.load(std::memory_order_relaxed));
      const float y = scaled(event.b, g_buffer_height.load(std::memory_order_relaxed),
                             g_client_height.load(std::memory_order_relaxed));
      g_pointer_x.store(x, std::memory_order_relaxed);
      g_pointer_y.store(y, std::memory_order_relaxed);
      mail.kind = grace::input::MailKind::pointer;
      mail.x = x;
      mail.y = y;
      break;
    }
    case HolderEvent::Type::mouse_button:
      mail.kind = grace::input::MailKind::pointer;
      mail.button_event = 1;
      mail.button = event.key;
      mail.pressed = event.down;
      break;
    case HolderEvent::Type::wheel:
      mail.kind = grace::input::MailKind::pointer;
      mail.wheel_y = static_cast<float>(event.a) / WHEEL_DELTA;
      mail.wheel_x = static_cast<float>(event.b) / WHEEL_DELTA;
      break;
    case HolderEvent::Type::none: break;
  }
  return mail;
}

}  // namespace

void relative_motion(MotionSource source, std::int32_t dx, std::int32_t dy) noexcept {
  if (dx == 0 && dy == 0) {
    return;
  }
  const Holder holder = g_capture.holder();
  if (holder.phase != Phase::active || !holder.takes(grace::input::want_mouse)) {
    return;  // the game's motion: GraceUI counts the mouse's only while a holder has it
  }
  const std::int64_t now = now_ms();
  g_motion_holder.store(holder.generation, std::memory_order_relaxed);
  if (source == MotionSource::raw_input) {
    g_motion_raw_x.fetch_add(dx, std::memory_order_relaxed);
    g_motion_raw_y.fetch_add(dy, std::memory_order_relaxed);
    g_motion_raw_at.store(now, std::memory_order_relaxed);
  } else if (now - g_motion_raw_at.load(std::memory_order_relaxed) >= kMotionLiveMs) {
    g_motion_dinput_x.fetch_add(dx, std::memory_order_relaxed);
    g_motion_dinput_y.fetch_add(dy, std::memory_order_relaxed);
    g_motion_dinput_at.store(now, std::memory_order_relaxed);
  }
}

bool pointer_by_motion() noexcept {
  const Holder holder = g_capture.holder();
  return holder.generation != 0 && g_motion_holder.load(std::memory_order_relaxed) == holder.generation;
}

void absolute_motion() noexcept { g_motion_holder.store(0, std::memory_order_relaxed); }

void push_event(HolderEvent event) noexcept {
  if (!g_events.push(event) && g_events.dropped() == 1) {
    log_line("the holder's input came faster than its frames: some was dropped");
  }
  grace::input::HolderMail* mail = g_mail.load(std::memory_order_acquire);
  if (mail != nullptr && mail->active(event.generation)) {
    const grace::input::Mail posted = mail_of(event);
    if (posted.kind != grace::input::MailKind::none) {
      mail->post(posted);
    }
  }
}

void notify_mail(std::uint16_t owner) noexcept { wake::signal(owner); }  // its mod's poll has a holder's event (D212)

void make_holder_mail() {
  if (g_mail.load(std::memory_order_acquire) == nullptr) {
    auto* mail = new grace::input::HolderMail();  // never freed: GraceUI never unloads
    mail->set_notify(&notify_mail);
    g_mail.store(mail, std::memory_order_release);
  }
}

grace::input::HolderMail* holder_mail() noexcept { return g_mail.load(std::memory_order_acquire); }

void holder_mail_enabled(std::uint32_t generation) noexcept {
  // The enable above, the holder below; closed() pairs with it (its close, then its end): one of the two sees the other.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const Holder now = g_capture.holder();
  if (now.holds() && now.generation == generation) {
    return;  // still holding: its close routine posts its end
  }
  if (grace::input::HolderMail* mail = g_mail.load(std::memory_order_acquire); mail != nullptr) {
    const grace::input::LastClose last = g_capture.last_close();
    const auto why = last.holder.generation == generation ? last.why : grace::input::Closed::released;
    mail->end(generation, static_cast<std::uint32_t>(why));  // once: end() takes the holder only if it is still active
  }
}

void buffer_size(std::uint32_t& width, std::uint32_t& height) noexcept {
  width = g_buffer_width.load(std::memory_order_relaxed);
  height = g_buffer_height.load(std::memory_order_relaxed);
}

void set_mod_caret(std::uint16_t owner, std::int32_t x, std::int32_t y, std::int32_t height) noexcept {
  g_caret_height.store(0, std::memory_order_relaxed);  // never half of one caret and half of another
  g_caret_owner.store(owner, std::memory_order_relaxed);
  g_caret_x.store(x, std::memory_order_relaxed);
  g_caret_y.store(y, std::memory_order_relaxed);
  g_caret_height.store(height, std::memory_order_relaxed);
}

void set_window(HWND target, bool unicode) noexcept {
  const HWND before = g_window.exchange(target, std::memory_order_acq_rel);
  if (before != nullptr && before != target) {
    // The filter moved to the bound swap chain's window (M2): a holder's input came from the other one.
    closed(g_capture.close(grace::input::Closed::focus_lost, now_ms()));
    log_line("the window procedure's filter moved to the bound swap chain's window");
  }
  RECT client{};
  if (GetClientRect(target, &client)) {
    set_client_size(client.right - client.left, client.bottom - client.top);
  }
  g_unicode.store(unicode, std::memory_order_relaxed);
  g_filtered.store(true, std::memory_order_release);
  log_line("the game's window procedure is filtered (%s window)", unicode ? "a Unicode" : "an ANSI");
}

HWND window() noexcept { return g_window.load(std::memory_order_acquire); }
bool window_filtered() noexcept { return g_filtered.load(std::memory_order_acquire); }
bool window_unicode() noexcept { return g_unicode.load(std::memory_order_relaxed); }

void set_client_size(std::int32_t width, std::int32_t height) noexcept {
  g_client_width.store(width, std::memory_order_relaxed);
  g_client_height.store(height, std::memory_order_relaxed);
}

void window_moved() noexcept {
  if (g_clipped.load(std::memory_order_acquire) && g_capture.holder().takes(grace::input::want_mouse)) {
    confine_cursor(false);
  }
}

void set_keyboard_code_page(std::uint32_t code_page) noexcept {
  g_keyboard_code_page.store(code_page, std::memory_order_relaxed);
}

std::uint32_t code_page() noexcept {
  const std::uint32_t forced = g_forced_code_page.load(std::memory_order_relaxed);
  return forced != 0 ? forced : g_keyboard_code_page.load(std::memory_order_relaxed);
}

bool caret_in_client(std::int32_t& x, std::int32_t& y, std::int32_t& height) noexcept {
  const std::int32_t caret_height = g_caret_height.load(std::memory_order_relaxed);
  const std::uint16_t caret_owner = g_caret_owner.load(std::memory_order_relaxed);
  if (caret_height <= 0 || (caret_owner != 0 && g_capture.holder().owner != caret_owner)) {
    return false;  // none, or a mod's that does not hold input now (D176)
  }
  const auto to_client = [](std::int32_t value, std::uint32_t buffer, std::int32_t client) {
    return buffer > 0 && client > 0 ? static_cast<std::int32_t>(static_cast<std::int64_t>(value) * client / buffer)
                                     : value;
  };
  const std::uint32_t buffer_width = g_buffer_width.load(std::memory_order_relaxed);
  const std::uint32_t buffer_height = g_buffer_height.load(std::memory_order_relaxed);
  const std::int32_t client_width = g_client_width.load(std::memory_order_relaxed);
  const std::int32_t client_height = g_client_height.load(std::memory_order_relaxed);
  x = to_client(g_caret_x.load(std::memory_order_relaxed), buffer_width, client_width);
  y = to_client(g_caret_y.load(std::memory_order_relaxed), buffer_height, client_height);
  height = to_client(caret_height, buffer_height, client_height);
  return true;
}

grace::input::Request request(std::uint16_t owner, Kind kind, std::uint8_t wants) noexcept {
  const grace::input::Request answer = g_capture.request(owner, kind, wants, &read_conditions, nullptr, now_ms());
  if (answer.answer == grace::input::Answer::not_now) {
    closed(answer.holder);  // a holder granted, then ended at once by a claim made meanwhile (invariant 9)
    return answer;
  }
  if (answer.answer == grace::input::Answer::granted &&
      g_last_granted.exchange(answer.holder.generation, std::memory_order_acq_rel) != answer.holder.generation) {
    log_line("%s holds input", kind_name(answer.holder.kind));
    if ((answer.holder.wants & grace::input::want_mouse) != 0 && confine_cursor(true)) {
      // The holder may have ended between its grant and the confinement (focus loss, on the window's thread), and that
      // close found nothing to put back: the confinement is undone here. g_clipped is stored above, the holder read
      // below, and closed() fences between its close and its own look at g_clipped: one of the two undoes it.
      std::atomic_thread_fence(std::memory_order_seq_cst);
      const Holder now = g_capture.holder();
      if (now.phase != Phase::active || now.generation != answer.holder.generation) {
        restore_cursor();
      }
    }
  }
  return answer;
}

bool release(std::uint16_t owner) noexcept {
  const Holder before = g_capture.holder();
  if (before.owner != owner || !g_capture.release(owner, now_ms())) {
    return false;
  }
  closed(before);
  return true;
}

void world_changed() noexcept { closed(g_capture.settle(&read_conditions, nullptr, now_ms())); }  // invariant 9

bool danger_or_loading() noexcept { return (world_flags() & (GRACEUI_WORLD_DANGER | GRACEUI_WORLD_LOADING)) != 0; }

bool close_holder(grace::input::Closed why, std::uint32_t generation) noexcept {
  const Holder ended = g_capture.close(why, now_ms(), generation);
  closed(ended);
  return ended.holds();
}

void set_caret(std::int32_t x, std::int32_t y, std::int32_t height) noexcept {
  g_caret_owner.store(0, std::memory_order_relaxed);
  g_caret_x.store(x, std::memory_order_relaxed);
  g_caret_y.store(y, std::memory_order_relaxed);
  g_caret_height.store(height, std::memory_order_relaxed);
}

void closed(const Holder& holder) noexcept {
  if (!holder.holds()) {
    return;  // nobody was closed here
  }
  std::atomic_thread_fence(std::memory_order_seq_cst);  // the close above, g_clipped below (request() pairs with it)
  restore_cursor();
  const grace::input::LastClose last = g_capture.last_close();
  log_line("%s no longer holds input: %s", kind_name(holder.kind),
           last.holder.generation == holder.generation ? closed_name(last.why) : "closed");
  if (holder.kind == Kind::menu) {
    hooks::wake();  // the menu's part of the close routine runs on GraceUI's thread: it stops watching mods' topics
  }
  if (grace::input::HolderMail* mail = g_mail.load(std::memory_order_acquire); mail != nullptr) {
    // Its mod hears why, after its other events (D176): the reason recorded for this holder, else released.
    const auto why = last.holder.generation == holder.generation ? last.why : grace::input::Closed::released;
    mail->end(holder.generation, static_cast<std::uint32_t>(why));
  }
  if (holder.owner != 0 && g_caret_owner.load(std::memory_order_relaxed) == holder.owner) {
    g_caret_height.store(0, std::memory_order_relaxed);  // a mod's caret ends with its holder
  }
}

namespace {

// The menu is away no longer: it closes as any close would (G40), its thread woken to do its part.
void menu_back_closed(const char* why) noexcept {
  g_menu_return.store(0, std::memory_order_relaxed);
  if (g_menu_away.exchange(false, std::memory_order_acq_rel)) {
    log_line("the menu closes while away: %s", why);
    hooks::wake();
  }
}

}  // namespace

void tick() noexcept {
  const std::int64_t now = now_ms();
  closed(g_capture.tick(now));
  if (!g_menu_away.load(std::memory_order_acquire)) {
    return;
  }
  // Danger or loading while the menu is away closes it as it would have closed it there (invariant 9, G40).
  if ((world_flags() & (GRACEUI_WORLD_DANGER | GRACEUI_WORLD_LOADING)) != 0) {
    menu_back_closed("danger or loading");
    return;
  }
  // The focus is back: the menu takes input again once GraceUI draws, as any holder is granted it (a game may draw
  // nothing while in the background, so the return waits for its frames, at most kMenuReturnMs).
  const std::int64_t back = g_menu_return.load(std::memory_order_relaxed);
  if (back == 0) {
    return;
  }
  const grace::input::Request again = request(0, Kind::menu, grace::input::want_all);
  if (again.answer == grace::input::Answer::granted) {
    g_menu_return.store(0, std::memory_order_relaxed);
    if (g_menu_away.exchange(false, std::memory_order_acq_rel)) {
      log_line("the game has the focus again: the menu takes input again where it was");
      hooks::wake();  // the menu's thread goes on where it was
    }
  } else if (again.answer == grace::input::Answer::refused) {
    menu_back_closed("another holder has input");
  } else if (again.why != grace::input::NotNow::not_ready || now - back >= kMenuReturnMs) {
    menu_back_closed(again.why == grace::input::NotNow::game_menu ? "a game menu is open" : "GraceUI is not drawing");
  }
}

void set_menu_away() noexcept {
  g_menu_return.store(0, std::memory_order_relaxed);
  g_menu_away.store(true, std::memory_order_release);
}

bool menu_away() noexcept { return g_menu_away.load(std::memory_order_acquire); }

void focus_back() noexcept {
  if (g_menu_away.load(std::memory_order_acquire)) {
    g_menu_return.store(std::max<std::int64_t>(now_ms(), 1), std::memory_order_relaxed);  // the next frames ask
  }
}

void focus_gone() noexcept { g_menu_return.store(0, std::memory_order_relaxed); }  // gone again before the return

void fault(const char* why) noexcept {
  if (!g_off.exchange(true, std::memory_order_acq_rel)) {
    log_line("input is off for this session: %s", why);
  }
  closed(g_capture.settle(&read_conditions, nullptr, now_ms()));  // off: any holder ends (fault)
}

void turn_off(const char* why) noexcept {
  if (!g_off.exchange(true, std::memory_order_acq_rel)) {
    log_line("input is off for this session: %s", why);
  }
  closed(g_capture.settle(&read_conditions, nullptr, now_ms()));
}

bool off() noexcept { return g_off.load(std::memory_order_acquire); }

void note_menu_key(grace::input::MenuKey what) noexcept {
  switch (what) {
    case grace::input::MenuKey::notice: {
      const Holder holder = g_capture.holder();
      log_line("%s has the controls: press the menu key again within 2 s to take them back", kind_name(holder.kind));
      session::controls_notice(holder.kind == Kind::menu ? 0u : holder.owner);
      return;
    }
    case grace::input::MenuKey::open_menu:
      menu::open_requested();  // the menu (D167); the key is masked from the game whatever it answers (§5.1)
      return;
    default: return;
  }
}

bool wants_frames() noexcept { return g_capture.holder().phase == Phase::active; }

void rendered() noexcept { g_capture.rendered(now_ms()); }


void reserve_imgui_queues() noexcept {
  ImGuiContext& context = *ImGui::GetCurrentContext();
  context.InputEventsQueue.reserve(kImGuiReserve);
  context.InputEventsTrail.reserve(kImGuiReserve);
  ImGui::GetIO().InputQueueCharacters.reserve(kImGuiReserve);
}

void before_imgui_frame(std::uint32_t width, std::uint32_t height) noexcept {
  g_buffer_width.store(width, std::memory_order_relaxed);
  g_buffer_height.store(height, std::memory_order_relaxed);
  ImGuiIO& io = ImGui::GetIO();
  const Holder holder = g_capture.holder();
  const bool active = holder.phase == Phase::active;
  if (holder.generation != g_fed.generation || active != g_fed.active) {
    forget_input(io);  // a new holder, or the end of one: key-ups for everything Dear ImGui saw held (§5.1)
    g_fed.generation = holder.generation;
    g_fed.active = active;
    POINT cursor{};
    const HWND target = g_window.load(std::memory_order_acquire);
    g_menu_x = -1.0f;  // a new holder: where the pointer is, the menu learns from its first move
    g_menu_y = -1.0f;
    // The motion pointer starts where the cursor is, or at the client area's centre (D232); counts from before are
    // the game's, never the new holder's.
    g_motion_x = static_cast<float>(g_client_width.load(std::memory_order_relaxed)) / 2.0f;
    g_motion_y = static_cast<float>(g_client_height.load(std::memory_order_relaxed)) / 2.0f;
    for (std::atomic<std::int32_t>* counts : {&g_motion_raw_x, &g_motion_raw_y, &g_motion_dinput_x, &g_motion_dinput_y}) {
      counts->store(0, std::memory_order_relaxed);
    }
    if (active && holder.takes(grace::input::want_mouse) && target != nullptr && GetCursorPos(&cursor) &&
        ScreenToClient(target, &cursor)) {
      g_motion_x = static_cast<float>(cursor.x);
      g_motion_y = static_cast<float>(cursor.y);
      io.AddMousePosEvent(scaled(cursor.x, width, g_client_width.load(std::memory_order_relaxed)),
                          scaled(cursor.y, height, g_client_height.load(std::memory_order_relaxed)));
      if (menu_holds(holder)) {
        HolderEvent at;
        at.type = HolderEvent::Type::mouse_move;
        at.a = cursor.x;
        at.b = cursor.y;
        at.generation = holder.generation;
        to_menu(at, width, height);
      }
    }
  }
  io.MouseDrawCursor = active && holder.takes(grace::input::want_mouse);  // the holder's pointer: the Grace pointer (D178)
  HolderEvent event;
  if (!active) {
    for (int i = 0; i < 256 && g_events.pop(event); ++i) {
    }
    return;
  }
  // Consecutive mouse moves are one: only the last position counts, and Dear ImGui takes one change of a key or button
  // a frame, so a player's moves must not crowd out their keys.
  const ImGuiContext& context = *ImGui::GetCurrentContext();
  HolderEvent move;
  bool moved = false;
  for (int fed = 0; fed < kEventsPerFrame && context.InputEventsQueue.Size < kImGuiQueueLimit && g_events.pop(event);) {
    if (event.generation != holder.generation) {
      continue;
    }
    if (event.type == HolderEvent::Type::mouse_move) {
      move = event;
      moved = true;
      g_motion_x = static_cast<float>(event.a);  // a position the window gave: the motion pointer goes on from it (D232)
      g_motion_y = static_cast<float>(event.b);
      continue;
    }
    if (moved) {
      feed(io, move, width, height);
      if (menu_holds(holder)) {
        to_menu(move, width, height);
      }
      moved = false;
    }
    feed(io, event, width, height);
    if (menu_holds(holder)) {
      to_menu(event, width, height);
    }
    ++fed;
  }
  if (moved) {
    feed(io, move, width, height);
    if (menu_holds(holder)) {
      to_menu(move, width, height);
    }
  }
  // The mouse's own motion (D232), once a frame: raw input's where it came lately, else DirectInput's.
  if (holder.takes(grace::input::want_mouse)) {
    const bool raw = now_ms() - g_motion_raw_at.load(std::memory_order_relaxed) < kMotionLiveMs;
    std::atomic<std::int32_t>& source_x = raw ? g_motion_raw_x : g_motion_dinput_x;
    std::atomic<std::int32_t>& source_y = raw ? g_motion_raw_y : g_motion_dinput_y;
    const std::int32_t dx = source_x.exchange(0, std::memory_order_relaxed);
    const std::int32_t dy = source_y.exchange(0, std::memory_order_relaxed);
    if (raw) {
      g_motion_dinput_x.store(0, std::memory_order_relaxed);  // what DirectInput gave meanwhile is the same motion
      g_motion_dinput_y.store(0, std::memory_order_relaxed);
    }
    const std::int32_t client_width = g_client_width.load(std::memory_order_relaxed);
    const std::int32_t client_height = g_client_height.load(std::memory_order_relaxed);
    if ((dx != 0 || dy != 0) && client_width > 0 && client_height > 0) {
      g_motion_x = std::clamp(g_motion_x + static_cast<float>(dx), 0.0f, static_cast<float>(client_width - 1));
      g_motion_y = std::clamp(g_motion_y + static_cast<float>(dy), 0.0f, static_cast<float>(client_height - 1));
      HolderEvent motion;
      motion.type = HolderEvent::Type::mouse_move;
      motion.a = static_cast<std::int32_t>(g_motion_x);
      motion.b = static_cast<std::int32_t>(g_motion_y);
      motion.generation = holder.generation;
      feed(io, motion, width, height);
      if (menu_holds(holder)) {
        to_menu(motion, width, height);
      }
      grace::input::HolderMail* mail = g_mail.load(std::memory_order_acquire);
      if (mail != nullptr && mail->active(motion.generation)) {
        mail->post(mail_of(motion));  // a mod's holder learns its pointer's place as from a window's move (D176)
      }
    }
  }
  if (holder.takes(grace::input::want_pad)) {
    feed_pad(io, holder.generation);
  }
#if defined(GRACEUI_TEST_EXPORTS)
  counters::frames_fed.fetch_add(1, std::memory_order_relaxed);
#endif
}

void after_new_frame() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  const ImGuiIO& io = ImGui::GetIO();
  for (const ImWchar character : io.InputQueueCharacters) {
    counters::record_char(static_cast<std::uint32_t>(character));
  }
  std::uint32_t down = 0;
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
    if (!ImGui::IsMouseKey(static_cast<ImGuiKey>(key)) && io.KeysData[key - ImGuiKey_NamedKey_BEGIN].Down) {
      ++down;
    }
  }
  counters::imgui_keys_down.store(down, std::memory_order_relaxed);
  counters::imgui_mouse_x.store(static_cast<std::int32_t>(io.MousePos.x), std::memory_order_relaxed);
  counters::imgui_mouse_y.store(static_cast<std::int32_t>(io.MousePos.y), std::memory_order_relaxed);
  counters::imgui_draws_cursor.store(io.MouseDrawCursor ? 1u : 0u, std::memory_order_relaxed);
#endif
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_set_world(std::uint32_t flags) noexcept {
  g_test_world.store(flags & grace::abi::kWorldFlags, std::memory_order_release);
  world_changed();
}
void test_set_code_page(std::uint32_t code_page) noexcept {
  g_forced_code_page.store(code_page, std::memory_order_relaxed);
}
std::uint32_t test_events_dropped() noexcept { return g_events.dropped(); }
#endif

}  // namespace grace::host::input
