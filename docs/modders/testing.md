# Testing a mod

GraceUI gives a mod's author four ways to test it before the game: the page tool for its pages, the space check and
grace-dev for its pages as the two views draw them, and the mock of the ABI for its own code. GraceUI's harness tests
GraceUI itself, and the example mods with it, on Windows.

| Tool | What it tests | Where |
|---|---|---|
| The page tool | A space's `space.toml` against the page lint, compiled into its page data ([Compiling a mod's space](pages.md#compiling-a-mods-space)) | `tools/pages/build.py --lint`, `--build` |
| The space check | A space folder as the browser view draws it, dumped for a golden of the mod's own | `npm run -s check:space` in `web/` |
| grace-dev | A space folder served to a browser on loopback, a scenario of states and answers played, a page's in-game layout dumped | `tools/grace-dev/` |
| The mock of the ABI | The mod's own code: its connect, its frames, its answers, its hotkeys and notices (D285) | `src/graceui-mock/` |

## The mock of the ABI

`graceui_mock.h` and `graceui_mock.c` (`src/graceui-mock/`, MIT, pure C99 and its standard library) are a
`GraceUiApi1` table that answers as GraceUI does for what a test needs, records every call, and gives `poll` the
events the test queues. A mod's tests build `graceui_mock.c` with them, on any system, without the game, a window or
`graceui.dll`; the mod itself never links it. Define `GRACEUI_MOCK_VERSION` as the GraceUI the mod is built against
(GraceUI's own build does), or set it with `graceui_mock_set_version`.

**The pattern.** The mod keeps `graceui_sys_find` in the thread it starts, and hands the table it found to the rest of
its code. A test hands that code `graceui_mock_api()` instead, queues what a view or the player would send, and reads
back what the mod did. A mod's logic in C, as GraceUI's own tests of the mock drive it:

<!-- snippet: src/graceui-mock/tests/mod_in_c.c -->

    int c_mod_poll(void) {
      int handled = 0;
      for (;;) {
        GraceUiEvent event;
        char text[256];
        int length;
        memset(&event, 0, sizeof event);
        event.size = sizeof event;
        event.frame = g_frame;
        event.frame_capacity = GRACEUI_FRAME_FROM_PAGE_MAX;
        if (g_api->poll(g_me, &event) != GRACEUI_OK || event.kind == GRACEUI_EVENT_NONE) {
          return handled;
        }
        ++handled;
        g_frame[event.frame_length] = '\0';
        if (event.kind == GRACEUI_EVENT_COMMAND) {
          const int add = strstr(g_frame, "\"name\":\"c.add\"") != NULL;
          if (add) {
            ++g_count;
          }
          length = snprintf(text, sizeof text, add ? "{\"v\":1,\"t\":\"result\",\"space\":\"c-mod\",\"id\":0,\"ok\":true}"
                            : "{\"v\":1,\"t\":\"result\",\"space\":\"c-mod\",\"id\":0,\"error\":\"unknown_command\","
                              "\"detail\":\"C mod has no such command.\"}");
          g_api->answer(g_me, event.reply, text, (uint64_t)length);
        }
        if (event.kind == GRACEUI_EVENT_WATCHED || (event.kind == GRACEUI_EVENT_COMMAND && g_count > 0)) {
          length = snprintf(text, sizeof text,
                            "{\"v\":1,\"t\":\"state\",\"space\":\"c-mod\",\"topic\":\"c.count\",\"rev\":%d,"
                            "\"body\":{\"sentence\":\"The count is %d.\"}}", handled, g_count);
          g_api->publish(g_me, text, (uint64_t)length);
        }
      }
    }

and its test:

<!-- snippet: src/graceui-mock/tests/mock_tests.cpp -->

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

**What it answers.**

| Calls | The mock |
|---|---|
| `connect` | Checks the request as GraceUI does (its size, id, name, caps and floor): `ok` with handles 1, 2 and on, `duplicate_id`, `too_old` naming both versions, `refused` with its reason; the display as 1920 by 1080 at a Menu size of 1 |
| Every slot that names a mod | `not_connected` for a handle that names none; `inactive` after `graceui_mock_set_inactive`, but for `notice`, `log`, `poll` and `disconnect`, as in the anti-cheat's notice-only mode |
| `register_pages`, `publish`, `answer` | A space registered once; `publish` `not_yet` before it; `answer` takes a reply that `graceui_mock_push_frame` gave, once. Frames and page data are recorded as given: the mock parses no JSON, so the page tool checks page data |
| `poll` | The queued events in order, a frame into the mod's buffer, `full` with the size needed and the event kept; an event the mod's `GraceUiEvent` is too small for waits for a poll that can hold it, as with GraceUI |
| Panels, textures, faces | Ids from 1, GraceUI's limits, the registration window closed by the first `submit` or `set_on_present`; a list's counts checked against GraceUI's bounds, not its contents; `glyph_table` `not_ready` until `graceui_mock_set_glyph_table` |
| Input, hotkeys | One holder, every other mod's request refused naming it; `graceui_mock_set_input_answer` for the menu's or a danger's answer. A default another hotkey has already is `unbound`; `describe_hotkey` gives the defaults it registered; a pad chord from a mod built before ABI 1.3 is `not_yet` |
| `wake_event` | `not_yet`: a test polls |
| `set_on_present` | The callback kept; `graceui_mock_present` runs one frame of it |

`graceui_mock_set_minor` cuts the table to an older ABI minor's size, so that `GRACEUI_API1_COVERS` is false for every
later slot; `graceui_mock_set_caps` names the caps `has_cap` answers; `graceui_mock_fail_next` makes one slot's next call
answer a status of the test's choosing. `graceui_mock_call`, `graceui_mock_count` and `graceui_mock_last` read what the
mod called, each with what it answered and a copy of the text it carried. It is one mock in the process, for one test
at a time, from one thread: `graceui_mock_reset` starts the next.

**What it does not do.** It draws nothing and runs no present thread, keeps no notices on a screen, and has no
Hotkeys page, browser or server. Those are GraceUI's, tested by its harness on Windows; a mod's own panels and pages
are seen in game or with grace-dev.
