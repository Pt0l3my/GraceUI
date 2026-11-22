// session.cpp: the session's state as its mods see it (session.h), and ABI 1's slots behind the DLL's abi files
// (host.h's abi1 functions). A mod's strings arrive here already copied and bounded; the rules are grace-abi's.
#include "session.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string_view>

#include "display.h"
#include "grace/abi/text.h"
#include "grace/host/host.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "spaces.h"
#include "state.h"

namespace grace::host {
namespace session {
namespace {

constinit grace::abi::Consumers g_consumers;
constinit grace::abi::Notices g_notices;
constinit std::atomic<bool> g_notice_only{false};
constinit std::atomic<anticheat::Clause> g_anti_cheat{anticheat::Clause::none};

constexpr std::uint32_t kVersion = GRACEUI_VERSION_PACK(GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);

// S10.4.c's stores, made once (allocate). A function-local pointer: made at its first use, never destroyed.
struct Stores {
  grace::abi::Panels panels;
  grace::abi::Registry registry;
  grace::abi::World world;
  grace::abi::QuietNotices quiet;
};

Stores& stores() {
  static Stores* const made = new Stores();
  return *made;
}

// The panel that holds input, and its holder's generation: one pair, written and read whole under a lock of mods'
// threads (the present thread never reads it).
constinit grace::abi::SpinLock g_input_panel_lock;
constinit std::uint64_t g_input_panel = 0;
constinit std::uint32_t g_input_generation = 0;

}  // namespace

grace::abi::Consumers& consumers() noexcept { return g_consumers; }
grace::abi::Notices& notices() noexcept { return g_notices; }
grace::abi::Panels& panels() noexcept { return stores().panels; }
grace::abi::Registry& registry() noexcept { return stores().registry; }
grace::abi::World& world() noexcept { return stores().world; }
grace::abi::QuietNotices& quiet() noexcept { return stores().quiet; }
void allocate() {
  input::make_holder_mail();  // a holder's events for its mod (D176), before any mod has a table
  (void)stores();
  (void)spaces::registry();  // the mods' spaces (D169)
  (void)spaces::own();  // GraceUI's own settings, with the last session's (D179), before the first connect reads ui.scale
}

void enter_notice_only(anticheat::Clause clause) noexcept {
  g_anti_cheat.store(clause, std::memory_order_relaxed);
  if (g_notice_only.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  log_line("the anti-cheat check found %s: GraceUI shows notices only this session (D88)",
           clause == anticheat::Clause::parent ? "the game started by an anti-cheat's launcher"
                                               : "an anti-cheat's module loaded");
  input::turn_off("the anti-cheat's notice-only mode: every input passes to the game");
  own_notice(kOwnKeyAntiCheat, GRACEUI_TONE_WARNING,
             "Anti-cheat is running, so GraceUI shows notices only this session: no menu, panels or browser view.");
}

bool notice_only() noexcept { return g_notice_only.load(std::memory_order_acquire); }

anticheat::Clause anti_cheat() noexcept { return g_anti_cheat.load(std::memory_order_relaxed); }

grace::abi::Inactive inactive() noexcept {
  if (notice_only()) {
    return grace::abi::Inactive::anti_cheat;
  }
  if (state::host.load(std::memory_order_acquire) == HostState::failed) {
    return grace::abi::Inactive::host_failed;
  }
  if (state::render_off.load(std::memory_order_relaxed)) {
    return grace::abi::Inactive::render_dead;
  }
  return grace::abi::Inactive::none;
}

void own_notice(std::uint32_t key, std::uint32_t tone, const char* format, ...) noexcept {
  char text[grace::abi::kNoticeTextBytes + 1];
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(text, sizeof text, format, args);
  va_end(args);
  if (written <= 0) {
    return;
  }
  std::size_t length = static_cast<std::size_t>(written) < grace::abi::kNoticeTextBytes
                           ? static_cast<std::size_t>(written)
                           : grace::abi::kNoticeTextBytes;
  // A text cut short ends on a whole character.
  while (length < static_cast<std::size_t>(written) && length > 0 &&
         (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
    --length;
  }
  grace::abi::NoticeIn in;
  in.owner = 0;
  in.key = key;
  in.tone = tone;
  in.duration_ms = kOwnNoticeMs;
  in.text = std::string_view(text, length);
  switch (g_notices.post(in, input::now_ms())) {
    case grace::abi::Posted::full: log_line("one of GraceUI's own notices was not shown: too many are showing"); break;
    case grace::abi::Posted::invalid: log_line("one of GraceUI's own notices was not shown: its text is not a notice's");
      break;
    default: break;
  }
}

void own_notice_text(std::uint32_t key, std::uint32_t tone, std::uint32_t duration_ms, std::string_view text) noexcept {
  grace::abi::NoticeIn in;
  in.owner = 0;
  in.key = key;
  in.tone = tone;
  in.duration_ms = duration_ms;
  in.text = text;
  if (g_notices.post(in, input::now_ms()) == grace::abi::Posted::full) {
    log_line("one of GraceUI's own notices was not shown: too many are showing");
  }
}

void controls_notice(std::uint32_t owner) noexcept {
  // Made by hand, without formatting: the window procedure's filter calls this, which allocates nothing.
  char text[grace::abi::kNoticeTextBytes];
  std::size_t length = 0;
  const auto append = [&](std::string_view part) {
    for (const char c : part) {
      if (length < sizeof text) {
        text[length++] = c;
      }
    }
  };
  grace::abi::ConsumerInfo info;
  if (owner != 0 && g_consumers.info_of(owner, info)) {
    append(info.name);
  } else {
    append(owner == 0 ? "GraceUI's menu" : "A mod's panel");
  }
  append(" has the controls. Press F5 again to take them back.");
  own_notice_text(kOwnKeyControls, GRACEUI_TONE_NORMAL, kControlsNoticeMs, std::string_view(text, length));
}

void set_input_panel(std::uint32_t generation, std::uint64_t panel) noexcept {
  const grace::abi::SpinGuard guard(g_input_panel_lock);
  g_input_panel = panel;
  g_input_generation = generation;
}

bool input_panel(std::uint32_t& generation, std::uint64_t& panel) noexcept {
  const grace::abi::SpinGuard guard(g_input_panel_lock);
  generation = g_input_generation;
  panel = g_input_panel;
  return generation != 0 && panel != 0;
}

}  // namespace session

namespace abi1 {
namespace {

const char* inactive_name(std::uint32_t reason) noexcept {
  switch (reason) {
    case GRACEUI_INACTIVE_ANTI_CHEAT: return "anti-cheat: notices only";
    case GRACEUI_INACTIVE_HOST_FAILED: return "the overlay could not start";
    case GRACEUI_INACTIVE_RENDER_DEAD: return "the overlay turned itself off";
    default: return "inactive";
  }
}

const char* refused_name(std::uint32_t reason) noexcept {
  switch (reason) {
    case GRACEUI_REFUSED_ARGUMENT: return "a null pointer or a struct below its first size";
    case GRACEUI_REFUSED_ID: return "an id that is not 3 to 32 of a-z, 0-9 and '-'";
    case GRACEUI_REFUSED_NAME: return "a display name that is empty, too long or not UTF-8 text";
    case GRACEUI_REFUSED_CAPS: return "caps that are not cap names separated by spaces";
    case GRACEUI_REFUSED_TOO_MANY: return "as many mods as GraceUI takes are connected";
    default: return "refused";
  }
}

// The slots whose refusals, and whose faults, have been logged, one bit each: a mod that repeats a bad call fills no log.
constinit std::atomic<std::uint64_t> g_refusals_logged{0};
constinit std::atomic<std::uint64_t> g_faults_logged{0};

std::uint64_t slot_bit(const char* slot) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  for (const char* c = slot; *c != '\0'; ++c) {
    hash = (hash ^ static_cast<unsigned char>(*c)) * 1099511628211ull;
  }
  return std::uint64_t{1} << (hash % 64);
}

bool first_time(std::atomic<std::uint64_t>& logged, const char* slot) noexcept {
  const std::uint64_t bit = slot_bit(slot);
  return (logged.fetch_or(bit, std::memory_order_relaxed) & bit) == 0;
}

// Connects that did not connect a mod (too old, a duplicate id, refused): each is logged, up to this many in a session,
// so that a mod that keeps asking cannot push everything else out of the log.
constexpr std::uint32_t kConnectFailuresLogged = 16;
constinit std::atomic<std::uint32_t> g_connect_failures{0};

bool log_connect_failure() noexcept {
  const std::uint32_t count = g_connect_failures.fetch_add(1, std::memory_order_relaxed);
  if (count == kConnectFailuresLogged) {
    log_line("more connects failed than the log keeps: the rest are not logged");
  }
  return count < kConnectFailuresLogged;
}

}  // namespace

bool has_cap(std::string_view name) noexcept {
  return name == GRACEUI_CAP_NOTICES || name == GRACEUI_CAP_PANELS || name == GRACEUI_CAP_TEXTURES ||
         name == GRACEUI_CAP_FACES || name == GRACEUI_CAP_INPUT || name == GRACEUI_CAP_WORLD_STATE ||
         name == GRACEUI_CAP_HOTKEYS || name == GRACEUI_CAP_PAGES || name == GRACEUI_CAP_HOLDER_EVENTS ||
         name == GRACEUI_CAP_DISPLAY || name == GRACEUI_CAP_PAD_CHORDS;
}

grace::abi::ConnectResult connect(const grace::abi::ConnectRequest& request) noexcept {
  const grace::abi::ConnectResult result =
      session::consumers().connect(request, session::kVersion, session::inactive());
  const int id_length = static_cast<int>(request.id.size());
  if (result.status == GRACEUI_OK || result.status == GRACEUI_INACTIVE) {
    display::connected(result.owner);  // it hears of the display at its first poll of 1.2's size (D180)
    hooks::wake();                     // the Mods page lists it (D181)
  }
  switch (result.status) {
    case GRACEUI_OK:
      log_line("%.*s %u.%u.%u connected (ABI 1.%u)", id_length, request.id.data(), GRACEUI_VERSION_MAJOR(request.version),
               GRACEUI_VERSION_MINOR(request.version), GRACEUI_VERSION_PATCH(request.version), request.abi_minor);
      break;
    case GRACEUI_INACTIVE:
      log_line("%.*s connected, but GraceUI is inactive this session: %s", id_length, request.id.data(),
               inactive_name(result.reason));
      break;
    case GRACEUI_TOO_OLD:
      if (log_connect_failure()) {
        log_line("%.*s needs GraceUI %u.%u.%u or newer; this is GraceUI %u.%u.%u", id_length, request.id.data(),
                 GRACEUI_VERSION_MAJOR(request.floor), GRACEUI_VERSION_MINOR(request.floor),
                 GRACEUI_VERSION_PATCH(request.floor), GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);
      }
      session::own_notice(0, GRACEUI_TONE_WARNING, "%.*s needs GraceUI %u.%u.%u or newer; this is GraceUI %u.%u.%u.",
                          static_cast<int>(request.name.size()), request.name.data(),
                          GRACEUI_VERSION_MAJOR(request.floor), GRACEUI_VERSION_MINOR(request.floor),
                          GRACEUI_VERSION_PATCH(request.floor), GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR,
                          GRACEUI_BUILD_PATCH);
      break;
    case GRACEUI_DUPLICATE_ID:
      if (log_connect_failure()) {
        log_line("a second mod asked to connect as %.*s while it is connected: refused", id_length, request.id.data());
      }
      break;
    default:
      connect_refused(result.reason);
      break;
  }
  return result;
}

void connect_refused(std::uint32_t reason) noexcept {
  if (log_connect_failure()) {
    log_line("a connect was refused: %s", refused_name(reason));
  }
}

GraceUiStatus disconnect(GraceUiConsumer consumer) noexcept {
  std::uint32_t owner = 0;
  grace::abi::ConsumerInfo info;
  const bool known = session::consumers().info_of(static_cast<std::uint32_t>(consumer & 0xFFu), info);
  const GraceUiStatus status = session::consumers().disconnect(consumer, owner);
  if (status == GRACEUI_OK) {
    // The mod's number is not given to another until its clean-up is done (Consumers::left): what is withdrawn here by
    // that number is this mod's alone.
    session::notices().withdraw_owner(owner, input::now_ms());
    forget(consumer, owner);
    session::consumers().left(owner);
    hooks::wake();  // the Mods page lists it no more (D181)
    log_line("%s disconnected", known ? info.id : "a mod");
  }
  return status;
}

GraceUiStatus notice(GraceUiConsumer consumer, std::uint32_t tone, std::uint32_t key, std::uint32_t duration_ms,
                     std::string_view text) noexcept {
  const std::uint32_t owner = session::consumers().owner_of(consumer);
  if (owner == 0) {
    return GRACEUI_NOT_CONNECTED;
  }
  const grace::abi::Inactive inactive = session::inactive();
  if (inactive != grace::abi::Inactive::none && inactive != grace::abi::Inactive::anti_cheat) {
    return GRACEUI_INACTIVE;  // nothing can be drawn this session; in the notice-only mode notices still show (D88)
  }
  if (!text.empty()) {
    // A quiet mod's notice (G32, D181): accepted as any other, so the mod learns nothing, but kept in the Mods page's
    // list rather than drawn over the game. Its withdrawals go on as usual: one it posted before it was quiet goes.
    grace::abi::ConsumerInfo info;
    try {
      if (session::consumers().info_of(owner, info) && session::quiet().quiet(info.id)) {
        if (!grace::abi::valid_notice_text(text) || tone > GRACEUI_TONE_DANGER) {
          refused("notice", "a text that is not a notice's (UTF-8 text of 1 to 512 bytes), or an unknown tone");
          return GRACEUI_REFUSED;
        }
        session::quiet().keep(info.id, info.name, key, text);
        hooks::wake();  // the Mods page's list shows it
        return GRACEUI_OK;
      }
    } catch (...) {
      return GRACEUI_FULL;  // no room to keep it
    }
  }
  grace::abi::NoticeIn in;
  in.owner = owner;
  in.key = key;
  in.tone = tone;
  in.duration_ms = duration_ms;
  in.text = text;
  switch (session::notices().post(in, input::now_ms())) {
    case grace::abi::Posted::shown:
    case grace::abi::Posted::replaced:
    case grace::abi::Posted::withdrawn: return GRACEUI_OK;
    case grace::abi::Posted::full: return GRACEUI_FULL;
    case grace::abi::Posted::invalid: break;
  }
  refused("notice", "a text that is not a notice's (UTF-8 text of 1 to 512 bytes), or an unknown tone");
  return GRACEUI_REFUSED;
}

GraceUiStatus not_yet(GraceUiConsumer consumer) noexcept {
  if (session::consumers().owner_of(consumer) == 0) {
    return GRACEUI_NOT_CONNECTED;
  }
  return session::inactive() != grace::abi::Inactive::none ? GRACEUI_INACTIVE : GRACEUI_NOT_YET;
}

void refused(const char* slot, const char* why) noexcept {
  if (first_time(g_refusals_logged, slot)) {
    log_line("%s refused a call: %s", slot, why);
  }
}

void fault(const char* slot) noexcept {
  if (first_time(g_faults_logged, slot)) {
    log_line("%s: an exception inside GraceUI, caught; nothing was done", slot);
  }
}

}  // namespace abi1
}  // namespace grace::host
