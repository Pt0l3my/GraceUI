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
`graceui.dll`; the mod itself never links it. It answers the version of the GraceUI that brought its header's ABI
minor, or `GRACEUI_MOCK_VERSION` if the build defines it (GraceUI's own build defines it as its version), or what
`graceui_mock_set_version` sets.

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

**What it answers.** As GraceUI does, by its rules in `graceui.h` and its own code:

| Calls | The mock |
|---|---|
| `connect` | Checks the request (its size, id, name, caps and floor): `ok` with handles 1, 2 and on, `duplicate_id`, `too_old` naming both versions, `refused` with its reason; the display as 1920 by 1080 at a Menu size of 1 |
| Every slot that names a mod | `not_connected` for a handle that names none; `refused` from inside an `on_present` callback; after `graceui_mock_set_inactive`, `inactive` for every slot but `disconnect`, and but `notice` with `GRACEUI_INACTIVE_ANTI_CHEAT`, as GraceUI's notice-only mode |
| `log` | `not_yet`, as GraceUI's, which declares it and does not provide it yet; the line is kept for the test all the same |
| `register_pages`, `publish`, `answer` | A space registered once, 16 at most; `publish` `not_yet` before it; `answer` takes a reply that `graceui_mock_push_frame` gave, once. Frames and page data are recorded as given: the mock parses no JSON, so the page tool checks page data |
| `poll` | GraceUI's order: a holder's events, then the display, then hotkeys and their changes, then a space's frames. A frame goes into the mod's buffer, or `full` with the size needed and the event kept; an event the mod's `GraceUiEvent` is too small for waits for a poll that can hold it |
| Panels, textures, faces | GraceUI's checks of each description, its limits and budgets; ids from 1 (faces from 16, after GraceUI's own); the registration window closed by a mod's first `submit` to its panel, taken or refused, or `set_on_present`; a list's shape checked against GraceUI's bounds, not its contents; `glyph_table` `not_ready` until `graceui_mock_set_glyph_table`, which stands for warm-up, after which `register_face` is `too_late` |
| Input | GraceUI's checks (a panel of the mod's that may take input, on the modal layer for a modal holder, known wants and flags); `not_now` for a panel not shown, or while a provider claims danger, loading or a game menu; one holder, every other mod refused naming it; the holder ended by `release_input`, its panel hidden, a provider's danger or loading, or its mod's disconnect, with `GRACEUI_EVENT_INPUT_ENDED` and why to a mod that asked for its holder's events. `graceui_mock_set_input_answer` stands for the menu holding input, or a danger of the test's own |
| Hotkeys | GraceUI's refusals (an action the mod has already, Escape, a modifier or a mouse button, Alt+F4, unknown modifiers, a chord not of two different buttons); `unbound` when it names no default or a default it names is taken, by another hotkey or the menu key (F5); a pad chord from a mod built before ABI 1.3 `not_yet`; `describe_hotkey` gives the defaults it bound; a disconnect frees them |
| `wake_event` | `not_yet`, and `has_cap` does not name `wake`: a test polls |
| `set_on_present` | The callback kept, four a mod and eight in all; `graceui_mock_present` runs one frame of it, its list drawn only if it fits the room the hook asked for |

`graceui_mock_set_minor` makes the mock the GraceUI that brought an older ABI minor: its table cut to that minor's size,
so that `GRACEUI_API1_COVERS` is false for every later slot, that GraceUI's version and caps, and nothing read that the
minor did not bring. `graceui_mock_set_caps` names the caps `has_cap` answers; `graceui_mock_fail_next` makes one slot's
next call answer a status of the test's choosing. `graceui_mock_call`, `graceui_mock_count` and `graceui_mock_last` read
what the mod called, each with what it answered and a copy of the text it carried, which hold until the next
`graceui_mock_reset`; a text longer than its slot takes is not kept. It is one mock in the process, for one test at a
time, from one thread: `graceui_mock_reset` starts the next.

**What it does not do.** It draws nothing and runs no present thread, keeps no clock (a notice never times out, and
the notices never fill), and has no Hotkeys page, browser or server. Those are GraceUI's, tested by its harness on Windows; a mod's own panels and pages
are seen in game or with grace-dev.
