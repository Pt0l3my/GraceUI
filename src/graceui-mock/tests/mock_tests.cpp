// SPDX-License-Identifier: MIT
// The mock of ABI 1 (D285): its table, its answers as GraceUI's, what it records, the events it gives poll, and a mod's
// logic in C tested on it end to end.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

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

std::uint64_t make_panel(GraceUiConsumer me) {
  GraceUiPanelDesc desc{};
  desc.size = sizeof desc;
  desc.width = 100;
  desc.height = 50;
  std::uint64_t id = 0;
  REQUIRE(graceui_mock_api()->create_panel(me, &desc, &id) == GRACEUI_OK);
  return id;
}

}  // namespace

TEST_CASE("a fresh mock: the table of this header's minor, this GraceUI's version and every cap") {
  graceui_mock_reset();
  const GraceUiApi1* api = graceui_mock_api();
  CHECK(api->size == sizeof(GraceUiApi1));
  CHECK(api->abi_minor == GRACEUI_ABI1_MINOR);
  CHECK(api->graceui_version == GRACEUI_VERSION_PACK(GRACE_BUILD_MAJOR, GRACE_BUILD_MINOR, GRACE_BUILD_PATCH));
  CHECK(GRACEUI_API1_COVERS(api, set_hotkey_enabled));
  for (const char* cap : {GRACEUI_CAP_NOTICES, GRACEUI_CAP_PAGES, GRACEUI_CAP_WAKE, GRACEUI_CAP_HOTKEY_PAUSE}) {
    CHECK(api->has_cap(cap, std::strlen(cap)) == 1);
  }
  CHECK(api->has_cap("map", 3) == 0);
  CHECK(api->has_cap("page", 4) == 0);  // a cap's name whole, never its start
  graceui_mock_set_caps("notices panels");
  CHECK(api->has_cap("panels", 6) == 1);
  CHECK(api->has_cap("pages", 5) == 0);
  CHECK(graceui_mock_count(GRACEUI_MOCK_HAS_CAP) == 8);
}

TEST_CASE("an older minor's table: its size ends at that minor's last slot") {
  graceui_mock_reset();
  graceui_mock_set_minor(4);
  const GraceUiApi1* api = graceui_mock_api();
  CHECK(api->abi_minor == 4);
  CHECK(GRACEUI_API1_COVERS(api, describe_hotkey));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, wake_event));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, set_hotkey_enabled));
  graceui_mock_set_minor(0);
  CHECK(GRACEUI_API1_COVERS(api, log));
  CHECK_FALSE(GRACEUI_API1_COVERS(api, set_caret));
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

TEST_CASE("the notice-only mode: inactive, with a handle; notices and the log work, nothing else") {
  graceui_mock_reset();
  graceui_mock_set_inactive(GRACEUI_INACTIVE_ANTI_CHEAT);
  const Connected mod = connect("quiet-mod");
  CHECK(mod.status == GRACEUI_INACTIVE);
  CHECK(mod.answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT);
  REQUIRE(mod.answer.consumer != 0);
  CHECK(notice(mod.answer.consumer, "Only notices now.") == GRACEUI_OK);
  CHECK(register_space(mod.answer.consumer) == GRACEUI_INACTIVE);
  CHECK(graceui_mock_api()->disconnect(mod.answer.consumer) == GRACEUI_OK);
  CHECK(notice(mod.answer.consumer, "Gone.") == GRACEUI_NOT_CONNECTED);
}

TEST_CASE("a space: registered once, published to after, every frame recorded as given") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  CHECK(publish(me, R"({"v":1,"t":"state"})") == GRACEUI_NOT_YET);
  CHECK(register_space(me) == GRACEUI_OK);
  CHECK(register_space(me) == GRACEUI_DUPLICATE_ID);
  CHECK(publish(me, R"({"v":1,"t":"state","space":"a-mod","topic":"t","rev":1,"body":{}})") == GRACEUI_OK);
  CHECK(publish(me, "") == GRACEUI_REFUSED);
  const GraceUiMockCall* published = graceui_mock_last(GRACEUI_MOCK_PUBLISH);
  REQUIRE(published != nullptr);
  CHECK(published->status == GRACEUI_REFUSED);
  CHECK(graceui_mock_count(GRACEUI_MOCK_PUBLISH) == 3);
  CHECK(std::string(graceui_mock_call(graceui_mock_call_count() - 2)->text).find("\"rev\":1") != std::string::npos);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_REGISTER_PAGES)->text).empty());  // the refused second one's
}

TEST_CASE("poll: frames into the mod's buffer, a full one kept, and events its size cannot hold passed by") {
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

  // A mod of GraceUI 0.1's event size: a frame waits for a poll of 0.2's size, a hotkey's press comes first.
  GraceUiHotkeyDesc key{};
  key.size = sizeof key;
  key.key = 0x7A;
  key.action = "open";
  key.action_length = 4;
  key.name = "Open";
  key.name_length = 4;
  std::uint32_t hotkey = 0;
  REQUIRE(graceui_mock_api()->register_hotkey(me, &key, &hotkey) == GRACEUI_OK);
  CHECK(graceui_mock_push_frame(me, GRACEUI_EVENT_COMMAND, R"({"v":1,"t":"cmd"})") == 2);
  CHECK(graceui_mock_push_hotkey(me, hotkey, 2) == 1);
  const Polled small = poll(me, 16);
  CHECK(small.event.kind == GRACEUI_EVENT_HOTKEY);
  CHECK(small.event.count == 2);
  CHECK(poll(me, 16).event.kind == GRACEUI_EVENT_NONE);
  const Polled command = poll(me);
  CHECK(command.event.kind == GRACEUI_EVENT_COMMAND);
  CHECK(command.event.reply == 2);

  // A holder's key and the display, each to an event that holds its fields.
  GraceUiEvent display{};
  display.size = sizeof display;
  display.kind = GRACEUI_EVENT_DISPLAY;
  display.display_width = 2560;
  CHECK(graceui_mock_push_event(me, &display) == 1);
  CHECK(poll(me, static_cast<std::uint32_t>(offsetof(GraceUiEvent, display_width))).event.kind == GRACEUI_EVENT_NONE);
  const Polled shown = poll(me);
  CHECK(shown.event.kind == GRACEUI_EVENT_DISPLAY);
  CHECK(shown.event.display_width == 2560);
  CHECK(graceui_mock_push_frame(99, GRACEUI_EVENT_WATCHED, sub.c_str()) == 0);
  CHECK(graceui_mock_last(GRACEUI_MOCK_POLL)->id == GRACEUI_EVENT_DISPLAY);
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

TEST_CASE("hotkeys: a taken default unbound, the binding described, paused and resumed, a chord too new") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer old = connect("old-mod", 0, 2).answer.consumer;
  GraceUiHotkeyDesc key{};
  key.size = sizeof key;
  key.key = 0x77;  // F8
  key.action = "trim";
  key.action_length = 4;
  key.name = "Trim";
  key.name_length = 4;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  CHECK(graceui_mock_api()->register_hotkey(me, &key, &first) == GRACEUI_OK);
  CHECK(graceui_mock_api()->register_hotkey(old, &key, &second) == GRACEUI_UNBOUND);
  CHECK(second != 0);
  CHECK(std::string(graceui_mock_last(GRACEUI_MOCK_REGISTER_HOTKEY)->text) == "trim");
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
  key.pad_chord = GRACEUI_PAD_CHORD(0x0001, 0x0020);
  key.key = 0;
  key.action = "chord";
  key.action_length = 5;
  std::uint32_t chord = 0;
  CHECK(graceui_mock_api()->register_hotkey(old, &key, &chord) == GRACEUI_NOT_YET);  // built before ABI 1.3
  CHECK(graceui_mock_api()->register_hotkey(me, &key, &chord) == GRACEUI_OK);
  key.action = "Bad Name";
  key.action_length = 8;
  CHECK(graceui_mock_api()->register_hotkey(me, &key, &chord) == GRACEUI_REFUSED);
}

TEST_CASE("panels, textures and input: the registration window, list bounds, one holder") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const GraceUiConsumer other = connect("other").answer.consumer;
  const std::uint64_t panel = make_panel(me);
  const std::uint32_t pixels[4] = {0, 0, 0, 0};
  GraceUiTextureDesc texture{};
  texture.size = sizeof texture;
  texture.width = 2;
  texture.height = 2;
  texture.row_bytes = 8;
  texture.pixels = pixels;
  std::uint64_t texture_id = 0;
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_OK);
  CHECK(texture_id == 1);
  GraceUiDrawList list{};
  list.size = sizeof list;
  list.command_size = sizeof(GraceUiDrawCommand);
  CHECK(graceui_mock_api()->submit(me, panel, &list) == GRACEUI_OK);
  CHECK(graceui_mock_api()->register_texture(me, &texture, &texture_id) == GRACEUI_TOO_LATE);
  list.command_count = GRACEUI_DRAW_COMMANDS_MAX + 1;
  CHECK(graceui_mock_api()->submit(me, panel, &list) == GRACEUI_REFUSED);
  CHECK(graceui_mock_api()->submit(other, panel, &list) == GRACEUI_REFUSED);  // another mod's panel
  CHECK(graceui_mock_api()->show_panel(me, panel) == GRACEUI_OK);
  CHECK(graceui_mock_panel_shown(panel) == 1);

  GraceUiInputRequest request{};
  request.size = sizeof request;
  request.kind = GRACEUI_HOLDER_PANEL;
  request.panel = panel;
  GraceUiInputAnswer answer{};
  answer.size = sizeof answer;
  CHECK(graceui_mock_api()->request_input(me, &request, &answer) == GRACEUI_OK);
  CHECK(answer.answer == GRACEUI_INPUT_GRANTED);
  CHECK(graceui_mock_input_holder() == me);
  request.panel = make_panel(other);
  CHECK(graceui_mock_api()->request_input(other, &request, &answer) == GRACEUI_OK);
  CHECK(answer.answer == GRACEUI_INPUT_REFUSED);
  CHECK(std::string(answer.holder) == "a-mod");
  CHECK(graceui_mock_api()->release_input(me) == GRACEUI_OK);
  graceui_mock_set_input_answer(GRACEUI_INPUT_NOT_NOW, GRACEUI_NOT_NOW_DANGER, nullptr);
  CHECK(graceui_mock_api()->request_input(other, &request, &answer) == GRACEUI_OK);
  CHECK(answer.answer == GRACEUI_INPUT_NOT_NOW);
  CHECK(answer.why == GRACEUI_NOT_NOW_DANGER);
  CHECK(graceui_mock_input_holder() == 0);
}

TEST_CASE("fail_next: the slot's next call alone") {
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
std::uint32_t g_calls = 0;
std::uint32_t draw_each_frame(void* user, const GraceUiPresentFrame* frame, GraceUiDrawList* list) {
  ++*static_cast<std::uint32_t*>(user);
  return frame->panel != 0 && list->command_size == sizeof(GraceUiDrawCommand) && list->vertex_count == 0
             ? GRACEUI_PRESENT_DRAW
             : GRACEUI_PRESENT_KEEP;
}
}  // namespace

TEST_CASE("set_on_present: the callback kept and run by graceui_mock_present, closing the window") {
  graceui_mock_reset();
  const GraceUiConsumer me = connect("a-mod").answer.consumer;
  const std::uint64_t panel = make_panel(me);
  GraceUiDrawList list{};
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_KEEP);  // none yet
  GraceUiPresentHook hook{};
  hook.size = sizeof hook;
  hook.vertex_capacity = 64;
  hook.index_capacity = 96;
  hook.command_capacity = 1;
  hook.callback = &draw_each_frame;
  hook.user = &g_calls;
  CHECK(graceui_mock_api()->set_on_present(me, panel, &hook) == GRACEUI_OK);
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_DRAW);
  CHECK(g_calls == 1);
  hook.vertex_capacity = 0;
  CHECK(graceui_mock_api()->set_on_present(me, panel, &hook) == GRACEUI_REFUSED);
  hook.callback = nullptr;
  CHECK(graceui_mock_api()->set_on_present(me, panel, &hook) == GRACEUI_OK);  // cleared: capacities not read
  CHECK(graceui_mock_present(panel, nullptr, &list) == GRACEUI_PRESENT_KEEP);
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
