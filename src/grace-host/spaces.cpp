// spaces.cpp: the spaces in the host, and the browser view's server (spaces.h; D169, D161).
#include "spaces.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "anticheat.h"
#include "display.h"
#include "draw_atlas.h"
#include "grace/abi/spin.h"
#include "grace/serve/server.h"
#include "grace/spaces/devices.h"
#include "grace/spaces/own_space.h"
#include "grace/spaces/registry.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "session.h"
#include "wake.h"

namespace grace::host::spaces {
namespace {

// GraceUI's own thread's: made at its first serve(), never destroyed (GraceUI never unloads).
constinit std::atomic<grace::spaces::OwnSpace*> g_own{nullptr};  // published once, by GraceUI's own thread
constinit bool g_own_failed = false;
constinit grace::serve::Server* g_server = nullptr;
constinit std::atomic<bool> g_start_failed{false};
constinit std::uint64_t g_kept_revision = 1;  // the own space's revision last written to user\settings.json (GraceUI's thread)
constinit std::uint64_t g_kept_quiet = 1;     // the quiet mods' revision last written there (D181)
constinit std::uint64_t g_kept_bindings = 0;  // the kept hotkey bindings' revision last written there (D208)
constinit bool g_menu_key_applied = false;    // GraceUI's thread: hotkeys.menu applied once, at the start
constinit bool g_keep_failed = false;
constinit std::atomic<bool> g_running{false};
constinit std::atomic<std::uint32_t> g_port{0};
// An anti-cheat found after the first request (§7.2 item 1, D81): the server is closed for the rest of the session.
constinit std::atomic<bool> g_closed{false};
constinit std::int64_t g_next_recheck_ms = 0;  // GraceUI's thread: the next module re-check, while the server runs
constexpr std::int64_t kRecheckMs = 60'000;
constinit std::atomic<std::uint32_t> g_test_recheck_ms{0};

// Other devices at home (G9, §7.2 item 16): GraceUI's thread alone; made at first use, never destroyed.
struct Pairing {
  bool active = false;  // the sheet is open
  bool paired = false;
  bool ended = false;
  std::string why;
  std::string code;
  std::chrono::steady_clock::time_point expires{};
  std::vector<std::string> addresses;
  std::string paired_name;
};
constinit Pairing* g_pairing = nullptr;
constinit std::vector<grace::spaces::PairedDeviceRecord>* g_devices = nullptr;
constinit int g_lan_applied = -1;  // dashboard.lan as last given to the server; -1 before the first
constinit bool g_devices_keep_failed = false;
#if defined(GRACEUI_TEST_EXPORTS)
// The test build's: the code the sheet shows, and the addresses the network listener takes in a test (127.0.0.2).
constinit grace::abi::SpinLock g_test_pairing_lock;
constinit std::array<char, 8> g_test_code{};
constinit std::array<std::array<std::uint8_t, 4>, 4> g_test_addresses{};
constinit std::uint32_t g_test_address_count = 0;

void test_code(std::string_view code) noexcept {
  g_test_pairing_lock.lock();
  g_test_code.fill('\0');
  std::copy_n(code.begin(), std::min(code.size(), g_test_code.size() - 1), g_test_code.begin());
  g_test_pairing_lock.unlock();
}
#endif

Pairing& pairing() {
  if (g_pairing == nullptr) {
    g_pairing = new Pairing();
  }
  return *g_pairing;
}

std::vector<grace::spaces::PairedDeviceRecord>& devices() {
  if (g_devices == nullptr) {
    g_devices = new std::vector<grace::spaces::PairedDeviceRecord>();
  }
  return *g_devices;
}

std::int64_t steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t recheck_interval_ms() {
  const std::uint32_t test = g_test_recheck_ms.load(std::memory_order_relaxed);
  return test != 0 ? static_cast<std::int64_t>(test) : kRecheckMs;
}

void server_log(grace::serve::LogLevel, std::string_view line) {
  log_line("browser view: %.*s", static_cast<int>(line.size()), line.data());
}

bool wanted(const grace::spaces::OwnSpace& own) {
  const protocol::Json& enabled = own.store().held("dashboard.enabled");
  return !session::notice_only() && !g_closed.load(std::memory_order_acquire) && registry().wants_browser() &&
         enabled.is_boolean() && enabled.get<bool>();
}

#if defined(GRACEUI_TEST_EXPORTS)
// A test build's port (D234): GRACEUI_TEST_PORT names one (1024 to 65535), or "free" asks the system for one, so that
// two test runs on one machine need not share the settings' port. GraceUI's settings file is never changed by it.
enum class TestPort { none, fixed, free };

TestPort test_port(std::uint16_t& port) noexcept {
  wchar_t text[16] = {};
  const DWORD length = GetEnvironmentVariableW(L"GRACEUI_TEST_PORT", text, 16);
  if (length == 0 || length >= 16) {
    return TestPort::none;
  }
  if (std::wcscmp(text, L"free") == 0) {
    return TestPort::free;
  }
  wchar_t* end = nullptr;
  const unsigned long value = std::wcstoul(text, &end, 10);
  if (end == text || *end != L'\0' || value < grace::serve::kLowestPort || value > 65535) {
    log_line("GRACEUI_TEST_PORT is neither free nor a port from 1024 to 65535: the settings' port is used");
    return TestPort::none;
  }
  port = static_cast<std::uint16_t>(value);
  return TestPort::fixed;
}
#endif

void start(const grace::spaces::OwnSpace& own) {
  std::wstring folder;
  if (!atlas::own_folder(folder)) {
    g_start_failed.store(true, std::memory_order_relaxed);
    log_line("the browser view cannot start: GraceUI's own folder could not be named");
    return;
  }
  const protocol::Json& port = own.store().held("dashboard.port");
  grace::serve::ServerConfig config;
  config.port = static_cast<std::uint16_t>(port.is_number_integer() ? port.get<std::int64_t>() : 8642);
  config.graceui_version = version_text();
  config.folder = std::filesystem::path(folder);
  config.log = &server_log;
  config.paired_wake = [] { hooks::wake(); };  // the sheet shows the device paired, and the file keeps it (G9)
#if defined(GRACEUI_TEST_EXPORTS)
  g_test_pairing_lock.lock();
  config.network_addresses.assign(g_test_addresses.begin(), g_test_addresses.begin() + g_test_address_count);
  g_test_pairing_lock.unlock();
#endif
  int tries = 1;
#if defined(GRACEUI_TEST_EXPORTS)
  std::uint16_t chosen = 0;
  const TestPort asked = test_port(chosen);
  if (asked == TestPort::fixed) {
    config.port = chosen;
  } else if (asked == TestPort::free) {
    tries = 5;  // another program may take the port between the system's answer and the bind: ask again
  }
#endif
  grace::serve::StartResult result = grace::serve::StartResult::port_taken;
  for (int attempt = 0; attempt < tries; ++attempt) {
#if defined(GRACEUI_TEST_EXPORTS)
    if (asked == TestPort::free) {
      if (!grace::serve::free_loopback_port(chosen)) {
        log_line("GRACEUI_TEST_PORT is free, but the system gave no port");
        break;
      }
      config.port = chosen;
    }
#endif
    result = g_server->start(config);
    if (result != grace::serve::StartResult::port_taken) {
      break;
    }
  }
#if defined(GRACEUI_TEST_EXPORTS)
  if (asked != TestPort::none && result == grace::serve::StartResult::ok) {
    log_line("a test build's port, as GRACEUI_TEST_PORT asks: %u (the settings unchanged)",
             static_cast<unsigned>(config.port));
  }
#endif
  if (result != grace::serve::StartResult::ok) {
    g_start_failed.store(true, std::memory_order_relaxed);  // not tried again this session: the port's setting takes effect at the next launch
    const std::string_view why = grace::serve::to_string(result);
    log_line("the browser view could not start: %.*s", static_cast<int>(why.size()), why.data());
    session::own_notice(0, GRACEUI_TONE_WARNING, "The browser view could not start on port %u: %s.",
                        static_cast<unsigned>(config.port),
                        result == grace::serve::StartResult::port_taken ? "another program holds it" : "see the log");
    return;
  }
  g_port.store(config.port, std::memory_order_relaxed);
  g_running.store(true, std::memory_order_release);
  g_next_recheck_ms = steady_ms() + recheck_interval_ms();
  log_line("the browser view is served on loopback, port %u", static_cast<unsigned>(config.port));
}

}  // namespace

std::string version_text() {
  return std::to_string(GRACEUI_BUILD_MAJOR) + "." + std::to_string(GRACEUI_BUILD_MINOR) + "." +
         std::to_string(GRACEUI_BUILD_PATCH);
}

std::string increment_text() {
  return std::to_string(GRACEUI_BUILD_MAJOR) + "." + std::to_string(GRACEUI_BUILD_MINOR);
}

grace::spaces::Registry& registry() noexcept {
  static grace::spaces::Registry* const made = [] {
    auto* made = new grace::spaces::Registry([](std::string_view line) {
      log_line("%.*s", static_cast<int>(line.size()), line.data());
    });
    made->set_game_wake([] { hooks::wake(); });  // the menu's: a space came or went, a state or a result came back
    made->set_owner_wake(&wake::signal_consumer);  // a mod's: a view's watch, unwatch or command for its poll (D212)
    return made;
  }();
  return *made;
}

namespace {

// GraceUI's settings kept between sessions (D84, D179): Game\GraceUI\user\settings.json, beside graceui.dll, outside
// any release. Read and written on GraceUI's own thread only; a line of the log says what happened, never a value.
constexpr std::uintmax_t kSettingsFileMax = 64 * 1024;

bool settings_path(std::filesystem::path& out) {
  std::wstring folder;
  if (!atlas::own_folder(folder)) {
    return false;
  }
  out = std::filesystem::path(folder) / L"user" / L"settings.json";
  return true;
}

// A mod that connected before GraceUI's thread read its settings registered its hotkeys with their defaults: the kept
// bindings now go over them (D208), each kept one's key and chord freed first so that one given up is free for another.
void apply_kept_to_registered() {
  static grace::input::Listed listed[grace::input::kMaxHotkeys];  // GraceUI's thread alone
  const std::size_t count = input::hotkeys().list(listed, grace::input::kMaxHotkeys);
  grace::input::Chosen chosen[grace::input::kMaxHotkeys];
  bool kept[grace::input::kMaxHotkeys] = {};
  grace::input::Taken other;
  for (std::size_t i = 0; i < count; ++i) {
    grace::abi::ConsumerInfo info;
    kept[i] = session::consumers().info_of(listed[i].owner, info) &&
              input::kept_bindings().find(info.id, listed[i].action(), chosen[i]);
    if (kept[i]) {
      input::hotkeys().rebind_key(listed[i].id, grace::input::kNoKey, 0, input::menu_key(), false, other);
      input::hotkeys().rebind_chord(listed[i].id, 0, false, other);
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (kept[i]) {
      input::hotkeys().rebind_key(listed[i].id, chosen[i].key, chosen[i].modifiers, input::menu_key(), false, other);
      input::hotkeys().rebind_chord(listed[i].id, chosen[i].chord, false, other);
    }
  }
  wake::signal_owners(input::hotkeys().changed_owners());
}

void restore_saved(grace::spaces::OwnSpace& own) {
  std::filesystem::path path;
  std::error_code error;
  if (!settings_path(path) || !std::filesystem::is_regular_file(path, error)) {
    return;  // none yet: the defaults
  }
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > kSettingsFileMax) {
    log_line("GraceUI's settings were not read: the file is %s", error ? "unreadable" : "too large");
    return;
  }
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const protocol::Json file = protocol::Json::parse(text, nullptr, false);
  if (file.is_discarded() || !file.is_object() || !file.contains("values")) {
    log_line("GraceUI's settings were not read: the file is not GraceUI's settings; the defaults stand");
    return;
  }
  const grace::pages::SettingsStore::Restored back = own.restore_settings(file["values"]);
  log_line("GraceUI's settings: %u kept from an earlier session, %u left out", static_cast<unsigned>(back.taken),
           static_cast<unsigned>(back.left_out));
  // The mods whose notices the player quieted (D181): ids only, each as connect would take it.
  if (const auto found = file.find("quiet_mods"); found != file.end() && found->is_array()) {
    std::vector<std::string> ids;
    for (const protocol::Json& id : *found) {
      if (id.is_string() && ids.size() < grace::abi::kQuietMods) {
        ids.push_back(id.get<std::string>());
      }
    }
    const std::size_t taken = session::quiet().restore(ids);
    log_line("GraceUI's settings: %u mods' notices kept quiet", static_cast<unsigned>(taken));
  }
  g_kept_quiet = session::quiet().ids_revision();
  // The player's hotkey bindings (D208): ids, key names and chords only, each checked as the Hotkeys page would.
  if (const auto found = file.find("hotkey_bindings"); found != file.end() && found->is_array()) {
    std::size_t taken = 0;
    for (const protocol::Json& entry : *found) {
      if (!entry.is_object() || !entry.contains("mod") || !entry["mod"].is_string() || !entry.contains("action") ||
          !entry["action"].is_string()) {
        continue;
      }
      grace::input::Chosen chosen;
      const std::string key = entry.value("key", std::string());
      const std::uint32_t chord = entry.contains("chord") && entry["chord"].is_number_unsigned()
                                      ? entry["chord"].get<std::uint32_t>() : 0u;
      if (!grace::input::key_from_text(key, chosen.key, chosen.modifiers) || grace::input::reserved_key(chosen.key) ||
          (chord != 0 && !grace::input::valid_chord(chord))) {
        continue;
      }
      chosen.chord = chord;
      taken += input::kept_bindings().keep(entry["mod"].get_ref<const std::string&>(),
                                           entry["action"].get_ref<const std::string&>(), chosen) ? 1 : 0;
    }
    log_line("GraceUI's settings: %u hotkey bindings kept", static_cast<unsigned>(taken));
    apply_kept_to_registered();
  }
  g_kept_bindings = input::kept_bindings().revision();
}

// The paired devices (G9, §7.2 item 16): Game\GraceUI\user\devices.toml, a file of their own, never a settings file, so
// that Copy settings, the bug report and recordings never carry it. Its hashes, names and dates only; never logged.
bool devices_path(std::filesystem::path& out) {
  std::wstring folder;
  if (!atlas::own_folder(folder)) {
    return false;
  }
  out = std::filesystem::path(folder) / L"user" / L"devices.toml";
  return true;
}

void restore_devices() {
  std::filesystem::path path;
  std::error_code error;
  if (!devices_path(path) || !std::filesystem::is_regular_file(path, error)) {
    return;  // none paired yet
  }
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > kSettingsFileMax) {
    log_line("the paired devices were not read: the file is %s; no device is paired", error ? "unreadable" : "too large");
    return;
  }
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  grace::spaces::DevicesRead read = grace::spaces::read_devices(text);
  if (!read.ok) {
    log_line("the paired devices were not read: the file is not GraceUI's; no device is paired");
    return;
  }
  devices() = std::move(read.devices);
  log_line("paired devices: %u kept from an earlier session, %u left out", static_cast<unsigned>(devices().size()),
           static_cast<unsigned>(read.left_out));
}

void keep_devices() noexcept {
  try {
    std::filesystem::path path;
    if (!devices_path(path)) {
      throw std::runtime_error("no folder");
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path partial = path;
    partial += L".partial";
    {
      std::ofstream out(partial, std::ios::binary | std::ios::trunc);
      out << grace::spaces::write_devices(devices());
      if (!out.flush()) {
        throw std::runtime_error("not written");
      }
    }
    // Whole or not at all, as the settings file (D179).
    if (MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
      throw std::runtime_error("not replaced");
    }
    g_devices_keep_failed = false;
  } catch (...) {
    if (!g_devices_keep_failed) {
      log_line("the paired devices could not be kept for the next session (GraceUI's user folder is not writable)");
    }
    g_devices_keep_failed = true;
  }
}

bool lan_on(const grace::spaces::OwnSpace& own) {
  const protocol::Json& lan = own.store().held("dashboard.lan");
  return lan.is_boolean() && lan.get<bool>();
}

// The PC's local date, as a device's "paired" (YYYY-MM-DD).
std::string today() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char text[16] = {};
  std::snprintf(text, sizeof text, "%04u-%02u-%02u", static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
                static_cast<unsigned>(now.wDay));
  return text;
}

// The devices the server paired since GraceUI's thread last looked: kept, and shown on the sheet that paired them.
void take_paired_devices() {
  if (g_server == nullptr) {
    return;
  }
  bool any = false;
  for (grace::serve::PairedDevice& fresh : g_server->take_paired()) {
    grace::spaces::PairedDeviceRecord device;
    device.id = fresh.id;
    device.name = fresh.name;
    device.paired = today();
    device.hash = fresh.hash;
    if (devices().size() >= grace::serve::kMaxDevices || !grace::serve::valid_device_id(device.id) ||
        !grace::serve::valid_device_name(device.name)) {
      continue;  // never: the server refuses a seventeenth, and names and ids it did not check
    }
    devices().push_back(std::move(device));
    any = true;
    log_line("a device was paired with the browser view (%u paired)", static_cast<unsigned>(devices().size()));
    if (g_pairing != nullptr && g_pairing->active) {
      g_pairing->paired = true;
      g_pairing->paired_name = fresh.name;
    }
  }
  if (any) {
    keep_devices();
    g_server->set_devices(grace::spaces::device_keys(devices()));
  }
}

// What a mod's caps say it uses, in the Mods page's words; a name GraceUI does not know is left out.
std::string uses_words(std::string_view caps) {
  static constexpr std::pair<std::string_view, std::string_view> kWords[] = {
      {GRACEUI_CAP_PAGES, "pages"},         {GRACEUI_CAP_NOTICES, "notices"},
      {GRACEUI_CAP_PANELS, "panels"},       {GRACEUI_CAP_TEXTURES, "textures"},
      {GRACEUI_CAP_FACES, "faces"},         {GRACEUI_CAP_INPUT, "input"},
      {GRACEUI_CAP_HOLDER_EVENTS, "input events"}, {GRACEUI_CAP_HOTKEYS, "hotkeys"},
      {GRACEUI_CAP_WORLD_STATE, "world state"},    {GRACEUI_CAP_DISPLAY, "the display"},
      {GRACEUI_CAP_PAD_CHORDS, "pad chords"},      {GRACEUI_CAP_HOTKEY_BINDINGS, "hotkey bindings"},
      {GRACEUI_CAP_WAKE, "a wait handle"},         {GRACEUI_CAP_HOTKEY_BY, "what pressed a hotkey"},
      {GRACEUI_CAP_PLAIN_TEXT, "Plain text"},
  };
  std::string out;
  for (const auto& [cap, word] : kWords) {
    bool named = false;
    for (std::size_t at = 0; at < caps.size();) {
      const std::size_t end = std::min(caps.find(' ', at), caps.size());
      named = named || caps.substr(at, end - at) == cap;
      at = end + 1;
    }
    if (named) {
      out += out.empty() ? std::string(word) : ", " + std::string(word);
    }
  }
  if (out.empty()) {
    return "Nothing named";
  }
  if (out[0] >= 'a' && out[0] <= 'z') {
    out[0] = static_cast<char>(out[0] - 'a' + 'A');  // "Plain text" is a capital already (D268)
  }
  return out;
}

// The Mods page's topics (D181): the mods connected now, with their notices' switch, and the quiet mods' kept notices.
// Built on GraceUI's thread at every wake and published only when they change.
protocol::Json mods_body() {
  protocol::Json rows = protocol::Json::array();
  grace::abi::ConsumerInfo info;
  for (std::uint32_t i = 0; session::consumers().info(i, info); ++i) {
    rows.push_back({{"id", info.id},
                    {"mod", info.name},
                    {"version", std::to_string(GRACEUI_VERSION_MAJOR(info.version)) + "." +
                                    std::to_string(GRACEUI_VERSION_MINOR(info.version)) + "." +
                                    std::to_string(GRACEUI_VERSION_PATCH(info.version))},
                    {"uses", uses_words(info.caps)},
                    {"notices", session::quiet().quiet(info.id) ? "Quiet" : "Shown"}});
  }
  return {{"columns",
           {{{"id", "mod"}, {"label", "Mod"}},
            {{"id", "version"}, {"label", "Version"}, {"numeric", true}},
            {{"id", "uses"}, {"label", "Uses"}},
            {{"id", "notices"},
             {"label", "Notices"},
             {"switch", {{"command", "mods.notices"}, {"key", "id"}, {"on", "Shown"}, {"off", "Quiet"}}}}}},
          {"rows", std::move(rows)}};
}

// The Hotkeys page's list (D206): the menu's key, then every mod's hotkeys with their defaults, and for each default
// another took first, whose it is. Built on GraceUI's thread at every wake, published only when it changes.
protocol::Json hotkeys_body() {
  static grace::input::Listed listed[grace::input::kMaxHotkeys];  // GraceUI's thread alone; kept off its stack
  const std::size_t count = input::hotkeys().list(listed, grace::input::kMaxHotkeys);
  const auto mod_name = [](std::uint16_t owner) {
    grace::abi::ConsumerInfo info;
    return session::consumers().info_of(owner, info) ? std::string(info.name) : std::string("A mod that has gone");
  };
  const grace::input::Key menu = input::menu_key();
  // Whose a default is: the hotkey that has it bound, or the menu's.
  const auto key_holder = [&](const grace::input::Listed& one) -> std::string {
    if (one.key == menu) {
      return "the menu";
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (listed[i].key_bound && listed[i].key == one.key && listed[i].modifiers == one.modifiers) {
        return mod_name(listed[i].owner);
      }
    }
    return "another hotkey";
  };
  const auto chord_holder = [&](const grace::input::Listed& one) -> std::string {
    for (std::size_t i = 0; i < count; ++i) {
      if (listed[i].chord_bound && listed[i].chord == one.chord) {
        return mod_name(listed[i].owner);
      }
    }
    return "another hotkey";
  };
  protocol::Json rows = protocol::Json::array();
  rows.push_back({{"mod", "GraceUI"},
                  {"action", "Open the menu"},
                  {"key", grace::input::key_text(menu, 0)},
                  {"chord", "No pad chord"},
                  {"state", "Bound"}});
  for (std::size_t i = 0; i < count; ++i) {
    const grace::input::Listed& one = listed[i];
    const bool has_key = one.key != grace::input::kNoKey;
    const bool has_chord = one.chord != 0;
    const std::string key = has_key ? grace::input::key_text(one.key, one.modifiers) : "No key";
    const bool key_lost = has_key && !one.key_bound;
    const bool chord_lost = has_chord && !one.chord_bound;
    std::string state;
    if (!has_key && !has_chord) {
      state = "Unbound: no key or pad chord";
    } else if (!key_lost && !chord_lost) {
      state = "Bound";
    } else {
      // A default another took first; with both named and one lost, the other still fires it.
      const std::string key_words = one.key == menu ? key + " opens the menu" : key_holder(one) + " took " + key + " first";
      const std::string chord_words = chord_holder(one) + " took its chord first";
      if (key_lost && chord_lost) {
        state = "Unbound: " + key_words + "; " + chord_words;
      } else if (key_lost) {
        state = (has_chord ? "Key unbound: " : "Unbound: ") + key_words;
      } else {
        state = (has_key ? "Chord unbound: " : "Unbound: ") + chord_words;
      }
    }
    if (one.paused && (has_key || has_chord)) {
      // Paused by its mod (ABI 1.11, D280): neither fires nor keeps its key or chord, whatever else is said of them.
      state = state == "Bound" ? std::string("Paused by its mod for now") : "Paused by its mod for now; " + state;
    }
    grace::abi::ConsumerInfo info;
    const std::string row_id = session::consumers().info_of(one.owner, info)
                                   ? std::string(info.id) + "/" + std::string(one.action()) : std::string();
    rows.push_back({{"id", row_id},
                    {"mod", mod_name(one.owner)},
                    {"action", one.name().empty() ? std::string(one.action()) : std::string(one.name())},
                    {"key", key},
                    {"chord", has_chord ? grace::input::chord_text(one.chord) : std::string("No pad chord")},
                    {"state", std::move(state)}});
  }
  return {{"columns",
           {{{"id", "mod"}, {"label", "Mod"}},
            {{"id", "action"}, {"label", "Action"}},
            {{"id", "key"}, {"label", "Key"}},
            {{"id", "chord"}, {"label", "Pad chord"}},
            {{"id", "state"}, {"label", "State"}}}},
          {"rows", std::move(rows)}};
}

protocol::Json notices_body() {
  protocol::Json rows = protocol::Json::array();
  for (const grace::abi::QuietNotices::Kept& kept : session::quiet().kept()) {
    rows.push_back({{"mod", kept.name}, {"text", kept.text}});
  }
  return {{"columns", {{{"id", "mod"}, {"label", "Mod"}}, {{"id", "text"}, {"label", "Notice"}}}},
          {"rows", std::move(rows)}};
}

#if defined(GRACEUI_TEST_EXPORTS)
// What GraceUI_Test_GetTopic reads: a copy of each topic's text, under a lock, since a test reads while GraceUI's thread
// may be replacing the body. Made at its first use, never destroyed.
constinit grace::abi::SpinLock g_test_topics_lock;
std::map<std::string, std::string, std::less<>>& test_topics() {
  static auto* const made = new std::map<std::string, std::string, std::less<>>();
  return *made;
}
#endif

// About and the Browser view's status (D184): what GraceUI is, and where and whether the browser view is served.
protocol::Json item(const char* label, std::string value) { return {{"label", label}, {"value", std::move(value)}}; }

protocol::Json about_body() {
  return {{"items",
           {item("Version", "GraceUI " + version_text()),
            item("Interface", "ABI 1." + std::to_string(GRACEUI_ABI1_MINOR) + ", protocol " +
                                  std::string(protocol::kProtocolVersion)),
            item("Licence", "MIT"), item("Built on", "Dear ImGui and MinHook in game; Preact in the browser"),
            item("Type", "Cinzel, EB Garamond, Noto Sans and Noto Sans Mono, under the SIL Open Font Licence")}}};
}

protocol::Json dashboard_body(const grace::spaces::OwnSpace& own) {
  const protocol::Json& enabled = own.store().held("dashboard.enabled");
  std::string address;
  if (g_running.load(std::memory_order_acquire)) {
    address = "http://127.0.0.1:" + std::to_string(g_port.load(std::memory_order_relaxed)) + "/";
  } else if (session::notice_only() || g_closed.load(std::memory_order_acquire)) {
    address = "Closed for this session: an anti-cheat was found";
  } else if (!enabled.is_boolean() || !enabled.get<bool>()) {
    address = "Off: the browser view is switched off";
  } else if (g_start_failed.load(std::memory_order_relaxed)) {
    address = "Not served: it could not start (the log says why)";
  } else {
    address = "Not served: no mod's pages ask for the browser view";
  }
  const bool running = g_server != nullptr && g_running.load(std::memory_order_acquire);
  const std::size_t open = running ? g_server->stats().websockets : 0;
  // Other devices (G9): never the PC's own network address, which only the pairing sheet shows, in game (PV3).
  std::string other;
  if (!lan_on(own)) {
    other = "Off";
  } else if (!running) {
    other = "On, once the browser view is served";
  } else if (g_server->network_addresses().empty()) {
    other = "On, but this PC has no home network address to listen on";
  } else {
    other = "On: served on this PC's home network too";
  }
  protocol::Json items = {item("Address", std::move(address)), item("Browsers connected", std::to_string(open)),
                          item("Other devices", std::move(other))};
  if (lan_on(own) || !devices().empty()) {
    const std::size_t connected = running ? g_server->connected_devices().size() : 0;
    items.push_back(item("Devices connected", std::to_string(connected) + " of " + std::to_string(devices().size()) +
                                                   " paired"));
  }
  return {{"items", std::move(items)}};
}

// The devices paired (G9): each listed, whether Other devices is on or off, with its Forget (the table's row action).
protocol::Json devices_body() {
  const std::vector<std::string> connected = g_server != nullptr && g_running.load(std::memory_order_acquire)
                                                 ? g_server->connected_devices()
                                                 : std::vector<std::string>();
  protocol::Json rows = protocol::Json::array();
  for (const grace::spaces::PairedDeviceRecord& device : devices()) {
    const bool now = std::find(connected.begin(), connected.end(), device.id) != connected.end();
    rows.push_back({{"id", device.id},
                    {"name", device.name},
                    {"paired", device.paired},
                    {"state", now ? "Connected" : "Not connected"}});
  }
  return {{"columns",
           {{{"id", "name"}, {"label", "Device"}},
            {{"id", "paired"}, {"label", "Paired"}},
            {{"id", "state"}, {"label", "Now"}}}},
          {"rows", std::move(rows)}};
}

void refresh_topics(grace::spaces::OwnSpace& own) {
  for (const auto& [topic, body] : {std::pair<const char*, protocol::Json>{"graceui.mods", mods_body()},
                                    std::pair<const char*, protocol::Json>{"graceui.notices", notices_body()},
                                    std::pair<const char*, protocol::Json>{"graceui.hotkeys", hotkeys_body()},
                                    std::pair<const char*, protocol::Json>{"graceui.about", about_body()},
                                    std::pair<const char*, protocol::Json>{"graceui.dashboard", dashboard_body(own)},
                                    std::pair<const char*, protocol::Json>{"graceui.devices", devices_body()}}) {
    const protocol::Json* now = own.state_of(topic);
    if (now == nullptr || *now != body) {
#if defined(GRACEUI_TEST_EXPORTS)
      std::string text = body.dump();
      g_test_topics_lock.lock();
      test_topics()[topic] = std::move(text);
      g_test_topics_lock.unlock();
#endif
      own.set_state(topic, body);
    }
  }
}

// mods.notices, from either view (D181): {"id": a connected mod's id, "on": whether its notices show}.
grace::pages::Outcome mods_notices(const protocol::Cmd& command) {
  grace::pages::Outcome out;
  if (command.name != "mods.notices") {
    out.error = protocol::ErrorCode::unknown_command;
    out.detail = "Not available in this version.";
    return out;
  }
  const protocol::Json& args = command.args;
  if (!args.is_object() || !args.contains("id") || !args["id"].is_string() || !args.contains("on") ||
      !args["on"].is_boolean()) {
    out.error = protocol::ErrorCode::bad_message;
    return out;
  }
  const std::string& id = args["id"].get_ref<const std::string&>();
  std::uint32_t owner = 0;
  grace::abi::ConsumerInfo info;
  for (std::uint32_t i = 0; owner == 0 && session::consumers().info(i, info); ++i) {
    owner = id == info.id ? info.owner : 0;
  }
  if (owner == 0) {
    out.error = protocol::ErrorCode::invalid_value;
    out.detail = "That mod is not connected now.";
    return out;
  }
  const bool quiet = !args["on"].get<bool>();
  if (!session::quiet().set(id, quiet)) {
    out.error = protocol::ErrorCode::invalid_value;
    out.detail = "Too many mods are quiet already.";
    return out;
  }
  if (quiet) {
    session::notices().withdraw_owner(owner, input::now_ms());  // what it shows now goes too
  }
  log_line("%s's notices are %s", info.id, quiet ? "quiet: kept on the Mods page" : "shown again");
  if (grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire); own != nullptr) {
    refresh_topics(*own);
  }
  keep_own_settings();
  out.ok = true;
  return out;
}

// ---- The Hotkeys page's changes (G37, D208).

// A hotkey by its row's id, "‹mod id›/‹action›": false if no connected mod has it now.
bool hotkey_of_row(std::string_view row, grace::input::Listed& out, std::string& mod_id) {
  const std::size_t slash = row.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= row.size()) {
    return false;
  }
  const std::string_view mod = row.substr(0, slash);
  const std::string_view action = row.substr(slash + 1);
  grace::abi::ConsumerInfo info;
  std::uint32_t owner = 0;
  for (std::uint32_t i = 0; owner == 0 && session::consumers().info(i, info); ++i) {
    owner = mod == info.id ? info.owner : 0;
  }
  if (owner == 0) {
    return false;
  }
  static grace::input::Listed listed[grace::input::kMaxHotkeys];  // GraceUI's thread alone
  const std::size_t count = input::hotkeys().list(listed, grace::input::kMaxHotkeys);
  for (std::size_t i = 0; i < count; ++i) {
    if (listed[i].owner == owner && listed[i].action() == action) {
      out = listed[i];
      mod_id = std::string(mod);
      return true;
    }
  }
  return false;
}

// A hotkey's binding as it stands now, kept for the next session under its mod's and action's ids.
void keep_binding(std::uint32_t id) {
  grace::input::Listed now;
  grace::abi::ConsumerInfo info;
  if (input::hotkeys().find(id, now) && session::consumers().info_of(now.owner, info)) {
    input::kept_bindings().keep(info.id, now.action(), grace::input::Chosen{now.key, now.modifiers, now.chord});
  }
}

// "Tide Charts's Show the tide table": whose a hotkey is, in words.
std::string whose(std::uint32_t id) {
  grace::input::Listed one;
  grace::abi::ConsumerInfo info;
  if (!input::hotkeys().find(id, one) || !session::consumers().info_of(one.owner, info)) {
    return "another hotkey";
  }
  return std::string(info.name) + "'s " + std::string(one.name().empty() ? one.action() : one.name());
}

grace::pages::Outcome refusal(protocol::ErrorCode code, std::string words) {
  grace::pages::Outcome out;
  out.error = code;
  out.detail = std::move(words);
  return out;
}

// hotkeys.key, hotkeys.chord, hotkeys.reset and hotkeys.reset_all, from either view (D208): {"id": a row's id,
// "value": a key's name ("" for none) or a chord (0 for none), "confirm": true to move one another hotkey has}.
grace::pages::Outcome hotkeys_command(const protocol::Cmd& command) {
  const protocol::Json& args = command.args;
  if (command.name == "hotkeys.reset_all") {
    static grace::input::Listed listed[grace::input::kMaxHotkeys];
    const std::size_t count = input::hotkeys().list(listed, grace::input::kMaxHotkeys);
    for (std::size_t i = 0; i < count; ++i) {
      input::hotkeys().reset(listed[i].id, input::menu_key());
    }
    input::kept_bindings().forget_all();
    log_line("every hotkey is back to its mod's own keys");
    grace::pages::Outcome out;
    out.ok = true;
    return out;
  }
  if (!args.is_object() || !args.contains("id") || !args["id"].is_string()) {
    return refusal(protocol::ErrorCode::bad_message, "");
  }
  grace::input::Listed hotkey;
  std::string mod_id;
  if (!hotkey_of_row(args["id"].get_ref<const std::string&>(), hotkey, mod_id)) {
    return refusal(protocol::ErrorCode::invalid_value, "That mod is not connected now.");
  }
  const bool move = args.contains("confirm") && args["confirm"].is_boolean() && args["confirm"].get<bool>();
  grace::input::Taken other;
  grace::input::Rebound done = grace::input::Rebound::gone;
  if (command.name == "hotkeys.reset") {
    done = input::hotkeys().reset(hotkey.id, input::menu_key());
    if (done == grace::input::Rebound::done) {
      input::kept_bindings().forget(mod_id, hotkey.action());
    }
  } else if (command.name == "hotkeys.key") {
    grace::input::Key key = grace::input::kNoKey;
    std::uint8_t modifiers = 0;
    if (!args.contains("value") || !args["value"].is_string() ||
        !grace::input::key_from_text(args["value"].get_ref<const std::string&>(), key, modifiers)) {
      return refusal(protocol::ErrorCode::invalid_value, "Not a key GraceUI knows.");
    }
    done = input::hotkeys().rebind_key(hotkey.id, key, modifiers, input::menu_key(), move, other);
    if (done == grace::input::Rebound::taken) {
      const std::string name = grace::input::key_text(key, modifiers);
      if (other.menu) {
        return refusal(protocol::ErrorCode::invalid_value,
                       name + " opens the menu: give the menu another key first, or choose another here.");
      }
      return refusal(protocol::ErrorCode::confirm_required,
                     name + " is " + whose(other.id) + ". Use it here, and leave that one with no key?");
    }
  } else if (command.name == "hotkeys.chord") {
    if (!args.contains("value") || !args["value"].is_number_unsigned()) {
      return refusal(protocol::ErrorCode::invalid_value, "Not a pad chord.");
    }
    const auto chord = args["value"].get<std::uint64_t>();
    if (chord > 0xFFFFFFFFull) {
      return refusal(protocol::ErrorCode::invalid_value, "Not a pad chord.");
    }
    done = input::hotkeys().rebind_chord(hotkey.id, static_cast<std::uint32_t>(chord), move, other);
    if (done == grace::input::Rebound::taken) {
      return refusal(protocol::ErrorCode::confirm_required,
                     grace::input::chord_text(static_cast<std::uint32_t>(chord)) + " is " + whose(other.id) +
                         ". Use it here, and leave that one with no pad chord?");
    }
  } else {
    return refusal(protocol::ErrorCode::unknown_command, "Not available in this version.");
  }
  if (done == grace::input::Rebound::invalid) {
    return refusal(protocol::ErrorCode::invalid_value,
                   command.name == "hotkeys.chord"
                       ? "A pad chord is two different buttons: not a trigger or a stick."
                       : "Escape, Alt+F4, and the Ctrl, Shift, Alt and Windows keys alone cannot be a hotkey.");
  }
  if (done == grace::input::Rebound::gone) {
    return refusal(protocol::ErrorCode::invalid_value, "That hotkey has gone.");
  }
  if (command.name != "hotkeys.reset") {
    keep_binding(hotkey.id);
  }
  if (other.id != 0) {
    keep_binding(other.id);  // the one that lost it stays without it, whichever mod registers first next time
  }
  grace::input::Listed now;
  input::hotkeys().find(hotkey.id, now);
  log_line("%s's hotkey %.*s: key %s, pad chord %s", mod_id.c_str(), static_cast<int>(now.action().size()),
           now.action().data(), now.key == grace::input::kNoKey ? "none" : grace::input::key_text(now.key, now.modifiers).c_str(),
           now.chord == 0 ? "none" : grace::input::chord_text(now.chord).c_str());
  grace::pages::Outcome out;
  out.ok = true;
  return out;
}

// dashboard.forget_device, in game or from this PC's browser (never a device's: sensitive, §7.2 item 16): {"id": a
// paired device's id}. Revoked at once: the server closes its connections, and the file forgets it.
grace::pages::Outcome forget_device(const protocol::Cmd& command) {
  const protocol::Json& args = command.args;
  if (!args.is_object() || !args.contains("id") || !args["id"].is_string() ||
      !grace::serve::valid_device_id(args["id"].get_ref<const std::string&>())) {
    return refusal(protocol::ErrorCode::invalid_value, "Not a device GraceUI knows.");
  }
  const std::string& id = args["id"].get_ref<const std::string&>();
  std::vector<grace::spaces::PairedDeviceRecord>& list = devices();
  const auto found = std::find_if(list.begin(), list.end(), [&](const auto& d) { return d.id == id; });
  if (found == list.end()) {
    return refusal(protocol::ErrorCode::invalid_value, "That device is not paired: it may have been forgotten already.");
  }
  list.erase(found);
  keep_devices();
  if (g_server != nullptr) {
    g_server->set_devices(grace::spaces::device_keys(list));
  }
  log_line("a device was forgotten (%u paired)", static_cast<unsigned>(list.size()));
  grace::pages::Outcome out;
  out.ok = true;
  return out;
}

// GraceUI's own commands past its settings (invariant 3): one entry point each.
grace::pages::Outcome own_command(const protocol::Cmd& command) {
  grace::pages::Outcome out =
      command.name.rfind("hotkeys.", 0) == 0   ? hotkeys_command(command)
      : command.name == "dashboard.forget_device" ? forget_device(command)
      : command.name == "dashboard.pair"
          // The menu answers it in game; a browser never shows a code (D66).
          ? refusal(protocol::ErrorCode::not_now,
                    "Only in game: choose Pair a device on GraceUI's Browser view page there, where the code is shown.")
          : mods_notices(command);
  if (out.ok) {
    wake::signal_owners(input::hotkeys().changed_owners());  // their mods' polls have the change (D210, D212)
    if (grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire); own != nullptr) {
      refresh_topics(*own);
    }
    keep_own_settings();
  }
  return out;
}

// hotkeys.menu's own rules past a key name's (D208): one key alone, never Escape or a modifier.
std::optional<grace::pages::Outcome> check_setting(const protocol::Cmd& command) {
  const protocol::Json& args = command.args;
  if (!args.is_object() || args.value("key", std::string()) != "hotkeys.menu" || !args.contains("value") ||
      !args["value"].is_string()) {
    return std::nullopt;
  }
  grace::input::Key key = grace::input::kNoKey;
  std::uint8_t modifiers = 0;
  if (!grace::input::key_from_text(args["value"].get_ref<const std::string&>(), key, modifiers)) {
    return refusal(protocol::ErrorCode::invalid_value, "Not a key GraceUI knows.");
  }
  if (modifiers != 0) {
    return refusal(protocol::ErrorCode::invalid_value, "The menu's key takes no Ctrl, Shift or Alt.");
  }
  if (key == grace::input::kNoKey || grace::input::reserved_key(key)) {
    return refusal(protocol::ErrorCode::invalid_value, "Escape and the Ctrl, Shift, Alt and Windows keys cannot open the menu.");
  }
  return std::nullopt;
}

void apply_menu_key(const grace::spaces::OwnSpace& own);

}  // namespace

// hotkeys.menu as the player set it, applied (D208): a mod's hotkey on the new key loses it, kept so once the player
// moved it; at the start it is only applied.
void publish_menu_key(const grace::spaces::OwnSpace& own) noexcept {
  try {
    apply_menu_key(own);
  } catch (...) {
    log_line("the menu's key could not be applied; it stays as it was");
  }
}

namespace {

void apply_menu_key(const grace::spaces::OwnSpace& own) {
  const protocol::Json& value = own.store().held("hotkeys.menu");
  grace::input::Key key = grace::input::kNoKey;
  std::uint8_t modifiers = 0;
  if (!value.is_string() || !grace::input::key_from_text(value.get_ref<const std::string&>(), key, modifiers) ||
      modifiers != 0 || key == grace::input::kNoKey || grace::input::reserved_key(key)) {
    key = grace::input::kF5;  // a value the check never let through (an edited file): the default
  }
  if (key == input::menu_key() && g_menu_key_applied) {
    return;
  }
  input::set_menu_key(key);
  std::uint32_t lost[grace::input::kMaxHotkeys];
  const std::size_t count = input::hotkeys().menu_key_moved(key, lost, grace::input::kMaxHotkeys);
  for (std::size_t i = 0; i < count && g_menu_key_applied; ++i) {
    keep_binding(lost[i]);
  }
  wake::signal_owners(input::hotkeys().changed_owners());
  if (g_menu_key_applied || key != grace::input::kF5) {
    log_line("the menu's key is %s", grace::input::key_text(key, 0).c_str());
  }
  g_menu_key_applied = true;
}

}  // namespace

void publish_ui_scale(const grace::spaces::OwnSpace& own) noexcept {
  const protocol::Json& value = own.store().held("ui.scale");
  const protocol::Setting* setting = own.store().find("ui.scale");
  const double default_value = setting != nullptr && setting->default_.is_number() ? setting->default_.get<double>() : 1.0;
  // ui.scale, its default and Plain text together, one display event for a change of any of them (D180, D268).
  display::set_settings(static_cast<float>(value.is_number() ? value.get<double>() : default_value),
                        static_cast<float>(default_value), own.store().held("ui.menu.plain_text") == true);
}

void keep_own_settings() noexcept {
  grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire);
  if (own == nullptr || (own->revision() == g_kept_revision && session::quiet().ids_revision() == g_kept_quiet &&
                         input::kept_bindings().revision() == g_kept_bindings)) {
    return;
  }
  g_kept_revision = own->revision();
  g_kept_quiet = session::quiet().ids_revision();
  g_kept_bindings = input::kept_bindings().revision();
  try {
    std::filesystem::path path;
    if (!settings_path(path)) {
      throw std::runtime_error("no folder");
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path partial = path;
    partial += L".partial";
    {
      std::ofstream out(partial, std::ios::binary | std::ios::trunc);
      protocol::Json file = {{"graceui_settings", 1}, {"values", own->saved_settings()}};
      if (const std::vector<std::string> quiet = session::quiet().ids(); !quiet.empty()) {
        file["quiet_mods"] = quiet;  // the mods whose notices the player quieted (D181)
      }
      static grace::input::KeptBindings::Entry kept[grace::input::kKeptBindings];  // GraceUI's thread alone
      if (const std::size_t count = input::kept_bindings().all(kept, grace::input::kKeptBindings); count != 0) {
        protocol::Json bindings = protocol::Json::array();
        for (std::size_t i = 0; i < count; ++i) {
          bindings.push_back({{"mod", std::string(kept[i].mod_id())},
                              {"action", std::string(kept[i].action_id())},
                              {"key", grace::input::key_text(kept[i].chosen.key, kept[i].chosen.modifiers)},
                              {"chord", kept[i].chosen.chord}});
        }
        file["hotkey_bindings"] = std::move(bindings);  // the player's hotkey bindings (D208): ids and key names only
      }
      out << file.dump(2) << '\n';
      if (!out.flush()) {
        throw std::runtime_error("not written");
      }
    }
    // Whole or not at all: the new file takes the old one's place in one step.
    if (MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
      throw std::runtime_error("not replaced");
    }
    g_keep_failed = false;
  } catch (...) {
    if (!g_keep_failed) {
      log_line("GraceUI's settings could not be kept for the next session (its user folder is not writable)");
    }
    g_keep_failed = true;
  }
}

grace::spaces::OwnSpace* own() noexcept {
  if (g_own.load(std::memory_order_acquire) == nullptr && !g_own_failed) {
    try {
      auto* own = new grace::spaces::OwnSpace();
      std::string why;
      if (!own->load(why)) {
        g_own_failed = true;
        log_line("GraceUI's own space does not load: %s", why.c_str());
        delete own;
        return nullptr;
      }
      restore_saved(*own);  // before any view or bake reads the store
      restore_devices();
      publish_ui_scale(*own);
      g_own.store(own, std::memory_order_release);
    } catch (...) {
      g_own_failed = true;
      log_line("GraceUI's own space does not load (an exception)");
    }
  }
  return g_own.load(std::memory_order_acquire);
}

grace::spaces::OwnSpace* loaded_own() noexcept { return g_own.load(std::memory_order_acquire); }

void serve() noexcept {
  try {
    grace::spaces::OwnSpace* own_space = own();
    if (own_space == nullptr) {
      return;
    }
    if (g_server == nullptr) {
      g_server = new grace::serve::Server();
      registry().attach(*g_server);
      own_space->attach(*g_server, version_text(),
                        grace::spaces::OwnSpace::Hooks{[] { disconnect_browsers(); }, &own_command, &check_setting},
                        [] { hooks::wake(); });
      g_server->set_devices(grace::spaces::device_keys(devices()));  // kept by the server across starts and stops
    }
    // GraceUI's own module re-check, every 60 s while the server runs (§7.2 item 1): an anti-cheat loaded since the
    // first request closes it for the session.
    if (g_server->running() && steady_ms() >= g_next_recheck_ms) {
      g_next_recheck_ms = steady_ms() + recheck_interval_ms();
      if (anticheat::module_loaded()) {
        close_for_anti_cheat("GraceUI's module re-check found an anti-cheat");
      }
    }
    const bool want = wanted(*own_space);
    if (want && !g_server->running() && !g_start_failed.load(std::memory_order_relaxed)) {
      start(*own_space);
    } else if (!want && g_server->running()) {
      g_server->stop();
      g_running.store(false, std::memory_order_release);
      g_port.store(0, std::memory_order_relaxed);
      log_line("the browser view is stopped: no socket is open");
    }
    // Other devices (G9): dashboard.lan to the server, which listens on the home network while it runs; and the devices
    // it paired since the last wake, kept.
    const bool lan = lan_on(*own_space);
    if (lan != (g_lan_applied == 1)) {
      g_server->set_network(lan);
      log_line("other devices at home: %s", lan ? "on" : "off");
    }
    g_lan_applied = lan ? 1 : 0;
    take_paired_devices();
    refresh_topics(*own_space);  // the Mods page: a mod came or went, or a quiet one posted (D181)
    own_space->serve();
    registry().expire(std::chrono::steady_clock::now());
    // A change from a browser while the menu is closed: the mods hear of it, and the next session keeps it (D179).
    publish_ui_scale(*own_space);
    publish_menu_key(*own_space);  // hotkeys.menu, live since D208
    keep_own_settings();
  } catch (...) {
    log_line("the spaces' service failed (an exception): the browser view may lag until the next wake");
  }
}

bool wants_timer() noexcept { return g_running.load(std::memory_order_acquire); }

void close_for_anti_cheat(const char* source) noexcept {
  if (!g_closed.exchange(true, std::memory_order_acq_rel)) {
    log_line("%s: the browser view is closed for the rest of the session", source);
    hooks::wake();  // GraceUI's thread stops the server at once
  }
}

void disconnect_browsers() noexcept {
  if (g_server != nullptr && g_running.load(std::memory_order_acquire)) {
    g_server->renew_session();
  }
}

PairingStart start_pairing() noexcept {
  PairingStart out;
  try {
    grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire);
    if (own == nullptr || !lan_on(*own)) {
      out.words = "Turn on Other devices on your home network first.";
      return out;
    }
    if (g_server == nullptr || !g_running.load(std::memory_order_acquire)) {
      out.words = "The browser view is not served now: its address above says why.";
      return out;
    }
    const std::vector<std::string> addresses = g_server->network_addresses();
    if (addresses.empty()) {
      out.words = "This PC has no home network address to listen on: is it connected to your router?";
      return out;
    }
    if (devices().size() >= grace::serve::kMaxDevices) {
      out.words = "Sixteen devices are paired already: forget one first.";
      return out;
    }
    const std::string code = g_server->start_pairing();
    if (code.size() != grace::serve::kCodeDigits) {
      out.words = "No code could be made just now: try again.";
      return out;
    }
    Pairing& p = pairing();
    p = Pairing{};
    p.active = true;
    p.code = code;
    p.expires = std::chrono::steady_clock::now() + grace::serve::kCodeLife;
    const std::string port = std::to_string(g_port.load(std::memory_order_relaxed));
    for (const std::string& address : addresses) {
      p.addresses.push_back("http://" + address + ":" + port + "/");
    }
    log_line("Pair a device: a code is shown in game");  // never the code, nor the address (PV3, PV5)
#if defined(GRACEUI_TEST_EXPORTS)
    test_code(code);
#endif
    out.ok = true;
  } catch (...) {
    out.words = "Not possible right now.";
  }
  return out;
}

void cancel_pairing() noexcept {
  if (g_pairing == nullptr || !g_pairing->active) {
    return;
  }
  if (!g_pairing->paired && g_server != nullptr) {
    g_server->cancel_pairing();  // the code is void at once (D113)
  }
#if defined(GRACEUI_TEST_EXPORTS)
  test_code({});
#endif
  try {
    *g_pairing = Pairing{};
  } catch (...) {
    g_pairing->active = false;
  }
}

PairingView pairing_view() noexcept {
  PairingView view;
  try {
    if (g_pairing == nullptr || !g_pairing->active) {
      return view;
    }
    Pairing& p = *g_pairing;
    if (!p.paired && !p.ended) {
      const grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire);
      const bool running = g_server != nullptr && g_running.load(std::memory_order_acquire);
      if (own == nullptr || !lan_on(*own)) {
        p.ended = true;
        p.why = "Other devices was switched off, so the code is void.";
      } else if (!running) {
        p.ended = true;
        p.why = "The browser view stopped, so the code is void.";
      } else if (g_server->pairing_code() != p.code) {
        take_paired_devices();  // used: by a device that just paired, or worn out
        if (!p.paired) {
          p.ended = true;
          p.why = "The code ran out, or was entered wrongly five times.";
        }
      }
      if (p.ended && g_server != nullptr) {
        g_server->cancel_pairing();
      }
#if defined(GRACEUI_TEST_EXPORTS)
      if (p.ended || p.paired) {
        test_code({});
      }
#endif
    }
    if (p.paired) {
      view.state = PairingView::State::paired;
      view.paired_name = p.paired_name;
    } else if (p.ended) {
      view.state = PairingView::State::ended;
      view.why = p.why;
    } else {
      view.state = PairingView::State::showing;
      view.code = p.code;
      const auto left = std::chrono::ceil<std::chrono::seconds>(p.expires - std::chrono::steady_clock::now()).count();
      view.seconds = static_cast<std::uint32_t>(std::max<long long>(0, left));
      view.addresses = p.addresses;
    }
  } catch (...) {
    view = PairingView{};
  }
  return view;
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_view(TestView& out) noexcept {
  out.running = g_running.load(std::memory_order_acquire) ? 1 : 0;
  out.port = g_port.load(std::memory_order_relaxed);
  out.spaces = static_cast<std::uint32_t>(registry().count());
  out.pending = static_cast<std::uint32_t>(registry().pending());
  out.failed = g_start_failed.load(std::memory_order_relaxed) ? 1 : 0;
}

void test_set_recheck_ms(std::uint32_t ms) noexcept { g_test_recheck_ms.store(ms, std::memory_order_relaxed); }

std::uint32_t test_pairing_code(char* out, std::uint32_t capacity) noexcept {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  g_test_pairing_lock.lock();
  const std::array<char, 8> code = g_test_code;
  g_test_pairing_lock.unlock();
  const std::size_t length = std::strlen(code.data());
  if (length == 0 || length + 1 > capacity) {
    return 0;
  }
  std::memcpy(out, code.data(), length + 1);
  return static_cast<std::uint32_t>(length);
}

void test_set_network_addresses(const std::uint8_t* addresses, std::uint32_t count) noexcept {
  g_test_pairing_lock.lock();
  g_test_address_count = addresses == nullptr ? 0 : std::min<std::uint32_t>(count, 4);
  for (std::uint32_t i = 0; i < g_test_address_count; ++i) {
    std::copy_n(addresses + 4 * i, 4, g_test_addresses[i].begin());
  }
  g_test_pairing_lock.unlock();
}

std::uint32_t test_topic(const char* topic, char* out, std::uint32_t capacity) noexcept {
  // Read on the caller's thread while GraceUI's thread may be writing: a test reads it only once GraceUI is idle.
  if (topic == nullptr || out == nullptr || capacity == 0) {
    return 0;
  }
  try {
    std::string text;
    g_test_topics_lock.lock();
    const auto found = test_topics().find(std::string_view(topic));
    const bool known = found != test_topics().end();
    if (known) {
      text = found->second;
    }
    g_test_topics_lock.unlock();
    if (!known || text.size() + 1 > capacity) {
      return 0;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return static_cast<std::uint32_t>(text.size());
  } catch (...) {
    return 0;
  }
}
#endif

}  // namespace grace::host::spaces
