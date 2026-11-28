// SPDX-License-Identifier: MIT
// The mock of ABI 1 (D285): its table, its answers as GraceUI's, what it records, the events it gives poll, and a mod's
// logic in C tested on it end to end.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "graceui_mock.h"

extern "C" {
GraceUiStatus c_mod_start(const GraceUiApi1* api);
int c_mod_poll(void);
}

namespace {

struct Connected {
  GraceUiStatus status = GRACEUI_REFUSED;
  GraceUiConnectAnswer answer{};
};

Connected connect(const char* id, std::uint32_t floor = 0, std::uint32_t abi_minor = GRACEUI_ABI1_MINOR) {
  GraceUiConnectRequest request{};
  request.size = sizeof request;
  request.abi_minor = abi_minor;
  request.version = GRACEUI_VERSION_PACK(1, 0, 0);
  request.floor = floor;
  request.id = id;
  request.id_length = std::strlen(id);
  request.name = "A mod";
  request.name_length = 5;
  Connected out;
  out.answer.size = sizeof out.answer;
  out.status = graceui_mock_api()->connect(&request, &out.answer);
  return out;
}

GraceUiStatus register_space(GraceUiConsumer me) {
  static const char pages[] = R"({"space":"a-mod","name":"A mod","minor":0,"categories":[],"pages":[],"topics":[],"commands":[]})";
  static const char settings[] = R"({"schema":{"number":1,"hash":"0"},"settings":[]})";
  GraceUiSpaceDesc space{};
  space.size = sizeof space;
  space.views = GRACEUI_VIEW_BROWSER;
  space.pages = pages;
  space.pages_length = sizeof pages - 1;
  space.settings = settings;
  space.settings_length = sizeof settings - 1;
  space.increment = "0.2";
  space.increment_length = 3;
  return graceui_mock_api()->register_pages(me, &space);
}

GraceUiStatus publish(GraceUiConsumer me, std::string_view frame) {
  return graceui_mock_api()->publish(me, frame.data(), frame.size());
}

GraceUiStatus notice(GraceUiConsumer me, std::string_view text, std::uint32_t key = 0) {
  GraceUiNotice n{};
  n.size = sizeof n;
  n.key = key;
  n.text = text.data();
  n.text_length = text.size();
  return graceui_mock_api()->notice(me, &n);
}

struct Polled {
  GraceUiStatus status = GRACEUI_REFUSED;
  GraceUiEvent event{};
  std::string frame;
};

Polled poll(GraceUiConsumer me, std::uint32_t size = sizeof(GraceUiEvent), std::uint64_t capacity = 4096) {
  static char buffer[GRACEUI_FRAME_FROM_PAGE_MAX];
  Polled out;
  out.event.size = size;
  out.event.frame = buffer;
  out.event.frame_capacity = capacity;
  out.status = graceui_mock_api()->poll(me, &out.event);
  if (out.status == GRACEUI_OK && out.event.frame_length != 0) {
    out.frame.assign(buffer, static_cast<std::size_t>(out.event.frame_length));
  }
  return out;
}

std::uint64_t make_panel(GraceUiConsumer me, std::uint32_t layer = GRACEUI_LAYER_PANEL,
                         std::uint32_t flags = GRACEUI_PANEL_TAKES_INPUT) {
  GraceUiPanelDesc desc{};
  desc.size = sizeof desc;
  desc.layer = layer;
  desc.flags = flags;
  desc.width = 100;
  desc.height = 50;
  std::uint64_t id = 0;
  REQUIRE(graceui_mock_api()->create_panel(me, &desc, &id) == GRACEUI_OK);
  return id;
}

GraceUiInputAnswer request_input(GraceUiConsumer me, std::uint64_t panel, std::uint32_t flags = 0,
                                 std::uint32_t kind = GRACEUI_HOLDER_PANEL, GraceUiStatus* status = nullptr) {
  GraceUiInputRequest request{};
  request.size = sizeof request;
  request.kind = kind;
  request.wants = GRACEUI_WANT_KEYBOARD;
  request.panel = panel;
  request.flags = flags;
  GraceUiInputAnswer answer{};
  answer.size = sizeof answer;
  const GraceUiStatus got = graceui_mock_api()->request_input(me, &request, &answer);
  if (status != nullptr) {
    *status = got;
  }
  return answer;
}

GraceUiHotkeyDesc hotkey_desc(std::uint32_t key, const char* action, std::uint32_t chord = 0) {
  GraceUiHotkeyDesc desc{};
  desc.size = sizeof desc;
  desc.key = key;
  desc.pad_chord = chord;
  desc.action = action;
  desc.action_length = std::strlen(action);
  desc.name = "A hotkey";
  desc.name_length = 8;
  return desc;
}

GraceUiStatus register_hotkey(GraceUiConsumer me, const GraceUiHotkeyDesc& desc, std::uint32_t* id = nullptr) {
  std::uint32_t got = 0;
  const GraceUiStatus status = graceui_mock_api()->register_hotkey(me, &desc, &got);
  if (id != nullptr) {
    *id = got;
  }
  return status;
}

GraceUiStatus world(GraceUiConsumer me, std::uint32_t flags) {
  GraceUiWorldState state{};
  state.size = sizeof state;
  state.flags = flags;
  return graceui_mock_api()->set_world_state(me, &state);
}

}  // namespace

TEST_CASE("a fresh mock: the table of this header's minor, this GraceUI's version and its caps but wake") {
  graceui_mock_reset();
  const GraceUiApi1* api = graceui_mock_api();
  CHECK(api->size == sizeof(GraceUiApi1));
  CHECK(api->abi_minor == GRACEUI_ABI1_MINOR);
  CHECK(api->graceui_version == GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR, GRACE_BUILD_PATCH));
  CHECK(api->graceui_version == GRACEUI_MOCK_VERSION);  // the build's define, which its users see too
  CHECK(GRACEUI_API1_COVERS(api, set_hotkey_enabled));
  for (const char* cap : {GRACEUI_CAP_NOTICES, GRACEUI_CAP_PAGES, GRACEUI_CAP_ON_PRESENT, GRACEUI_CAP_HOTKEY_PAUSE}) {
    CHECK(api->has_cap(cap, std::strlen(cap)) == 1);
  }
  CHECK(api->has_cap(GRACEUI_CAP_WAKE, 4) == 0);  // no wake event to give
  CHECK(api->has_cap("map", 3) == 0);
  CHECK(api->has_cap("page", 4) == 0);  // a cap's name whole, never its start
  graceui_mock_set_caps("notices panels");
  CHECK(api->has_cap("panels", 6) == 1);
  CHECK(api->has_cap("pages", 5) == 0);
  CHECK(graceui_mock_count(GRACEUI_MOCK_HAS_CAP) == 9);
}

TEST_CASE("an older minor: its table's size, its GraceUI's version and caps, and what it did not read") {
  graceui_mock_reset();
  graceui_mock_set_minor(4);
  const GraceUiApi1* api = graceui_mock_api();
  CHECK(api->abi_minor == 4);
  CHECK(api->graceui_version == GRACEUI_VERSION_PACK(0, 2, 16));
  CHECK(GRACEUI_API1_COVERS(api, describe_hotkey));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, wake_event));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, set_hotkey_enabled));
  CHECK(api->has_cap(GRACEUI_CAP_HOTKEY_BINDINGS, std::strlen(GRACEUI_CAP_HOTKEY_BINDINGS)) == 1);
  CHECK(api->has_cap(GRACEUI_CAP_ON_PRESENT, std::strlen(GRACEUI_CAP_ON_PRESENT)) == 0);
  graceui_mock_set_minor(2);
  const GraceUiConsumer me = connect("old-host-mod").answer.consumer;
  std::uint32_t id = 0;
  CHECK(register_hotkey(me, hotkey_desc(0x77, "trim", GRACEUI_PAD_CHORD(0x0001, 0x0020)), &id) == GRACEUI_OK);
  GraceUiHotkeyBinding binding{};
  binding.size = sizeof binding;
  graceui_mock_set_minor(4);  // describe it: the chord was never read by a GraceUI before 1.3
  CHECK(api->describe_hotkey(me, id, &binding) == GRACEUI_OK);
  CHECK(binding.bound == GRACEUI_HOTKEY_KEY_BOUND);
  CHECK(binding.pad_chord == 0);
  graceui_mock_set_minor(5);
  CHECK(graceui_mock_push_hotkey(me, id, 1) == 1);
  CHECK(poll(me).event.hotkey_by == 0);  // before ABI 1.7 nothing is written there
  graceui_mock_set_minor(0);
  CHECK(GRACEUI_API1_COVERS(api, log));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, set_caret));
  graceui_mock_set_version(GRACEUI_VERSION_PACK(0, 1, 9));
  CHECK(api->graceui_version == GRACEUI_VERSION_PACK(0, 1, 9));
  graceui_mock_set_minor(99);
  CHECK(api->abi_minor == GRACEUI_ABI1_MINOR);
}

TEST_CASE("connect answers as GraceUI does") {
  graceui_mock_reset();
  const Connected first = connect("first-mod");
  CHECK(first.status == GRACEUI_OK);
  CHECK(first.answer.consumer == 1);
  CHECK(first.answer.display_width == 1920);
  CHECK(first.answer.ui_scale == doctest::Approx(1.0));
  CHECK(connect("first-mod").status == GRACEUI_DUPLICATE_ID);
  CHECK(connect("second").answer.consumer == 2);
  const Connected bad = connect("No Caps");
  CHECK(bad.status == GRACEUI_REFUSED);
  CHECK(bad.answer.reason == GRACEUI_REFUSED_ID);
  CHECK(bad.answer.consumer == 0);
  graceui_mock_set_version(GRACEUI_VERSION_PACK(0, 2, 10));
  const Connected old = connect("needs-newer", GRACEUI_VERSION_PACK(0, 2, 70));
  CHECK(old.status == GRACEUI_TOO_OLD);
  CHECK(old.answer.floor == GRACEUI_VERSION_PACK(0, 2, 70));
  CHECK(old.answer.graceui_version == GRACEUI_VERSION_PACK(0, 2, 10));
  CHECK(graceui_mock_api()->connect(nullptr, nullptr) == GRACEUI_REFUSED);
  const GraceUiMockCall* last = graceui_mock_last(GRACEUI_MOCK_CONNECT);
  REQUIRE(last != nullptr);
  CHECK(last->status == GRACEUI_REFUSED);
  CHECK(std::string(graceui_mock_call(0)->text) == "first-mod");
  CHECK(graceui_mock_call(0)->value == GRACEUI_ABI1_MINOR);
}

TEST_CASE("the notice-only mode: inactive with a handle, notices with the anti-cheat's reason alone") {
  graceui_mock_reset();
  graceui_mock_set_inactive(GRACEUI_INACTIVE_ANTI_CHEAT);
  const Connected mod = connect("quiet-mod");
  CHECK(mod.status == GRACEUI_INACTIVE);
  CHECK(mod.answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT);
  REQUIRE(mod.answer.consumer != 0);
  CHECK(notice(mod.answer.consumer, "Only notices now.") == GRACEUI_OK);
  CHECK(register_space(mod.answer.consumer) == GRACEUI_INACTIVE);
  CHECK(poll(mod.answer.consumer).status == GRACEUI_INACTIVE);
  CHECK(graceui_mock_api()->log(mod.answer.consumer, GRACEUI_LOG_INFO, "x", 1) == GRACEUI_INACTIVE);
  CHECK(graceui_mock_api()->disconnect(mod.answer.consumer) == GRACEUI_OK);
  CHECK(notice(mod.answer.consumer, "Gone.") == GRACEUI_NOT_CONNECTED);
  graceui_mock_set_inactive(GRACEUI_INACTIVE_HOST_FAILED);
  const Connected dark = connect("dark-mod");
  CHECK(dark.status == GRACEUI_INACTIVE);
  CHECK(notice(dark.answer.consumer, "Nothing is drawn.") == GRACEUI_INACTIVE);
}

TEST_CASE("log: declared, not provided yet, as GraceUI's; its line kept for the test") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  CHECK(graceui_mock_api()->log(me, GRACEUI_LOG_WARNING, "a line", 6) == GRACEUI_NOT_YET);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_LOG)->text) == "a line");
}

TEST_CASE("a refused call's text is never read past its slot's bound") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  static const char tiny[] = "x";
  GraceUiNotice n{};
  n.size = sizeof n;
  n.text = tiny;
  n.text_length = GRACEUI_NOTICE_TEXT_MAX + 1;  // a length the mod's buffer does not have: ASan would see a read
  CHECK(graceui_mock_api()->notice(me, &n) == GRACEUI_REFUSED);
  CHECK(graceui_mock_last(GRACEUI_MOCK_NOTICE)->text_length == 0);
  n.text_length = UINT64_MAX;
  CHECK(graceui_mock_api()->notice(me, &n) == GRACEUI_REFUSED);
  CHECK(graceui_mock_api()->publish(me, tiny, UINT64_MAX) == GRACEUI_NOT_YET);
  CHECK(graceui_mock_last(GRACEUI_MOCK_PUBLISH)->text_length == 0);
}

TEST_CASE("a call's record holds until reset, however many calls come after it") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  REQUIRE(notice(me, "The first.") == GRACEUI_OK);
  const GraceUiMockCall* first = graceui_mock_last(GRACEUI_MOCK_NOTICE);
  for (int i = 0; i < 5000; ++i) {
    notice(me, "Another.");
  }
  CHECK(std::string(first->text) == "The first.");
  CHECK(graceui_mock_count(GRACEUI_MOCK_NOTICE) == 5001);
  CHECK(graceui_mock_call(graceui_mock_call_count()) == nullptr);
}

TEST_CASE("a space: registered once, published to after, every frame recorded as given") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  CHECK(publish(me, R"({"v":1,"t":"state"})") == GRACEUI_NOT_YET);
  CHECK(register_space(me) == GRACEUI_OK);
  CHECK(register_space(me) == GRACEUI_DUPLICATE_ID);
  CHECK(publish(me, R"({"v":1,"t":"state","space":"a-mod","topic":"t","rev":1,"body":{}})") == GRACEUI_OK);
  CHECK(publish(me, "") == GRACEUI_REFUSED);
  CHECK(graceui_mock_last(GRACEUI_MOCK_PUBLISH)->status == GRACEUI_REFUSED);
  CHECK(graceui_mock_count(GRACEUI_MOCK_PUBLISH) == 3);
  CHECK(std::string(graceui_mock_call(graceui_mock_call_count() - 2)->text).find("\"rev\":1") != std::string::npos);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_REGISTER_PAGES)->text).empty());  // the refused second one's
  for (int i = 1; i < 16; ++i) {
    const std::string id = "space-" + std::to_string(i);
    REQUIRE(register_space(connect(id.c_str()).answer.consumer) == GRACEUI_OK);
  }
  CHECK(register_space(connect("seventeenth").answer.consumer) == GRACEUI_FULL);
}

TEST_CASE("poll: frames into the mod's buffer, a full one kept, events its size cannot hold passed by") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  CHECK(poll(me).event.kind == GRACEUI_EVENT_NONE);
  const std::string sub = R"({"v":1,"t":"sub","space":"a-mod","id":1,"topic":"t"})";
  CHECK(graceui_mock_push_frame(me, GRACEUI_EVENT_WATCHED, sub.c_str()) == 1);
  const Polled tight = poll(me, sizeof(GraceUiEvent), 8);
  CHECK(tight.status == GRACEUI_FULL);
  CHECK(tight.event.frame_length == sub.size());
  const Polled watched = poll(me);
  CHECK(watched.status == GRACEUI_OK);
  CHECK(watched.event.kind == GRACEUI_EVENT_WATCHED);
  CHECK(watched.frame == sub);

  // A mod of GraceUI 0.1's event size: a frame waits for a poll of 0.2's size; a hotkey's press comes.
  std::uint32_t hotkey = 0;
  REQUIRE(register_hotkey(me, hotkey_desc(0x7A, "open"), &hotkey) == GRACEUI_OK);
  CHECK(graceui_mock_push_frame(me, GRACEUI_EVENT_COMMAND, R"({"v":1,"t":"cmd"})") == 2);
  CHECK(graceui_mock_push_hotkey(me, hotkey, 2) == 1);
  const Polled small = poll(me, 16);
  CHECK(small.event.kind == GRACEUI_EVENT_HOTKEY);
  CHECK(small.event.count == 2);
  CHECK(poll(me, 16).event.kind == GRACEUI_EVENT_NONE);
  const Polled command = poll(me);
  CHECK(command.event.kind == GRACEUI_EVENT_COMMAND);
  CHECK(command.event.reply == 2);

  GraceUiEvent tiny{};
  tiny.size = 4;  // too small to name its kind
  tiny.kind = GRACEUI_EVENT_DISPLAY;
  CHECK(graceui_mock_push_event(me, &tiny) == 0);
  CHECK(graceui_mock_push_frame(99, GRACEUI_EVENT_WATCHED, sub.c_str()) == 0);
}

TEST_CASE("poll's order is GraceUI's: a holder's, the display, hotkeys, then frames") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  std::uint32_t hotkey = 0;
  REQUIRE(register_hotkey(me, hotkey_desc(0x7A, "open"), &hotkey) == GRACEUI_OK);
  graceui_mock_push_frame(me, GRACEUI_EVENT_WATCHED, R"({"v":1,"t":"sub"})");
  graceui_mock_push_hotkey(me, hotkey, 1);
  GraceUiEvent display{};
  display.size = sizeof display;
  display.kind = GRACEUI_EVENT_DISPLAY;
  display.display_width = 2560;
  graceui_mock_push_event(me, &display);
  GraceUiEvent key{};
  key.size = sizeof key;
  key.kind = GRACEUI_EVENT_KEY;
  key.key = 0x41;
  graceui_mock_push_event(me, &key);
  CHECK(poll(me, static_cast<std::uint32_t>(offsetof(GraceUiEvent, display_width))).event.kind == GRACEUI_EVENT_KEY);
  // An event of ABI 1.1's size cannot hold the display: the hotkey comes before it.
  CHECK(poll(me, static_cast<std::uint32_t>(offsetof(GraceUiEvent, display_width))).event.kind == GRACEUI_EVENT_HOTKEY);
  const Polled shown = poll(me);
  CHECK(shown.event.kind == GRACEUI_EVENT_DISPLAY);
  CHECK(shown.event.display_width == 2560);
  CHECK(poll(me).event.kind == GRACEUI_EVENT_WATCHED);
  CHECK(graceui_mock_last(GRACEUI_MOCK_POLL)->id == GRACEUI_EVENT_WATCHED);
}

TEST_CASE("answer: a command's reply taken once, by the mod it was for") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer other = connect("other").answer.consumer;
  REQUIRE(register_space(me) == GRACEUI_OK);
  const std::uint64_t reply = graceui_mock_push_frame(me, GRACEUI_EVENT_COMMAND, R"({"v":1,"t":"cmd","id":7})");
  const std::string ok = R"({"v":1,"t":"result","space":"a-mod","id":7,"ok":true})";
  CHECK(graceui_mock_answer_to(reply) == nullptr);
  CHECK(graceui_mock_api()->answer(other, reply, ok.data(), ok.size()) == GRACEUI_REFUSED);
  CHECK(graceui_mock_api()->answer(me, reply, ok.data(), ok.size()) == GRACEUI_OK);
  CHECK(graceui_mock_api()->answer(me, reply, ok.data(), ok.size()) == GRACEUI_REFUSED);
  CHECK(graceui_mock_api()->answer(me, 999, ok.data(), ok.size()) == GRACEUI_REFUSED);
  REQUIRE(graceui_mock_answer_to(reply) != nullptr);
  CHECK(std::string(graceui_mock_answer_to(reply)) == ok);
}

TEST_CASE("hotkeys: defaults taken and refused as GraceUI's, described, paused and freed by a disconnect") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer old = connect("old-mod", 0, 2).answer.consumer;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  CHECK(register_hotkey(me, hotkey_desc(0x77, "trim"), &first) == GRACEUI_OK);
  CHECK(register_hotkey(old, hotkey_desc(0x77, "trim"), &second) == GRACEUI_UNBOUND);  // F8 is taken
  CHECK(second != 0);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_REGISTER_HOTKEY)->text) == "trim");
  CHECK(register_hotkey(me, hotkey_desc(0x74, "menu-like")) == GRACEUI_UNBOUND);  // the menu key, F5
  CHECK(register_hotkey(me, hotkey_desc(0x00, "none")) == GRACEUI_UNBOUND);     // no default at all
  GraceUiHotkeyBinding binding{};
  binding.size = sizeof binding;
  CHECK(graceui_mock_api()->describe_hotkey(me, first, &binding) == GRACEUI_OK);
  CHECK(binding.bound == GRACEUI_HOTKEY_KEY_BOUND);
  CHECK(binding.key == 0x77);
  CHECK(graceui_mock_api()->describe_hotkey(me, second, &binding) == GRACEUI_REFUSED);  // not its own
  CHECK(graceui_mock_api()->set_hotkey_enabled(me, first, 0) == GRACEUI_OK);
  CHECK(graceui_mock_hotkey_enabled(first) == 0);
  CHECK(graceui_mock_api()->set_hotkey_enabled(me, first, 1) == GRACEUI_OK);
  CHECK(graceui_mock_hotkey_enabled(first) == 1);

  const std::uint32_t chord = GRACEUI_PAD_CHORD(0x0001, 0x0020);  // D-pad Up, then View
  CHECK(register_hotkey(old, hotkey_desc(0, "chord", chord)) == GRACEUI_NOT_YET);  // built before ABI 1.3
  CHECK(register_hotkey(me, hotkey_desc(0x78, "both", chord)) == GRACEUI_OK);
  // A key free and a chord taken: unbound, as GraceUI's, and the key alone bound.
  std::uint32_t half = 0;
  CHECK(register_hotkey(me, hotkey_desc(0x79, "half", chord), &half) == GRACEUI_UNBOUND);
  CHECK(graceui_mock_api()->describe_hotkey(me, half, &binding) == GRACEUI_OK);
  CHECK(binding.bound == GRACEUI_HOTKEY_KEY_BOUND);

  for (const GraceUiHotkeyDesc& bad :
       {hotkey_desc(0x77, "trim"), hotkey_desc(0x1B, "esc"), hotkey_desc(0x10, "shift"), hotkey_desc(0x01, "click"),
        hotkey_desc(0, "same", GRACEUI_PAD_CHORD(0x0001, 0x0001)),
        hotkey_desc(0, "trigger", GRACEUI_PAD_CHORD(0x0400, 0x0001)), hotkey_desc(0x41, "Bad Name")}) {
    CHECK_MESSAGE(register_hotkey(me, bad) == GRACEUI_REFUSED, bad.action);
  }
  GraceUiHotkeyDesc alt_f4 = hotkey_desc(0x73, "quit");
  alt_f4.modifiers = GRACEUI_MOD_ALT;
  CHECK(register_hotkey(me, alt_f4) == GRACEUI_REFUSED);
  GraceUiHotkeyDesc odd = hotkey_desc(0x42, "odd");
  odd.modifiers = 8;
  CHECK(register_hotkey(me, odd) == GRACEUI_REFUSED);

  CHECK(graceui_mock_api()->disconnect(me) == GRACEUI_OK);
  const GraceUiConsumer again = connect("a-mod").answer.consumer;
  CHECK(register_hotkey(again, hotkey_desc(0x77, "trim")) == GRACEUI_OK);  // its old default freed
}

TEST_CASE("panels, textures and faces: the registration window, list bounds, warm-up") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer other = connect("other").answer.consumer;
  const std::uint64_t panel = make_panel(me);
  GraceUiPanelDesc no_layer{};
  no_layer.size = sizeof no_layer;
  no_layer.width = 10;
  no_layer.height = 10;
  std::uint64_t id = 0;
  CHECK(graceui_mock_api()->create_panel(me, &no_layer, &id) == GRACEUI_REFUSED);
  const std::uint32_t pixels[4] = {0, 0, 0, 0};
  GraceUiTextureDesc texture{};
  texture.size = sizeof texture;
  texture.width = 2;
  texture.height = 2;
  texture.row_bytes = 8;
  texture.pixels = pixels;
  std::uint64_t texture_id = 0;
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_REFUSED);  // no format
  texture.format = GRACEUI_TEXTURE_RGBA8;
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_OK);
  CHECK(texture_id == 1);
  texture.width = 2048;
  texture.height = 2048;  // 16 MiB: past a mod's 8
  texture.row_bytes = 8192;
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_FULL);
  GraceUiDrawList list{};
  list.size = sizeof list;
  list.command_size = 0;
  CHECK(graceui_mock_api()->submit(me, panel, &list) == GRACEUI_REFUSED);  // a stride below 1.0's command
  texture.width = 2;
  texture.height = 2;
  texture.row_bytes = 8;
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_TOO_LATE);  // closed all the same
  list.command_size = sizeof(GraceUiDrawCommand);
  CHECK(graceui_mock_api()->submit(me, panel, &list) == GRACEUI_OK);
  list.command_count = GRACEUI_DRAW_COMMANDS_MAX + 1;
  CHECK(graceui_mock_api()->submit(me, panel, &list) == GRACEUI_REFUSED);
  CHECK(graceui_mock_api()->submit(other, panel, &list) == GRACEUI_REFUSED);  // another mod's panel
  CHECK(graceui_mock_api()->show_panel(me, panel) == GRACEUI_OK);
  CHECK(graceui_mock_panel_shown(panel) == 1);

  const unsigned char font[4] = {0, 1, 0, 0};
  GraceUiFaceDesc face{};
  face.size = sizeof face;
  face.pixel_height = 20;
  face.data = font;
  face.data_length = sizeof font;
  std::uint32_t face_id = 0;
  CHECK(graceui_mock_api()->register_face(other, &face, &face_id) == GRACEUI_OK);
  CHECK(face_id == 16);  // after GraceUI's own faces
  GraceUiGlyphTable table{};
  table.size = sizeof table;
  CHECK(graceui_mock_api()->glyph_table(other, face_id, &table) == GRACEUI_NOT_READY);
  GraceUiGlyphTable baked{};
  baked.generation = 3;
  graceui_mock_set_glyph_table(&baked);
  CHECK(graceui_mock_api()->glyph_table(other, face_id, &table) == GRACEUI_OK);
  CHECK(table.generation == 3);
  CHECK(table.face_id == face_id);
  CHECK(graceui_mock_api()->register_face(other, &face, &face_id) == GRACEUI_TOO_LATE);
  face.pixel_height = 200;
  CHECK(graceui_mock_api()->register_face(other, &face, &face_id) == GRACEUI_REFUSED);
}

TEST_CASE("input: GraceUI's checks, one holder, and every way it ends") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer other = connect("other").answer.consumer;
  const std::uint64_t panel = make_panel(me);
  GraceUiStatus status = GRACEUI_OK;
  CHECK(request_input(me, panel).answer == GRACEUI_INPUT_NOT_NOW);  // not shown yet
  CHECK(request_input(me, panel).why == GRACEUI_NOT_NOW_NOT_READY);
  REQUIRE(graceui_mock_api()->show_panel(me, panel) == GRACEUI_OK);
  request_input(me, panel, 0, GRACEUI_HOLDER_MODAL, &status);
  CHECK(status == GRACEUI_REFUSED);  // a modal holder needs a panel on the modal layer
  const std::uint64_t plain = make_panel(me, GRACEUI_LAYER_PANEL, 0);
  REQUIRE(graceui_mock_api()->show_panel(me, plain) == GRACEUI_OK);
  request_input(me, plain, 0, GRACEUI_HOLDER_PANEL, &status);
  CHECK(status == GRACEUI_REFUSED);  // a panel that may not take input
  request_input(me, panel, 8, GRACEUI_HOLDER_PANEL, &status);
  CHECK(status == GRACEUI_REFUSED);  // an unknown flag

  CHECK(request_input(me, panel, GRACEUI_INPUT_EVENTS).answer == GRACEUI_INPUT_GRANTED);
  CHECK(graceui_mock_input_holder() == me);
  const std::uint64_t theirs = make_panel(other);
  REQUIRE(graceui_mock_api()->show_panel(other, theirs) == GRACEUI_OK);
  const GraceUiInputAnswer refused = request_input(other, theirs);
  CHECK(refused.answer == GRACEUI_INPUT_REFUSED);
  CHECK(std::string(refused.holder) == "a-mod");

  // Hiding the holding panel ends the holder; its mod hears why.
  CHECK(graceui_mock_api()->hide_panel(me, panel) == GRACEUI_OK);
  CHECK(graceui_mock_input_holder() == 0);
  const Polled ended = poll(me);
  CHECK(ended.event.kind == GRACEUI_EVENT_INPUT_ENDED);
  CHECK(ended.event.why == GRACEUI_CLOSED_RELEASED);

  // A provider's danger ends it, and makes the next request not now.
  REQUIRE(graceui_mock_api()->show_panel(me, panel) == GRACEUI_OK);
  REQUIRE(request_input(me, panel, GRACEUI_INPUT_EVENTS).answer == GRACEUI_INPUT_GRANTED);
  CHECK(world(other, GRACEUI_WORLD_DANGER) == GRACEUI_OK);
  CHECK(graceui_mock_input_holder() == 0);
  CHECK(poll(me).event.why == GRACEUI_CLOSED_DANGER);
  const GraceUiInputAnswer danger = request_input(me, panel);
  CHECK(danger.answer == GRACEUI_INPUT_NOT_NOW);
  CHECK(danger.why == GRACEUI_NOT_NOW_DANGER);
  CHECK(world(other, 0) == GRACEUI_OK);
  CHECK(world(other, 64) == GRACEUI_REFUSED);

  // release_input, and the test's own answer for the menu holding input.
  REQUIRE(request_input(me, panel).answer == GRACEUI_INPUT_GRANTED);
  CHECK(graceui_mock_api()->release_input(me) == GRACEUI_OK);
  CHECK(poll(me).event.kind == GRACEUI_EVENT_NONE);  // it asked for no events this time
  graceui_mock_set_input_answer(GRACEUI_INPUT_REFUSED, 0, "graceui");
  const GraceUiInputAnswer menu = request_input(other, theirs);
  CHECK(menu.answer == GRACEUI_INPUT_REFUSED);
  CHECK(menu.holder_kind == GRACEUI_HOLDER_MENU);

  // The pad keyboard: the holder's alone; a null keyboard closes, always ok.
  CHECK(graceui_mock_api()->pad_keyboard(me, nullptr) == GRACEUI_OK);
  GraceUiPadKeyboard keyboard{};
  keyboard.size = sizeof keyboard;
  keyboard.panel = panel;
  keyboard.width = 10;
  keyboard.height = 10;
  CHECK(graceui_mock_api()->pad_keyboard(me, &keyboard) == GRACEUI_REFUSED);  // it does not hold input
}

TEST_CASE("fail_next: the slot's next call alone; wake_event has no event") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  graceui_mock_fail_next(GRACEUI_MOCK_NOTICE, GRACEUI_FAULT);
  CHECK(notice(me, "One.") == GRACEUI_FAULT);
  CHECK(notice(me, "Two.") == GRACEUI_OK);
  CHECK(graceui_mock_count(GRACEUI_MOCK_NOTICE) == 2);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_NOTICE)->text) == "Two.");
  CHECK(notice(me, "", 0) == GRACEUI_REFUSED);  // an empty text withdraws a keyed notice, and only that
  CHECK(notice(me, "", 3) == GRACEUI_OK);
  CHECK(graceui_mock_api()->wake_event(me, nullptr) == GRACEUI_REFUSED);
  void* wake = &wake;
  CHECK(graceui_mock_api()->wake_event(me, &wake) == GRACEUI_NOT_YET);
  CHECK(wake == nullptr);
}

namespace {
struct Present {
  std::uint32_t calls = 0;
  std::uint32_t vertices = 0;         // what the callback says it drew
  GraceUiStatus inside = GRACEUI_OK;  // what a slot called from inside answered
  GraceUiConsumer me = 0;
  std::vector<GraceUiDrawVert> room = std::vector<GraceUiDrawVert>(128);
};
std::uint32_t draw_each_frame(void* user, const GraceUiPresentFrame* frame, GraceUiDrawList* list) {
  Present& p = *static_cast<Present*>(user);
  ++p.calls;
  p.inside = graceui_mock_api()->log(p.me, GRACEUI_LOG_INFO, "inside", 6);
  list->vertex_count = p.vertices;
  list->vertices = p.room.data();
  return frame->panel != 0 && list->command_size == sizeof(GraceUiDrawCommand) ? GRACEUI_PRESENT_DRAW
                                                                                : GRACEUI_PRESENT_KEEP;
}
}  // namespace

TEST_CASE("set_on_present: the callback run by graceui_mock_present, within its room, no slot from inside it") {
  graceui_mock_reset();
  Present p;
  p.me = connect("a-mod").answer.consumer;
  const std::uint64_t panel = make_panel(p.me);
  GraceUiDrawList list{};
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_KEEP);  // none yet
  GraceUiPresentHook hook{};
  hook.size = sizeof hook;
  hook.vertex_capacity = 64;
  hook.index_capacity = 96;
  hook.command_capacity = 1;
  hook.callback = &draw_each_frame;
  hook.user = &p;
  CHECK(graceui_mock_api()->set_on_present(p.me, panel, &hook) == GRACEUI_OK);
  p.vertices = 4;
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_DRAW);
  CHECK(p.calls == 1);
  CHECK(p.inside == GRACEUI_REFUSED);
  p.vertices = 65;  // past the room it asked for: the submitted list is drawn instead
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_KEEP);
  hook.vertex_capacity = 0;
  CHECK(graceui_mock_api()->set_on_present(p.me, panel, &hook) == GRACEUI_REFUSED);
  hook.callback = nullptr;
  CHECK(graceui_mock_api()->set_on_present(p.me, panel, &hook) == GRACEUI_OK);  // cleared: capacities not read
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_KEEP);
  hook.vertex_capacity = 64;
  hook.callback = &draw_each_frame;
  for (int i = 0; i < 4; ++i) {
    REQUIRE(graceui_mock_api()->set_on_present(p.me, make_panel(p.me), &hook) == GRACEUI_OK);
  }
  CHECK(graceui_mock_api()->set_on_present(p.me, make_panel(p.me), &hook) == GRACEUI_FULL);  // four a mod
}

TEST_CASE("a mod's logic in C, tested on the mock: connected, registered, a watch and two commands answered") {
  graceui_mock_reset();
  REQUIRE(c_mod_start(graceui_mock_api()) == GRACEUI_OK);
  const GraceUiConsumer mod = graceui_mock_last(GRACEUI_MOCK_CONNECT)->consumer;
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_REGISTER_PAGES)->text).find("\"c-mod\"") != std::string::npos);
  graceui_mock_push_frame(mod, GRACEUI_EVENT_WATCHED, R"({"v":1,"t":"sub","space":"c-mod","id":1,"topic":"c.count"})");
  const std::uint64_t add = graceui_mock_push_frame(mod, GRACEUI_EVENT_COMMAND, R"({"v":1,"t":"cmd","id":2,"name":"c.add"})");
  const std::uint64_t odd = graceui_mock_push_frame(mod, GRACEUI_EVENT_COMMAND, R"({"v":1,"t":"cmd","id":3,"name":"c.odd"})");
  CHECK(c_mod_poll() == 3);
  REQUIRE(graceui_mock_answer_to(add) != nullptr);
  CHECK(std::string(graceui_mock_answer_to(add)).find("\"ok\":true") != std::string::npos);
  REQUIRE(graceui_mock_answer_to(odd) != nullptr);
  CHECK(std::string(graceui_mock_answer_to(odd)).find("C mod has no such command.") != std::string::npos);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_PUBLISH)->text).find("The count is 1.") != std::string::npos);
  CHECK(graceui_mock_count(GRACEUI_MOCK_PUBLISH) == 3);
  graceui_mock_reset();
  graceui_mock_set_caps("notices");
  CHECK(c_mod_start(graceui_mock_api()) == GRACEUI_NOT_YET);  // a GraceUI without pages: the mod stays headless
}
