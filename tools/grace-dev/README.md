# grace-dev, GraceUI's fake host

`grace-dev` (DESIGN §9.2, D168) serves spaces from their page data and fixtures instead of a game and its mods, through
the real `grace-serve` on loopback, so the web app is developed against the real server, its security checks included.
It also lays a page out with `grace-view` and `grace-draw` and prints its in-game structure, so an author sees their
pages as the game would draw them without Windows. It is a developer's tool: never part of `graceui.dll` or a release,
and it never touches a game.

It is built with the Linux unit-test build and with the Windows test builds (BUILD.md): `bin/grace-dev` (or
`bin\grace-dev.exe`), with its tests in `bin/grace_dev_tests`.

## Serving spaces

```sh
build-linux/bin/grace-dev --space demo --space graceui
build-linux/bin/grace-dev --space-dir path/to/my-space --web web/dist --speed 4 --loop
```

Once ready it prints the spaces, each scenario it plays, where its web files come from and the page's address,
`http://127.0.0.1:8642/`, and keeps serving until Ctrl+C (or SIGTERM on Linux). A line per command follows: the space,
the command, the setting's key or the page it names (never a value), and the answer. It never prints a setting's
value, a command's arguments, the session cookie or a ticket (invariant 5), nor a folder's path (PV3).

| Option | What it does |
|---|---|
| `--space ID` | A built-in space: `graceui` (GraceUI's own) or `demo`. With no space given, both, GraceUI's own first |
| `--space-dir DIR` | An author's space: `DIR/pages.json` and `DIR/settings.json` (the protocol's `space_pages` and `settings_description`, as `tools/pages/build.py` writes them), with `DIR/topics.json` (fixture states) and `DIR/scenario.json` if there are any |
| `--scenario FILE` | Plays `FILE` in the space it names, instead of that space's own scenario |
| `--no-scenario` | The spaces' own scenarios stay still: fixture states alone (`--scenario` files still play) |
| `--speed X` | Plays scenarios X times as fast, from 0.01 to 1000 (default 1) |
| `--loop` | Plays every scenario again and again |
| `--port N` | The port, as GraceUI's `dashboard.port` takes it (1024 to 65535; default its default, 8642) |
| `--web DIR` | The web app: a GraceUI folder whose `graceui.toml` lists its files under `web/` (served as the server always serves them), or a plain folder such as `web/dist`, whose files are handed to the server from memory (a file whose name or type the server does not serve is named and left out; a hidden folder is not read; a link is never followed). With none, a placeholder page |
| `--increment X.Y` | The increment `welcome` names for every space (default 0.2, the increment this branch builds) |

A start fails, saying why and exiting with 2, if an option, a space, a scenario or the web folder fails its check, and
with 1 if the server cannot start (a port another program holds: choose another with `--port`).

### What each space does

- **Checked before it is served.** Its files pass the protocol's decoders, the settings store's and the page view's
  own loads, and grace-dev's checks: topics declared once, a `settings` state topic when it has settings, every
  component's topic declared and of the shape it draws, every header action's command declared, `menu.close` and
  `dashboard.disconnect` in GraceUI's own space alone, and its fixtures (below). The page lint checks the rest of an
  author's space: run `python3 -I tools/pages/build.py --lint space.toml` on its source.
- **Settings** come from the space's own settings store (`grace-pages`' `SettingsStore`, its one validating entry point,
  invariant 3): the `settings` topic's body holds the description, the values (a secret's as `"set"` or `""`), the
  keys changed from their defaults and, for an inherit tri-state on `inherit`, what the scenario says it resolves to
  (D163). `settings.set` and `settings.reset` are answered by the store, with its error code and its words in the
  result's body as `{"detail": "..."}`; a change publishes the topic again at a new revision. Settings start at their
  defaults each run, and grace-dev does nothing with their values (a new `dashboard.port` does not move it).
- **Fixture states.** A state topic watched is published with its body from `topics.json` (revision 1, rising with
  each change); a stream watched, with its rows from the oldest held. A topic with no fixture publishes nothing
  (absence is silent), and grace-dev says so once.
- **Commands.** `dashboard.disconnect` is answered ok, then the server renews its session (every browser disconnected,
  as in game); `menu.close` is refused `not_now`, as there is no menu here; a space's own command follows its
  scenario's reaction, or is answered ok with nothing changed. Each command is answered once.

### `topics.json`

An object of the space's declared topics, `settings` excepted: a state topic's body (an object), or a stream topic's
rows (a list of objects, each with a `seq` number, rising by one, no more than its ring holds). The demo's are in
`fixtures/demo/topics.json`; the page lint checks the built-in spaces' (every declared topic has one).

## Scenarios

A scenario is one space's scripted time, a JSON file: `fixtures/<space>/scenario.json` for a built-in space (compiled
into grace-dev, as the space's pages are into grace-pages), `scenario.json` in an author's folder, or a file given with
`--scenario`. It is checked whole before anything plays; a bad one fails the start and names its file, the line of the
step or reaction that fails, and what is wrong (`bad-scenario.json line 14: step 2: lantern.wick is not a topic of the
space demo`).

```json
{
  "scenario": 1,
  "space": "demo",
  "about": "What it plays, for whoever reads it.",
  "length_ms": 60000,
  "loop": false,
  "inherited": [{"key": "lantern.shield", "on": true, "source": "The lighthouse"}],
  "steps": [
    {"at": 4000, "rows": "lantern.log", "add": [{"time": "19:44", "line": "A moth circled the glass."}]},
    {"at": 8000, "state": "lantern.status", "body": {"tiles": [], "sentence": "The flame flickers."}},
    {"at": 9000, "clear": "lantern.log"}
  ],
  "reactions": [
    {"on": "lantern.clear_log", "do": [{"clear": "lantern.log"}, {"rows": "lantern.log", "add": [{"line": "Cleared."}]}]},
    {"on": "lantern.trim", "answer": {"error": "not_now", "detail": "The wick is too hot to trim."}}
  ]
}
```

| Field | What it holds |
|---|---|
| `scenario` | The format, 1 |
| `space` | The space it plays in |
| `about` | Optional words about it |
| `length_ms` | Optional: a pass's length, at least its last step's time (by default, that time); a week at most |
| `loop` | Optional: whether it plays again and again (`--loop` makes every scenario loop) |
| `inherited` | Optional: what each inherit tri-state of the space resolves to while it is on `inherit` (D163): `key`, `on`, `source` in words, and optionally `page` |
| `steps` | In time order: each has `at`, its time in milliseconds from the pass's start at speed 1, and one action |
| `reactions` | Each has `on`, one of the space's own commands (not a framework one), `do`, a list of actions (16 at most), and `answer`, `"ok"` (the default) or `{"error": "<a protocol error code>", "detail": "<words>"}` |

An action is one of:

- `{"state": TOPIC, "body": {...}}`: a state topic's body is replaced, and published at a new revision.
- `{"rows": TOPIC, "add": [{...}, ...]}`: 1 to 100 rows are appended to a stream, and published; grace-dev numbers them
  (a row has no `seq` of its own), and the stream keeps no more than its ring.
- `{"clear": TOPIC}`: a stream's rows are dropped, and its next rows start it over after a gap in their numbers, which
  the server and every view take as a fresh start (§6.3, D161). A view learns of it with those next rows: the protocol
  has no empty batch, so a reaction that clears a stream adds a row to say so, as the demo's does.

Steps fire whether or not a view watches (what nobody watches is kept and sent when watched), and each loop's pass
starts again from the fixture's states. Passes that a slow machine misses are skipped, not played in a burst. The
settings topic changes only through `settings.set` and `settings.reset`, never a scenario.

The demo's scenario, `fixtures/demo/scenario.json`, plays a minute of the lantern's evening (wind, rain, a passing
ship) and reacts to `lantern.trim` and `lantern.clear_log`.

## The in-game view on Linux

```sh
build-linux/bin/grace-dev --dump demo demo.overview --size 800x600
build-linux/bin/grace-dev --dump graceui graceui.menu --size 1280x800 --scale 1.25 --theme clear
build-linux/bin/grace-dev --dump tidepool tidepool.today --size 1920x1080 --space-dir path/to/tidepool
```

`--dump SPACE PAGE` lays `PAGE` out as the in-game menu does (D167): at the display's `--size`, with GraceUI's
`ui.scale` (`--scale`, as `ui.scale` takes it; GraceUI's default, 1.25, without it) giving the effective scale
(H / 1080) × ui.scale (§3.13), the space's
settings at their defaults (GraceUI's own Menu page showing the scale and theme it is drawn at), and the components the
menu draws in game (a table, a key-value list, a check list, stat tiles, a computed sentence; D184) showing their
topics' fixtures, as the menu shows what a space publishes; a page with a master list (D215) gets the list's fixture,
which chooses its first row, and then that row's member's (`<family>/<key>`), as the menu watches it, so the detail is
drawn as the menu draws it. It prints a header,
the structure dump the golden tests compare (§9.3: one line per item, its shape, colour role, place, size and text),
and the tessellated list's counts with the draw-list checker's answer (D76). Text is measured in the layout tests'
fake face (every glyph 8 px wide at a bake of 16 px), so the dump needs no font file and matches `grace-view`'s goldens
line for line; the real faces' widths differ, the structure does not. `--theme clear` changes the palette and so the
vertices' colours, not the structure. It exits with 0 if the list passes the checker, 1 if it is refused.

The C mock of the ABI and a consumer played over it come later, with the ABI's protocol slots (D169).

## Its tests

`grace_dev_tests` (doctest) runs the host over real sockets with `grace-serve`'s own test client
(`src/grace-serve/io/tests/`), checks the spaces and scenarios, and compares `--dump` with
`tests/golden/demo-overview-800x600.txt` (`GRACE_UPDATE_GOLDEN=1` writes it anew). CTest also runs grace-dev itself:
`--dump` against the same golden, a bad space folder and a bad scenario failing the start with their messages
(`tests/run_cli.cmake`), and a run whose page and ticket are fetched and which SIGTERM and then SIGINT stop
(`tests/cli_run.py`). `tests/tidepool/` is an author's space of the tests' own. In a Windows test build:

```sh
unset TMPDIR; WINEPREFIX=<a prefix of its own> WINEDEBUG=-all wine64 build-cl-test/bin/grace_dev_tests.exe
```
