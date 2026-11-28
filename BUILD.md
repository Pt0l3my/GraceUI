# Building GraceUI

GraceUI's in-game parts are C++20 with CMake and Ninja (D106): `graceui.dll`, the host (D69), with ABI 1's table
(D131): notices, panels and their checked draw lists, textures, faces and glyph tables, input and world-state providers
(D137 to D144); `graceui-sys`, the header-only helper a mod compiles in to find GraceUI (D136); two example mods,
`examples/hello-notice` (C, a notice) and `examples/hello-panel` (C++, a panel drawn by its own headless Dear ImGui,
D144); and `tools/grace-harness`, which runs the host without a game: its render half, its input half, ABI 1, panels,
two copies and the anti-cheat check. They build for Windows x64 two ways, both on Linux: with clang-cl and
lld-link against Microsoft's CRT and SDK, the release build, and with MinGW-w64. The harness runs under Wine on Xvfb,
drawing through Mesa's software Vulkan (lavapipe). The pure modules (`grace-input`, D124; `grace-abi`'s rules, D131)
also build on Linux, with their unit tests, and `graceui.h`'s layout is checked in C and C++ by every build (D109,
D132). Every build has 0 warnings in our own code, and the modules `graceui.dll` links keep its rules: no static object
with a dynamic initialiser or an exit-time destructor, checked by Clang.

Dear ImGui 1.93.0 and MinHook 1.3.4 are downloaded at configure time from their own repositories, every file checked
against its SHA-256 (`cmake/third_party.cmake`), into the build tree; `NOTICE` credits them. So are two fonts (OFL)
from the Noto project's own repository (`cmake/fonts.cmake`): Noto Sans Regular, GraceUI's notices' face, which the
build puts in `bin\fonts\` beside `graceui.dll`, as GraceUI's own folder holds it (D51), and Noto Sans Mono Regular,
which the second example registers, in `bin\hello-panel\`; each folder gets the licence as `OFL.txt`. Without
`fonts\NotoSans-Regular.ttf` beside it, `graceui.dll` draws its notices in Dear ImGui's own font (D141). Configuring
needs the network once per build tree.

## The unit tests: Linux

The pure modules hold no Win32 and no Dear ImGui, so they build and are tested on Linux, the only targets a Linux
build has (D106, D124). The tests are doctest 2.4.11 suites under CTest; doctest is fetched pinned by SHA-256 like the
libraries above (`cmake/doctest.cmake`), for the tests only. Run them with AddressSanitizer and
UndefinedBehaviorSanitizer, with ThreadSanitizer for the threaded parts, and with GCC:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build-linux -G Ninja -DGRACEUI_SANITIZE=address,undefined
cmake --build build-linux && ctest --test-dir build-linux --output-on-failure
CC=clang-18 CXX=clang++-18 cmake -S . -B build-linux-tsan -G Ninja -DGRACEUI_SANITIZE=thread
cmake --build build-linux-tsan && ctest --test-dir build-linux-tsan --output-on-failure
CC=gcc CXX=g++ cmake -S . -B build-linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux-gcc && ctest --test-dir build-linux-gcc --output-on-failure
```

`grace-input`'s 76 cases cover the holder's answers, the close routine from every exit path, the player's exit and its
timing, the watchdog, the game's view of DirectInput and XInput (untouched, at rest, a closing press hidden until
released, the packet number), the holder's pad view with the opening press, the window-message routing, the surrogate
pairing (lone and swapped halves too), the ANSI byte joiner and the UTF-8 one (cut short, stray, overlong, a surrogate,
past U+10FFFF), and the hand-overs between threads under ThreadSanitizer. From the host's review (D145): a claim of
danger or input turned off between a request's read and its grant ends the new holder at once, the newest claims
decide whatever order the writers settle in, and no holder outlives a danger claim over 2,000 rounds of two threads.
Wine has no pad, so the pads' masking is checked here only. Since D155 (2026-11-17): the mods' hotkeys, bound, unbound when taken
and refused, their bounds, presses counted for their own mod alone, a removed or replaced hotkey counting nothing, the
filter's routing of a hotkey's press, of a key bound with no modifier, of a press the game saw begin and of presses while
a holder has input, DirectInput's view of them, focus loss, and presses counted on one thread while another adds,
removes and takes, under ThreadSanitizer.

`grace-abi`'s 48 cases cover `connect`'s answers across versions, ids and the session's states (ok, too_old against
floors below, at and above GraceUI's, duplicate_id, inactive with each reason, refused with each), handles that outlive
a disconnect, two mods in either order and eight connecting at once under ThreadSanitizer, the room for 32 mods; the
notices' keys, durations, bounds and refusals, and the present thread's reads through the triple buffer while three
writers replace their notices, never torn; the UTF-8 decoder (overlong forms, surrogates, cut sequences) and the shapes
of an id, a name, caps and a notice's text; and the anti-cheat check's matching of names. From S10.4.c: the draw-list
checker's every refusal (the size bounds, a command's vertex and index ranges and sums that would wrap, an index past
its own command's vertices, clip rectangles not finite, inverted or past the panel, a texture neither the mod's nor the
atlas, the atlas at another generation or before it is baked) and what it lets through; a panel's triple buffer under a
writer thread whose lists grow and shrink, the reader never meeting a list half written or an older one, with no
allocation and no free on the reader's side (the test binary's own `operator new` counts them per thread, except under
ThreadSanitizer, whose runtime defines it); a panel's description, its place by each of the nine anchors and on a
resize, its life (hidden at first, its own mod's only, gone with it, an old id naming nothing) and the bounds, and the
present thread's view of a slot that a writer makes and removes meanwhile; textures' budgets per connection, the
registration window, their life from a mod's copy to the GPU and back, a texture never uploaded taken back, and the
present thread's side making no allocation or free while a mod's thread registers; faces' budgets, the seal at
warm-up and a face being written then, a font file's table directory and a face's ranges; and the providers' claims,
combined, replaced, gone with their provider and read without waiting. From the host's review (D148, D152, D153): the
code points a font's character map gives (formats 4, 12 and 13 on invented maps, and every code point where the map
cannot be read), a face's area and the atlas's room (EldenNet's faces first, the other mods' within their half), a
command stride bounded at 256 bytes, a long name cut to its end on a character's start, `Panels::shown`, and a
disconnecting mod's slot given to nobody until its clean-up is done.

The draw-list checker also has a libFuzzer target, `grace_drawlist_fuzz`, and a mod's font file another,
`grace_font_fuzz` (Clang on Linux; their corpora are yours). The rules' library carries the fuzzers' coverage, so that
each is guided by what the rules do with its bytes:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build-linux-fuzz -G Ninja -DGRACEUI_FUZZ=ON -DGRACEUI_UNIT_TESTS=OFF \
  -DGRACEUI_SANITIZE=address,undefined
cmake --build build-linux-fuzz --target grace_drawlist_fuzz grace_font_fuzz
build-linux-fuzz/bin/grace_drawlist_fuzz -max_total_time=60 -max_len=4096 <a corpus folder>
build-linux-fuzz/bin/grace_font_fuzz -max_total_time=60 -max_len=8192 <another corpus folder>
```

Any bytes become a list and a context; the checker must answer without a crash or undefined behaviour, and a list it
lets through must index only its own vertices (188,667 runs in 60 s, none failing, on 2026-11-16; 134,050 in 40 s with
the rules instrumented). Any bytes become a font file and a face's ranges; the font checks and the character map's
count must answer without a crash or undefined behaviour, and the count never exceeds the ranges' own (210,174 runs in
60 s from a corpus seeded with invented maps of formats 0, 4, 6, 12 and 13, none failing, on 2026-11-16). `grace_abi_layout_c` and
`grace_abi_layout_cpp` compile `graceui.h` as C and as C++ with a `static_assert` on every struct's size and every
field's offset; CTest runs both on Linux, and both Windows builds compile them.

`grace-serve`'s 49 cases (`grace_serve_tests`, D158) cover the head parser's every refusal with its status and the
order of its decisions; the caps at and past their limits; the Host, Origin, Fetch Metadata and cookie checks (an
`en_session` of a narrower Path, a flood of cookies); the response writer's headers on every status; the redirect's
Location; the RFC 3174, RFC 4648 and RFC 6455 vectors; every frame failure with its close code; UTF-8 across fragments;
Ping, Pong and Close in a session; a chunking differential (whole, byte by byte, random splits); and no allocation. Its
head parser and frame decoder have libFuzzer targets, `grace_head_fuzz` and `grace_ws_frame_fuzz`, built as the protocol's
fuzzer below (`--target grace_head_fuzz grace_ws_frame_fuzz`), each run with a corpus folder of its own and the committed
seeds as a second, read-only one:

```sh
build-linux-fuzz/bin/grace_head_fuzz -max_total_time=60 -max_len=16640 -dict=src/grace-serve/fuzz/head.dict \
  <corpus> src/grace-serve/fuzz/corpus/head
build-linux-fuzz/bin/grace_ws_frame_fuzz -max_total_time=60 -max_len=49152 <corpus> src/grace-serve/fuzz/corpus/frame
```

`-len_control=0` reaches the caps sooner. On 2026-11-20 they ran 384,505 and 395,309 times in 60 s, none failing.

`grace_spaces_tests` (D169) take a mod's space across the ABI end to end through the real server: registration's checks, sub, state, cmd, answer once, a buffer too small, the 10 s deadline, a disconnect, and GraceUI's own space's settings shared by both views; CTest runs them on Linux (TSan included), and the Windows test builds build `bin/grace_spaces_tests.exe` for Wine.

`grace_serve_io_tests` is the server security suite (§9.1, D161): 35 cases over real loopback sockets with GraceUI's own
space and the demo space, in seconds, through short deadlines. CTest runs it in every Linux build, and TSan covers its
threads. The Windows test builds (`-DGRACEUI_TEST_EXPORTS=ON`, clang-cl and MinGW-w64) build
`bin/grace_serve_io_tests.exe`: run it with `unset TMPDIR; WINEPREFIX=<dir> WINEDEBUG=-all wine64
build-.../bin/grace_serve_io_tests.exe`; it needs no Xvfb, and exercises the Winsock code (WSAPoll, the self-connected
UDP wake, `SO_EXCLUSIVEADDRUSE`, non-inheritable handles, BCryptGenRandom). On Linux the server never sets
`SO_REUSEADDR`, so a restart on the same port within TIME_WAIT reports the port taken; the suite takes a fresh port each
time. Where the system has no IPv6, the `::1` listener's test skips itself.

## The tokens

`tokens/grace.toml` is the Grace theme's one source (D157). `tools/tokens/build.py` checks it (contrast in every variant,
the type minimums, the durations, no looping animation in `web/src`) and writes `web/src/tokens.css` and
`src/grace-tokens/include/grace/tokens.h`, both committed; edit the TOML, never the outputs.

```sh
python3 -I tools/tokens/build.py            # check, then write both outputs
python3 -I tools/tokens/build.py --check    # check, and fail if an output is not what the TOML builds
python3 -I tools/tokens/build.py --table    # §3.3's contrast table, as the tokens give it
python3 -I -m unittest discover -s tools/tokens
```

The Linux build's CTest runs `--check`, the tool's tests and the header's (`grace_tokens_tests`).

## The protocol

`src/grace-protocol/protocol.toml` is the bridge protocol's one schema (D159). `tools/protocol/generate.py` checks it
and writes the C++ types and decoders (`src/grace-protocol/include/grace/protocol/messages.h`,
`src/grace-protocol/messages.cpp`) and the web app's types (`web/src/protocol.ts`), all committed; edit the schema,
never the outputs.

```sh
python3 -I tools/protocol/generate.py          # check the schema, then write the three outputs
python3 -I tools/protocol/generate.py --check  # fail if an output is not what the schema generates
python3 -I -m unittest discover -s tools/protocol
```

The Linux build's CTest runs `--check`, the generator's tests, `grace_protocol_tests` (the golden frames in
`src/grace-protocol/tests/golden/frames.jsonl`, round trips, refusals, the largest frame's cost, printed) and, where
`tsc` is installed, `tsc --strict` on the TypeScript. nlohmann/json 3.12.0 is fetched pinned by SHA-256
(`cmake/nlohmann_json.cmake`). The frame parser's fuzzer, as grace-abi's:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build-linux-fuzz -G Ninja -DGRACEUI_FUZZ=ON -DGRACEUI_SANITIZE=address,undefined
cmake --build build-linux-fuzz --target grace_frame_fuzz
build-linux-fuzz/bin/grace_frame_fuzz -max_total_time=60 -max_len=20000 <a corpus folder: the golden frames, one a file>
```

## The page descriptions

GraceUI's built-in spaces, its own and the demo space, are `src/grace-pages/spaces/<id>/space.toml` (D160).
`tools/pages/build.py` checks each against the protocol's schema and by the page lint, then writes
`fixtures/<id>/pages.json`, `fixtures/<id>/settings.json` and `src/grace-pages/builtin.cpp`, all committed; edit the
TOML, never the outputs. `--lint` checks another project's spaces against this GraceUI's rules, and `--build` compiles
one, a mod's, into a space folder's `pages.json` and `settings.json` and, if asked, a C header of both (D283;
`docs/modders/pages.md`). `examples/hello-space` runs it at its build, so the Windows builds need Python 3.11 on the
machine that builds them, as the Linux build's checks do.

```sh
python3 -I tools/pages/build.py                  # check the spaces, then write their outputs
python3 -I tools/pages/build.py --check          # fail if an output is not what the spaces build
python3 -I tools/pages/build.py --lint <space.toml>...
python3 -I tools/pages/build.py --build <space.toml> --out <folder> [--minor N] [--header FILE] [--check]
python3 -I -m unittest discover -s tools/pages
```

The Linux build's CTest runs `--check`, the lint's tests (each rule failing on a demo space seeded to break it) and
`grace_pages_tests` (every built-in space decoded by grace-protocol's own decoders).

## The in-game views

`grace_view_tests` (D162) drive GraceUI's own space and the demo space with keys and buttons as §5.3 and §5.4 give them
and check every command, search, and the layout's structure dumps against `src/grace-view/tests/golden/` (the Menu page
at 1920 × 1080 and at 1280 × 800 and 150%, the demo's Behaviour page); after a deliberate change to the layout, write
them anew with `GRACE_UPDATE_GOLDEN=1 build-linux/bin/grace_view_tests` and read the diff before committing.
`grace_draw_tests` tessellate the Menu page and pass it through grace-abi's draw-list checker, check that Grace Clear
changes colours and nothing else, the shapes' vertices and fringes, hairlines on whole pixels, and that a second frame
allocates nothing. Two more goldens there are the browser view's parity references (D166): `rows-order.txt`, every
page's rows in grace-view's focus order, and `formats.txt`, every setting's formatted value and each press's command;
the web view's tests compare against both.

## The browser view

`web/` is the browser view (D166): Preact, its signals and TypeScript, built with Vite into `web/dist/`. `web/README.md`
has every command; in short, from `web/`:

```sh
npm ci --ignore-scripts && npm audit
npm run build && python3 -I ../tools/web/licences.py   # dist/, then the licence check and dist/licences.txt
npm run typecheck && npm test && npm run test:dom
```

The Linux build's CTest runs the licence tool's tests always, and the type check, the logic tests, the build, the
licence check, the bundle scan and (with the machine's own Playwright and Chromium) the DOM tests when Node and
`web/node_modules` are present; `protocol_typescript` then uses the web app's pinned TypeScript.

## The fake host: grace-dev

`grace-dev` (D168, `tools/grace-dev/`) serves spaces from page data and fixtures on the real server, plays scripted time,
and dumps a page's in-game structure; it is built with the Linux unit tests and in the Windows test builds.
`tools/grace-dev/README.md` has its command line, the space folder's and the scenario's formats, and how its tests run
under Wine. With the web app built, `build-linux/bin/grace-dev --space demo --space graceui --web web/dist --loop` serves
the browser view at `http://127.0.0.1:8642/`.

The browser view's own check of a space folder, the same folder grace-dev takes, is `npm run -s check:space -- <folder>`
in `web/` (D265; `web/README.md`): it prints what the built view shows of each page, on this PC and on a paired device,
and fails on an error, a CSP violation, a request off the origin or a faint text.

## The release build: clang-cl

Built as EldenNet's engine is (its ARCH §2.6), against the CRT and SDK that [xwin](https://github.com/Jake-Shadle/xwin)
(MIT or Apache-2.0) downloads, pinned in `cmake/xwin.lock` (the same pins as the engine's, so one splat serves both).
Microsoft's licence covers those files: xwin asks for its acceptance, and they stay in a cache outside every repository.

```sh
xwin --accept-license --manifest-version 17 --channel release --arch x86_64 --variant desktop \
     --sdk-version 10.0.26100 --crt-version 14.43.17.13 splat --output ~/.cache/xwin
cmake -S . -B build-cl-rel -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cl-rel
python3 -I tools/check_safety.py --exports build-cl-rel/bin/graceui.dll
```

- `build-cl-rel/bin/graceui.dll` exports `GraceUI_Version` and `GraceUI_RequestApi` and nothing else; the check above
  fails a release build with any other export, and any build whose imports leave the reviewed list (kernel32, user32,
  gdi32, imm32, ws2_32, bcrypt, dwmapi, and MinGW's msvcrt): no Direct3D DLL is imported, as D170 says. `hello-notice.dll` and `hello-panel.dll`, the example mods, are built
  beside it, the second with Dear ImGui's core alone (`gu_imgui_core`, no backend, D144).
- The flags: `/EHsc` (the exports' catch-all guards depend on it, D110), `/W4 /WX /permissive- /utf-8`, the static CRT
  (`/MT`) in every configuration, `/Brepro` at compile and link, no incremental link; a release build adds ThinLTO,
  `/Z7 -gline-tables-only` and a PDB beside each binary. Paths in debug information are relative to the source and
  build trees, and each binary names its PDB by file name only (`/pdbaltpath`), so no build machine's path ships.
- `XWIN_ROOT`, a cache variable or in the environment, points elsewhere than `~/.cache/xwin`; the toolchain file
  refuses a splat whose CRT or SDK differs from the lock.
- **The GraceUI folder** (§2.7, D165): after each build, `build-cl-rel/GraceUI/` holds `graceui.dll`, `LICENSE.txt`,
  `NOTICE.txt` and the `graceui.toml` that lists them, each with its size and SHA-256, as `Game\GraceUI\` is installed;
  the web app and the fonts join it once they are built. `tools/release/manifest.py` writes the file (from
  CMakeLists.txt's version, `graceui.h`'s ABI minor and `protocol.toml`'s version and caps) and checks one
  (`python3 -I tools/release/manifest.py check FOLDER`, which names every missing, extra or changed file); its date is
  `SOURCE_DATE_EPOCH`'s, else today's. A test build lays out its folder the same way, test exports and all, for a
  consumer's own tests to install from; it is never a release.

## The test build and the harness

`-DGRACEUI_TEST_EXPORTS=ON` adds the test-only exports (`GraceUI_Test_*`, declared in
`src/grace-abi/include/graceui_test.h`), the counters behind them, the harness, `grace-harness-shield.dll`, an
empty DLL of ours that stands in for an anti-cheat's module, and `grace-harness-steam.dll`, a DLL of ours with the
names of Steam's flat API that the menu-pad scenario's test build is told to use for Steam's floating keyboard
(`GraceUI_Test_SetSteamModule`, D275), recording each call. A release build never has them.

A test build also reads `GRACEUI_TEST_PORT` when its browser view starts (D234): `free` serves it on a loopback port
the system has free (asked again, up to five times, if another program takes it first), a number from 1024 to 65535 on
that port; unset, on the port in GraceUI's settings, as a release build does. GraceUI's settings file is never changed
by it, and `GraceUI_Test_GetServer` gives the port in use, so that two trees running GraceUI's cases at once need not
share 8642. The harness's free-port scenario checks it.

A test build guards every heap block of graceui.dll's own (D235, `src/grace-guard`): its operator new and delete and
Dear ImGui's allocator go through the guard, which puts 16 bytes of a known value before and after each block, keeps a
freed block (filled with another) in a quarantine of 4096 blocks and 64 MiB, and checks each block when it is freed,
when it leaves the quarantine, and every `GRACEUI_TEST_HEAP_CHECK_MS` on GraceUI's thread (100 to 600000; 5000 by
default). A breach is logged, recorded for `GraceUI_Test_GetHeapGuard`, and raised as `GRACEUI_TEST_HEAP_GUARD_BREACH`
(`0xE0475548`), which a handler of GraceUI's own, last in the process's list, ends the process with; the spike
observer's recorder, first in it, writes it down before. A release build has none of it. `GRACEUI_TEST_HEAP_GUARD=off`,
read at graceui.dll's first allocation, makes its blocks malloc's own again: the guard costs about four times malloc's
time per block under Wine, which slows GraceUI's warm-up past checks that count its frames. So the harness's regression
run sets it off (its children inherit it), and its heap-guard scenario turns the guard on for itself and makes each
kind of breach on purpose; `grace-harness.exe --heap-guard on` is the breach sweep, every scenario with the guard on,
where a breach ends its scenario's process and the checks that time a warm-up may fail.

```sh
cmake -S . -B build-cl-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release \
  -DGRACEUI_TEST_EXPORTS=ON
cmake --build build-cl-test
python3 -I tools/check_safety.py --exports build-cl-test/bin/graceui.dll --test
unset TMPDIR    # Wine 9.0's wineserver aborts at start with TMPDIR set ("free(): invalid pointer"): see below
export WINEPREFIX=<a prefix of its own> WINEDEBUG=-all LC_ALL=C.UTF-8
wine64 reg add 'HKCU\Software\Wine\Direct3D' /v renderer /t REG_SZ /d vulkan /f    # once per prefix: see below
xvfb-run -a -s "-screen 0 1280x720x24" wine64 build-cl-test/bin/grace-harness.exe
```

A run fails with "no window" when a Wine server is still running for the prefix from an earlier `xvfb-run`. That
server stays bound to a display that has gone, so the harness can't open its window. Stop it before every run with
`wineserver -k` (Debian keeps it in `/usr/lib/wine`); with that, three runs in a row of each build passed all 82 checks
(2026-11-16, S10.4.c). Frame-cost checks then failed now and then under another build's load (a p99 of 1.0 to 1.6 ms
against 1 ms, with no allocation, its median as usual), and one functional check raced the example's thread; since
D154 and the refusals check's fix, ten runs in a row of each build passed all 95 checks (2026-11-16) (96 since D155's hotkeys check, which a full run passed on 2026-11-17; 106 since D156's two example
scenarios, which a full run passed on 2026-11-20; 177 since D181's quiet scenario, which a full run of the clang-cl build passed on 2026-11-20, and of the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual, which passed when run again; 185 since D182's menu-mouse and D183's two anti-cheat scenarios, which a full run of each build passed on 2026-11-20; 186 since D184's components in menu-space; at 0.2.7 the MinGW build passed all 186, and the clang-cl build all but two frame-cost checks, run while Linux builds loaded the machine, which passed when run again; at 0.2.8 (G34, D192 to D196) the clang-cl build passed all 186, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual, which passed when run again; at 0.2.9 (D197, D198) each build passed all 186; at 0.2.10 (D199) the clang-cl build passed all 186, and the MinGW build all but the menu's frame cost, its p99 over budget at 640 by 360 at medians as usual and with no allocation, which failed twice more alone and passed on the next run; at 0.2.11 (D200) each build passed all 186; at 0.2.12 (D202) the clang-cl build passed all 186, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual, which passed when run again; 192 since D204's chords scenario, at 0.2.14 the clang-cl build passing all 192, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed when run again; 193 since D206's Hotkeys page check, which a full run of each build passed at 0.2.15, with every Linux tree's suites after D207's fix; 203 since the bindings scenario of D208 to D210, at 0.2.16 each build passing all but the chords scenario's first check, which still pinned ABI 1.3 and passed on both once it took 1.3 or later; 206 since D212's wake scenario, which at 0.2.17 the clang-cl build passed whole, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed when run again; 216 since D214's typing scenario, which at 0.2.18 each build passed whole; 221 since D215's master scenario, which at 0.2.19 each build passed whole; 224 since D216's slow scenario, which at 0.2.20 each build passed whole; 230 since D217's editor scenario, which at 0.2.21 each build passed whole; 231 since D218's chosen row in it, which at 0.2.22 each build passed whole; 232 since D219's search across spaces, which at 0.2.23 each build passed all but the menu's frame cost, its p99 over budget at medians as usual, on a four-core machine where 0.2.22's build, measured beside it, failed and passed that check by turns as 0.2.23's did, and where 0.2.23's passed it alone; at 0.2.24 (D220) each build passed all 232; 245 since G9's pairing scenario (D221 to D225), run from the build's GraceUI folder with the network listener on 127.0.0.2, which at 0.2.25 the MinGW build passed whole, and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed when run again alone; 246 since G28's upload in the pages scenario (D226 to D229), which at 0.2.26 each build passed whole, with every Linux tree's suites, the web view's 74 unit and 21 browser tests, and the Windows suites of both builds under Wine; 248 since D230's first-frame step and log file in the two spike scenarios, which at 0.2.27 the clang-cl build passed whole, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which failed seven times more alone, then passed two runs of four, where the same build without D230's two changed files passed one of two: this machine's noise, not D230's atomic load a frame; 250 since D231's fault recorder in the two spike scenarios, which at 0.2.28 the clang-cl build passed whole, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, graceui.dll being unchanged since 0.2.27 but for its version; 252 since D232's menu-recentred scenario (a game that recentres its cursor every frame and reads raw mouse input), which at 0.2.29 each build passed whole; 256 since G40's and G41's menu-away scenario, which at 0.2.30 the MinGW build passed whole, and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed when run again alone; the observer's heap-corruption code, added after that run, passed both spike scenarios on each build; 258 since D234's free-port scenario, which at 0.2.31 the MinGW build passed whole, and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed two runs of four alone, and the pairing scenario's check that Escape voids the code: its wait read the menu's lists from before the device's pairing and ui.scale laid the menu out anew, so it could end before Escape was handled; the harness now reads them afresh and waits for the code's end itself, and the scenario then passed on each build alone and twice on clang-cl with four cores kept busy; 266 since D235's heap-guard and heap-guard-off scenarios, which at 0.2.32 the clang-cl build passed whole, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed two runs of three alone (the first run with the guard on in every scenario had failed 11 and 16 checks that count warm-up frames, which is why the regression run turns it off); and the breach sweep, `--heap-guard on`, every scenario with the guard, found no breach on either build: each scenario's process that failed ended with 1, on those warm-up checks (13 on clang-cl, 16 on MinGW), and none with the guard's code but the heap-guard scenario's own probes; at 0.2.33 (D236), before G42's scenario, each build passed all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation; 268 since G42's menu-focus-ring scenario (D237), which at 0.2.33 each build passed whole; 271 since D238's and D239's counters in the input and menu-away scenarios, which at 0.2.34 each build passed whole; 280 since D240's hook leads in every check of the hooks, the layered hook in the input scenario and the two copy-beside-mod scenarios, which at 0.2.35 each build passed whole; 281 since D241's italic face in the display scenario, which at 0.2.36 each build passed with the main, menu and pages scenarios, the MinGW build's menu frame cost on its second run, its p99 over budget at medians as usual; 287 since D242's menu-pad scenario, which at 0.2.37 each build passed but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed when the menu scenario ran again (0.2.36's MinGW build, run beside it, failed and passed that check the same way); 288 since D243's check of what pressed a hotkey in the chords scenario, which at 0.2.38 the MinGW build passed whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual, which passed when the menu scenario ran again; at 0.2.39 (D245, the menu-space scenario's links back) each build passed all 288 whole; at 0.2.40 (D246's key caps),
on a machine started afresh, the clang-cl build passed all 288 whole and the MinGW build all but the menu's frame cost,
its p99 over budget with no allocation, which passed one run of two alone; its medians stood higher than before, at
about 150 µs at 640 by 360 and 200 at 1920 by 1080, and 0.2.39's MinGW build, built beside it and run in turn with it
three times each, stood the same (medians of 142 to 218 µs against 0.2.40's 138 to 226, one run of three failing the
p99 on each), so the machine and not the key caps raised them, though the list grew from 3004 vertices to 3292; at 0.2.41 (D247's drawn arrows, the list at 3316 vertices) the MinGW build passed all 288 whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed two runs of two alone; at 0.2.42 (D248's rail foot, D249's empty words, protocol 1.10) the MinGW build passed all 288 whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual (about 150 µs at 640 by 360 and 200 at 1920 by 1080) and with no allocation, the list unchanged at 3316 vertices, which passed one run of five alone; at 0.2.43 (D250's filter bar, protocol 1.11) the clang-cl build passed all 288 whole, D248 included, and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, which passed two runs of three alone; at 0.2.44 (D251's documents, prose form field and report preview, protocol 1.12) the first full run stopped the typing scenario's hire form at Post, since the form gained a Notes field the walk did not yet take, and with the walk through Notes, its prose well and the note sent, the typing scenario passed all 10 alone on both builds; otherwise the MinGW build passed whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed one run of three alone; at 0.2.45 (D252's quick switches and live updates, protocol 1.13, and a 289th check: the pad's Start opening the quick switches, which the game never saw, A flipping the bell once its state came, B closing them) the clang-cl build passed all 289 whole and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed one run of three alone; at 0.2.46 (D253's two fixes from EldenNet's 0.2.44 pin) both builds passed all 289 whole, the menu's frame cost included; at 0.2.47 (D254's scope and rows read a page at a time, protocol 1.14, and a 290th check: Lightings asking for nothing until its scope came, then for winter's first page, its rows drawn with "3 in all", and Show more asking with the mod's cursor and adding the next page below) both builds passed all but the menu's frame cost, its p99 over budget at medians as usual (about 150 to 260 µs) and with no allocation, the list unchanged at 3316 vertices, which passed alone in one run of one on clang-cl and one of two on MinGW; at 0.2.48 (D255's four fixes to the paging, in grace-view and the browser alone) the three Linux trees passed all 34 suites and both builds passed the master and typing scenarios whole, run alone, 16 checks each; at 0.2.49 (D256's culling, first focus, row drawer and header switch, protocol 1.15) the three Linux trees passed all 34 suites, and both builds all 290 checks but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed alone in one run of one on each; at 0.2.50 (D257's streams in game and a stream's filter bar, protocol 1.16, and a 291st check: the Log's rows drawn in game once the mod published them) the three Linux trees passed all 34 suites, the clang-cl build all 291 whole and the MinGW build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed alone in one run of one; at 0.2.51 (D258's browser-only, shown-with, gated and ribboned pages, protocol 1.17) the three Linux trees passed all 34 suites, the MinGW build all 291 whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed alone in one run of two; at 0.2.52 (D259's on_present callbacks, ABI 1.8, and eleven checks more: the on-present scenario's ten and its process's, 302 in all) the three Linux trees passed all 34 suites, the MinGW build all 302 whole and the clang-cl build all but the menu's frame cost, its p99 over budget at medians as usual and with no allocation, the list unchanged at 3316 vertices, which passed alone in two runs of two; at 0.2.53 (D260's table columns, wrapped sentences and a tile's small unit in game, in grace-view alone) the three Linux trees passed all 34 suites, and both builds all 302 checks but the menu's frame cost, its p99 over budget with medians of about 190 to 250 µs and no allocation, the list unchanged at 3316 vertices (the menu's own pages hold no mod's table, and D260 changes nothing on the present thread), which passed alone in one run of two on each; at 0.2.54 (D261's wrapping of the rest of the in-game texts, in grace-view alone) the three Linux trees passed all 34 suites, and both builds all 302 checks but the menu's frame cost, its p99 over budget with medians of about 110 to 200 µs and no allocation, the list unchanged at 3316 vertices, which passed alone in two runs of two on clang-cl and one of two on MinGW; at 0.2.55 (D262's six pin points, protocol 1.18) the three Linux trees passed all 34 suites, the clang-cl build all 302 whole and the MinGW build all but the menu's frame cost, its p99 over budget with medians of about 150 µs and no allocation, the list unchanged at 3316 vertices, which passed alone in two runs of two; at 0.2.56 (D263's Copy and Save on a report preview, protocol 1.19, and a 303rd check: a report's Copy clicked and its Save in game) the three Linux trees passed all 34 suites, and both builds all 303 checks but the new one, the menu's frame cost included; the new check's walk first took the Log page's own History request for the Save, then clicked Copy in the back buffer's pixels on a client area Xvfb had cut to 1063 rows, and with both mended the master scenario passed whole alone on each build; at 0.2.57 (D264's pages and topics on this PC alone and its refusal of newer page data, protocol 1.20, and a 304th check: page data for a newer GraceUI refused, in a notice) the three Linux trees passed all 34 suites; the first full run passed all 304 whole on both builds; after its review's fixes, the second passed all 304 on clang-cl and all but the menu's frame cost on MinGW, its p99 over budget with medians of about 120 to 170 µs and no allocation, the list unchanged at 3316 vertices, which passed alone in one run of two; at 0.2.58 (D265's space check of the browser view, in web/ alone, and a 35th suite, web_space_check, over the demo's fixtures) the Linux tree passed all 35 suites twice running, the space check under ctest's parallel load included, and graceui.dll's code is as at 0.2.57 but for its version, so the Windows harness was not run again; at 0.2.59 (D266's typed entry on a slider, in grace-view and the browser, and a 305th check: "1.4" typed into Menu size and set, "9" refused in words without reaching the store) the three Linux trees passed all 35 suites; after its review's fixes, the clang-cl build passed all 305 whole and the MinGW build all but the menu's frame cost, its p99 over budget with medians of about 160 µs and no allocation, the list at 3424 vertices with the footer's Enter cap, which passed alone in two runs of four; the first full run's MinGW half, run while other tests loaded the machine, also missed the pages scenario's join and the anti-cheat flag's stop, which passed whole alone on both builds; at 0.2.60 (D267's Plain text in game, in the host and the harness alone, and two checks more, 307 in all: Plain text turned on in game and its rail's words redrawn at once, then its own faces baked at the next launch, the atlas 2048 by 2048 at a scale of 1.3) the three Linux trees passed all 35 suites; after its review's fixes both builds passed all 307 but the menu's frame cost, its p99 over budget with medians of about 140 to 220 µs and no allocation, the list unchanged at 3424 vertices, which passed alone in one run of one on clang-cl and one of four on MinGW; at 0.2.61 (D268's Plain text for mods, ABI 1.9, and two checks more, 309 in all: Plain text in the display event and at connect, an answer of 1.8's size left alone, and a mod's connect after a kept Plain text) the three Linux trees passed all 35 suites; after its review's fixes the clang-cl build passed all 309 whole and the MinGW build all but the menu's frame cost, which passed alone in one run of one; at 0.2.62 (D269's five points from EldenNet's Workshop build, and a 310th check: a page's presence in the menu by its id) the three Linux trees passed all 35 suites and both builds all 310 checks whole, the menu's frame cost included; the review's fixes to the browser's tests, made while that run's last Linux tree was under way, passed their unit and DOM suites again by hand, 108 and 38; at 0.2.63 (D270's held directions and repeats, and a
311th to 313th check: a held direction steps a slider, a held direction moves along and no further, and a held key's
characters) the three Linux trees passed all 35 suites twice; the first full run passed all 312 then present whole on
MinGW and all but the menu's frame cost on clang-cl; after its review's fixes the second passed all 313 but the menu
scenario's new characters check on both builds, which posted its keys without their characters (the harness's
TranslateMessage makes none) and so upset the close check after it, its frame cost passing on both; fixed, the menu
scenario alone passed both on both builds, its frame cost failing in six runs of eight on MinGW and one of three on
clang-cl with the new check in place, and in one of two on MinGW with it compiled out, so the flake is the
machine's, not the check's; at 0.2.64 (D271's fades, and a 314th check: the menu fades in and out) the three Linux trees
passed all 35 suites twice; the first full run failed on both builds the on_present scenario's refused-list pixel, read
through the menu fading out after its F5, and on clang-cl the menu's frame cost; after its review's fixes (that
scenario waits for the fade) the second passed all 314 whole on clang-cl and all but the menu's frame cost on MinGW,
whose menu scenario then passed whole alone in one run of two; at 0.2.65 (D272's page body fading in, within the
fades check) the three Linux trees passed all 35 suites twice and both builds all 314 checks whole in both full runs,
before and after the review's fixes, the menu's frame cost included; at 0.2.66 (D273's settings followed with the menu
closed, and a 315th check: Plain text from the browser followed with the menu never opened) the three Linux trees
passed all 35 suites, the MinGW build all 315 whole and the clang-cl build all but the menu's frame cost, whose
scenario then passed whole alone in one run of one; at 0.2.67 (D274's confirmation that waits, checked in the view's,
the lint's and the browser's suites, with no new harness check) the three Linux trees passed all 35 suites and the MinGW
build all 315 whole in both full runs, before and after the review's fixes, and the clang-cl build all but the menu's
frame cost in both, whose scenario then passed whole alone in one run of one; at 0.2.68 (D275's Steam keyboard, and
a 316th check: the menu-pad scenario's) the three Linux trees passed all 35 suites, the clang-cl build all 316 whole and
the MinGW build all 316 whole in a second harness run, its first having hung on objects a stopped build had left
half-written (rebuilt, its input and menu-pad scenarios then passed whole alone), and clang-cl all but the menu's frame
cost in that second run; at 0.2.69 (D276's pad_keyboard, ABI 1.10, and a 317th check: the holder-events scenario's)
the three Linux trees passed all 35 suites and both builds all 317 checks whole in both full runs, before and after the
review's fixes; at 0.2.70 (D277's category icons, the Menu page 3,616 vertices from 3,424) the first full run, before
the review's fixes, found grace-dev's dump goldens and the built-in pages' round trip out of date on the Linux trees,
MinGW whole and clang-cl all but the menu's frame cost; the second, after them, passed all 35 suites on the three Linux
trees and all but the menu's frame cost on both builds, whose scenario then passed whole alone on clang-cl in one run of
one and on MinGW in one run of three; at 0.2.71 (D278's flows, and the new menu-flow scenario's five checks, which
passed on both builds in both runs) the first full run, before the review's fixes, passed all 35 suites on the three
Linux trees and all 323 checks but the menu's frame cost on both builds; the second, after them, all 35 suites on the
three Linux trees, all 323 checks on clang-cl and all but the menu's frame cost on MinGW, whose scenario then passed
whole alone in one run of one; at 0.2.72 (D280's paused hotkeys, ABI 1.11, and the new hotkey-pause scenario's four
checks and its process's, 328 in all) one full run, after the review's fixes, passed all 35 suites on the three Linux
trees and all 328 checks whole on both builds; at 0.2.73 (D281's controls notice, and its check in the bindings
scenario, 329 in all) one full run passed all 35 suites on the three Linux trees and all 329 checks whole on both
builds; at 0.2.74 (D282's log file, and its check in the bindings scenario, 330 in all) the first full run failed that
check on both builds, the previous file held open by the harness's own GraceUI so that the write session's log was
truncated; with the last session's log appended to instead, the second passed all 35 suites on the three Linux trees and
all 330 checks whole on both builds, the held-file path taken in each; after 0.2.74, with up to five windows a frame-cost check (D154 as amended, in the harness alone), the menu scenario run alone at a load average below 1 passed whole on both builds, MinGW's 640 by 360 window in its fourth) at a load average
of 1.3 to 10.5 on four cores, a frame-cost check taking a second window in 6 of the 80 checks run, a third in none.
Wine 9.0's wineserver (Ubuntu's 9.0~repack-4build3) aborts as it starts, with "free(): invalid pointer", whenever
`TMPDIR` is set, even to `/tmp`: it frees the variable's own string (a backtrace, 2026-11-20). Every Wine command
fails with it, `wineserver -k` among them, so unset it first; `/tmp` is where Wine puts its server's folder anyway.
The long-name scenario names a folder in Unicode, which Wine can do only in a UTF-8 locale: hence `LC_ALL=C.UTF-8`
above; without it that scenario fails, saying so. On a new prefix, run `xvfb-run -a wine64 wineboot -u` once first. Debian's Wine packages keep `wine64`
in `/usr/lib/wine`: put it on the `PATH` first.

Wine 9.0 with Mesa's lavapipe (`mesa-vulkan-drivers`) creates the Direct3D 12 device, queue and flip-model swap chain
the harness needs. The harness prints one line per check, PASS or FAIL with what it saw, and exits with 0 only if all
passed; the whole run takes a few seconds.

Wine's DXGI factory starts wined3d, Wine's own Direct3D 9–11 layer, whose default renderer is OpenGL. On Xvfb, its GL
context creation hung in at least two runs of four when the host's init thread made its own factory while the
harness's thread was pumping its window's messages (a backtrace showed it in `wglCreateContextAttribsARB`); with
wined3d's Vulkan renderer (the same lavapipe), set as above, none of the seventeen runs since has hung.
The setting belongs to the prefix: Direct3D 12 (vkd3d) runs on Vulkan either way, and Proton's DXGI does not use
wined3d. It plays the game's part: it presents frames of a known clear colour, loads `graceui.dll`, calls
`GraceUI_RequestApi(1)` and reads each checked frame back after GraceUI drew on it. A different clear colour for each
check keeps a stale frame from passing for a new one.

It runs thirty-one scenarios, each in a process of its own: the render half's checks ("main"); two direct queues; the input
half ("input"), in a process whose game loads three XInput DLLs before GraceUI starts; the input half on an ANSI window
("input-ansi"); ABI 1 ("abi"), whose first request is the example mod's; panels and the rest of S10.4.c ("panels"),
whose warm-up comes after the second example mod and a mod of the harness's own have registered their faces and
textures, since nothing is presented until they have; two copies asked at once ("copies"); the
example finding a copy under another name ("found-by-scan") and GraceUI's own folder ("found-by-path", which runs a copy
of the harness from `bin\by-path`, beside the example and `GraceUI\graceui.dll`, as a game's folder); and a simulated
anti-cheat hit by each clause ("anti-cheat", "anti-cheat-parent"); two example mods at once, `hello-notice` and
`hello-debug-view`, the one loaded before GraceUI and the other after the hooks, and the other way round ("examples",
"examples-reversed", D156); graceui-sys with GraceUI not installed ("absent", D164); the in-game menu at 1920 by 1080 ("menu", D167); the menu by mouse ("menu-mouse", D182); a mod's space across the ABI with the browser view's server, run from the build's GraceUI folder ("pages", D169); an anti-cheat found later, by a provider's flag and by the module re-check, each closing that server for the session, from the same folder ("anti-cheat-flag", "anti-cheat-recheck", D183); the menu on mods' spaces, with GraceUI's at the rail's foot, a page's components drawn from the mod's state ("menu-space", D171, D184); the fourth example mod, `hello-space`, its space compiled at its build, in game and in the browser from the build's GraceUI folder ("hello-space", D283); GraceUI's own text field in game on the demo space: a text setting typed and sent (Q typing a letter while it edits), the IME's composition window at its caret, the clipboard both ways, a secret masked with none of its glyphs laid out, a form typed field by field and emptied once taken, a row secret sent with its row's key, search by `/` and Ctrl+F opening a row on its tab, search across spaces opening another space's row (D219), and the close routine keeping a form's text ("typing", D214); a mod's master list in game on the demo space's Lamps page: its first row chosen and its member watched and drawn, ↓ moving the watch to the next member while a late state for the last is dropped, → into the detail and Esc back, and the close letting the member go ("master", D215); a slow command in game, its button reading "Working…" while it waits and the mod's answer after 11 s still taken ("slow", D216); the editor block in game on the Lamps page's Your notes tab: its document on a family's member drawn with a pick's choices from their topic, a line typed and kept through the menu's close, Save sent with the revision the edit was made on and a conflict shown with Reload, a slow draft's proposal beside its field, then used, and a save whose answer names another row, which the list chooses, Save keeping focus once its note is drawn ("editor", D217, D218); the in-game spikes' observer and the stand-in for another overlay, loaded before GraceUI with the typing test, and after its hooks ("spike", "spike-after", D175; `tools/grace-spike/README.md`); a holder's events for its mod through ABI 1.1, with two mods that get none, each way out, a pad through `GraceUI_Test_Pad` and the caret ("holder-events", D176); GraceUI's settings kept between sessions, in three processes of their own (written, read back, and a file that is not GraceUI's: "settings", D179); the display at connect and in its event, and scaled faces ("display", D180); a mod's notices quieted from the Mods page, in two processes of their own (quieted and kept, then read back and shown again: "quiet", D181); mods' pad chords through ABI 1.3, read by read as the game's XInputGetState returns them through `GraceUI_Test_PadRead`: a chord that completes, one that fails and is tapped, standing aside in danger, for a quick press and while a holder has input, a taken chord unbound and an older mod's answered `not_yet`, and the Hotkeys page's list of them ("chords", D204 to D206); hotkeys rebound in the menu, in two processes of their own: a key captured, a taken one asked about and moved, the menu's key moved to F6, each mod built for ABI 1.4 told and its `describe_hotkey` read, all kept and applied in the next session, then every hotkey reset ("bindings", D208 to D210), a mod's wait handle set by the change; a mod's wait handle, set once when asked for and then for a hotkey's press, a holder's key and a resize, each timed by a thread that waits on it, and unset with nothing to poll ("wake", D212); and five from the host's review: a larger window
found before the game's ("decoy-window"), the shared atlas running out of room ("atlas-full"), a request made while
the first is still running ("slow-first"), a second copy with a long name ("long-name") and the game moving to a new
device ("device-change"); a mod of EldenNet's kind beside a second copy, each way round ("copy-beside-mod", "copy-beside-mod-reversed",
D240); and the menu by pad alone ("menu-pad", D242). Each scenario whose first `GraceUI_RequestApi`
matters has a process of its own: the anti-cheat check and the mutex are the first request's (D133, D135). A test build
is told to look for names of the harness's own (`GraceUI_Test_SetAntiCheatNames`): `grace-harness-shield.dll`, loaded
before the first request, and `grace-harness.exe`, every scenario's parent; no anti-cheat software, and no file named
after one, is needed. Six scenarios make no window ("copies", "found-by-scan", "found-by-path", "anti-cheat-parent",
"slow-first", "long-name"), so GraceUI's init thread is still looking for one when their process ends, and Wine may then print a "wine client error"
line among the harness's own; it is harmless. Each process ends with a "-- N passed, M failed" line, the main process's
counting each other process as one check.

One more scenario is run by name only, never in the full run: "page-shots" (D191) registers a mod's space from a folder
named by `GRACE_SPACE_DIR` (its `pages.json`, `settings.json` and `topics.json` as grace-dev takes them, and a
`values.json` of its settings' values if wanted), opens the menu on it and, with `GRACE_SHOTS` set, saves a frame of each
of its pages and tabs; `GRACE_SPACE_INCREMENT` names the increment it registers (0.2 without it). It is for look reviews
of a mod's pages in game, with invented fixtures only.

The game reads its keyboard and mouse through DirectInput and its pads through XInput, as the game does (EldenNet ER02
§7.2). The input checks inject keys and mouse input with `SendInput`, which Wine hands to DirectInput through its
low-level hook and to the window as messages, and post or send the window messages the others need. Two quirks of Wine
9.0 on Xvfb shape them: a key injected by its scan code alone reaches the window with no virtual key, so keys are
injected with both; and Wine brings its key state into line with the X server's on X focus and pointer-grab events (a
click, a focus change, the cursor's confinement), releasing a key injected before them, since the X server never saw it.
So the harness keeps the cursor inside the window, lets those events pass before it injects a key, presses a button
before a key, and checks with `GetAsyncKeyState` that a key it expects held is held. Wine has no pad: `XInputGetState`
answers `ERROR_DEVICE_NOT_CONNECTED`, which the checks require unchanged.

| Check | Passes when |
|---|---|
| harness | Before GraceUI is loaded, a frame reads back as its clear colour |
| version | `GraceUI_Version` answers 0.1 |
| start | `GraceUI_RequestApi(1)` answers ABI 1's table (its size, minor and version this header's), the anti-cheat check finds nothing, and one init thread starts, which installs the hooks in one run, each named in D102's list and leading into this copy's own detour (D240; every scenario's check of the hooks asks the same): the render half's five, `GetDeviceState` (DirectInput is loaded), and the window procedure's filter, each named in D102's list |
| untouched | With nothing to draw, every pixel of the frame is the clear colour, though the host has bound the swap chain |
| warm-up | The present queue is found (and how is printed), the atlas is baked on GraceUI's own thread (D147, D148), the renderer is built and the atlas uploaded |
| Present | The test panel appears through `Present`: its colour, its text's ink, and the clear colour everywhere else |
| Present1 | The same through `Present1`, drawn in every one of five frames |
| references | Each back buffer's references besides the game's are what they were before GraceUI was loaded |
| ResizeBuffers | `ResizeBuffers` to 800×450 succeeds, the panel is drawn at the new size, references unchanged |
| ResizeBuffers1 | The same with `ResizeBuffers1` to 480×270 and three buffers, naming the present queue for each |
| second request | A second `GraceUI_RequestApi(1)` answers the same table and starts nothing: one init thread, one install run |
| second load | Loading the same file again gives the same module and table, and nothing starts |
| second copy | A copy from another folder is another module; it answers null twice and idles (D72) with no hook to read (D240); the first keeps its hooks, each leading into its own detours, draws on, and names it once in a notice, drawn, by its name relative to the game's folder (`second-copy\graceui.dll`), never a path (D135) |
| frame cost | With the panel drawn and the GPU idle before each frame, over 600 frames (in up to five windows, D154): GraceUI's own work in the detour within 1 ms at p99; the whole detour and the runtime's submission calls are printed too, and the same figures for 600 frames run as a game runs them |
| allocations | No allocation and no free on the present thread in the detours over those 1,200 frames, after warm-up |
| idle cost | With nothing to draw: the detour's time, and again no allocation or free |
| new swap chain | The game releases its swap chain and makes another on the same window: that succeeds, and the panel is drawn on it |
| second swap chain | A second swap chain on another window passes through untouched while the first is bound; once the first ends, the second is bound and drawn on |
| render path | The render path is still on after every check |
| two queues | (a second process) With two direct queues executing, the host takes neither and the frame stays untouched |
| named queue | Once `ResizeBuffers1` names the present queue, the panel is drawn |
| input harness | (the input scenario, a third process) Before GraceUI, the game's DirectInput reads an injected key and mouse movement, and three XInput DLLs are loaded |
| input hooks | The hooks are installed in one run: the render half's five, `GetDeviceState`, `XInputGetState` in each of the three XInput DLLs and the window procedure's filter, each named in D102's list |
| pass-through | With no holder, DirectInput reads an injected key, mouse movement and a button as they are, `XInputGetState` answers as before GraceUI, and F5 alone is hidden from the game, its messages too |
| request_input | A request is granted; another is refused naming the holder (owner and kind); the holder asking again is told it holds input; a release by another is ignored |
| neutral | While a panel holds input with a key and a button held, the game's DirectInput reads nothing and `XInputGetState` answers as before; the key's messages are kept from the game, and Dear ImGui sees the key and draws its own cursor |
| closing press | The holder released with the key and the button still held: the game reads neither, 300 ms on as well, until each is released, then reads the next press; it never sees the messages of the press that began while the holder had input |
| characters | Posted `WM_CHAR` units, among them a surrogate pair, a lone high half, a lone low half and swapped halves, and `WM_UNICHAR` reach Dear ImGui as the right code points (U+FFFD for each lone or misplaced half), and none reaches the game |
| IME | With a caret reported, `WM_IME_STARTCOMPOSITION` places the IME's composition window at it; no IME message reaches the game |
| Escape held | Escape held for 1 s ends the holder (the player's exit); neither its press, its repeats nor its release reach the game, and the next press does |
| menu key | With a panel holding input, the first F5 warns and the second within 2 s ends it (the player's exit); with the menu holding input, one F5 closes it; F5 never reaches the game |
| opening press | A key held as a panel takes input does nothing in it until released; pressed again, it does |
| focus loss | The focus moving to another window ends the holder, and Dear ImGui no longer sees the key it saw held |
| danger | A provider's danger makes `request_input` answer not_now (danger), and ends a holder |
| no render | With no frame presented for 2 s, the holder ends while the game reads input on |
| input allocations | No allocation and no free in the filter, the detours or on the present thread: over 1,000 reads and messages with no holder, and over 300 frames of a holder fed input as a player types; no event dropped |
| input cost | GraceUI's own work per message in the filter and per read in each detour, the original's time left out, with and without a holder: within 50 µs at p99 each (EST) |
| holder frame cost | With a holder fed input and its cursor drawn, the GPU idle before each frame, over 300 frames (in up to five windows, D154): GraceUI's own work in the detour within 1 ms at p99 |
| input path | The render path and input are still on after every check |
| hotkeys | Through ABI 1, two mods (D155): the first's F11 (bound with no modifier) and Ctrl+K register bound, the second's F11 unbound with a log line; Escape, an action again and an action that is not an id refused, a pad chord not_yet; injected F11 fires once through `poll`, the other mod gets nothing, and neither DirectInput nor the window's messages show it to the game; Ctrl+K fires once with K hidden from DirectInput and its messages kept; K alone reaches the game and fires nothing; with a holder nothing fires; after the first mod's disconnect F11 reaches the game and its `poll` answers not_connected |
| render off | Last, the render path turned off as a fault turns it off: input goes off with it, the holder ends (a fault), F5 reaches DirectInput and the window again, and a request is not now (D150) |
| ANSI characters | (the input-ansi scenario, a fourth process) On an ANSI window, filtered as one: bytes in Windows-1252, a Shift-JIS character in two posted `WM_CHAR`s and one whole in `WM_IME_CHAR`, `WM_UNICHAR`, and in UTF-8 as the code page characters of two, three and four bytes and a sequence cut short (U+FFFD), reach Dear ImGui as code points, and none reaches the game |
| example mod | (the abi scenario) `hello-notice.dll`, loaded before GraceUI, finds it through `graceui-sys` by name once it is loaded (at its fourth look or later), connects as it names itself, and its request starts the host |
| example's notice | Its notice is drawn at the top right, and nothing else |
| table | 184 bytes, ABI 1.2, GraceUI's version, every slot set; majors 0, 2 and 0xFFFFFFFF answer null; `has_cap` names `notices`, `panels`, `textures`, `faces`, `input`, `world-state`, `hotkeys`, `pages`, `holder-events` and `display`, and not `log` |
| connect answers | Seventeen requests across versions and ids answer ok, duplicate_id, too_old (naming both versions, with GraceUI's notice naming the mod and one log line) or refused with the reason; a null request, one below its first size, a null answer and one too small are refused, nothing written where it should not be |
| struct sizes | A newer mod's larger request is read as far as this GraceUI knows it, and its larger answer written no further |
| two consumers | Two mods connect in either order, each with its own handle; both notices drawn, in order; both gone once they disconnect |
| disconnect | A handle after its disconnect names nobody, in every slot; the id connects again under a new handle |
| not yet | The four slots still only declared (`register_pages`, `publish`, `answer`, `log`) answer not_yet to a connected mod, and all sixteen after `notice` not_connected to a handle of 0 |
| notices | Shown, replaced in place by key, drawn in its tone, withdrawn by an empty text; a timed one goes when its time is up; four a mod, a fifth full; refused when it is not UTF-8, too long, empty without a key, of an unknown tone, null or too small; a long text three lines at most, on the Grace look's 22 px lines (D141); a mod's notices go with it |
| notice allocations | As notices come, change and go over 300 frames after warm-up (in up to five windows, D154): no allocation or free on the present thread, and GraceUI's own work within 1 ms at p99 |
| before warm-up | (the panels scenario) `hello-panel.dll`, loaded before GraceUI, connects and registers its face, two textures and its panel; the harness's mod registers a texture and faces of 18 and 24 px, a third face full, a file that is not a font and a size of 2 px refused, the font's whole BMP at 128 px full (over the atlas's room, D148); `glyph_table` answers not_ready |
| panels made | The harness's mod makes 16 panels (EST), the next full, GraceUI's menu layer refused; panels start hidden; a texture after the mod's first submit is too late |
| atlas and faces | Warm-up bakes one atlas, generation 1, with four faces (GraceUI's notices' face from `bin\fonts\` and the mods' three) and uploads the three textures with it, in one upload; face 1's table: over 300 glyphs, 15 px, sorted, 'A' in it; the 24 px face's own size; face 999 refused; a face after warm-up too late |
| example's panel | The example's list, built by its own headless Dear ImGui and placed by its anchor: its ground, its checker's squares, its own font's ink (its own atlas, its texture) and its gold line from the shared atlas's glyph table, read back from the frame |
| layers | hud < panel < GraceUI's own drawing (the test panel) < modal < notices, each where two overlap; a panel anchored at the bottom right; a 'W' of GraceUI's atlas drawn on a panel, and the same list built against generation 2 refused |
| anchors | After `ResizeBuffers` to 800×450, the bottom-right panel follows its anchor, and the top-left one stays |
| draw list refusals | Through `submit`: an index past its command's vertices, a clip past the panel, nobody's texture, a command's vertices past the list's, 65 commands, a stride of 16, a stride of 2 GiB (D152), no vertices, 13,108 vertices, a stale panel id; each refused, the panel keeping its last list (its own count of lists published unchanged; the session's moves with the example's thread), one log line for the mod |
| late textures | A mod connected after warm-up: a texture ok, 8 MiB more full, an unknown format and rows shorter than the width refused, eight in all, the ninth full; after its first submit too late; its eight uploaded in one upload at the next frame (the upload counter) and drawn |
| request_input | Through ABI 1: a modal granted; another mod refused, naming `harness-mod` and its kind; the holder asking again granted; the wrong kind, a panel that takes no input and another mod's panel refused; hiding the panel ends the holder (released); a hidden panel's request not now (not ready) |
| player's exit | A panel's holder: the first F5 shows "Harness mod has the controls. Press F5 again to take them back.", the second ends it (the player's exit); Escape held 1 s ends the next; neither reaches the game; a release ends the next (released) |
| providers on threads | 3,000 claims of danger and clears from one provider's thread, 3,000 clears from another's and requests for input from a third, ending on danger: no holder is left, the combined claims say danger, and a request is not now (danger) (D145) |
| hidden on a thread | 3,000 shows and hides of a modal on one thread while another asks for input with it, ending hidden: no holder is left (D153) |
| providers | One provider's danger ends the holder and a request is not now (danger); another's game menu, the first clear: two providers, a request not now (game menu); an unknown flag refused; the second disconnected: its claims gone, a request granted |
| panel frames | Over 300 frames with seven panels shown while the example publishes a list every 10 ms from its thread, the GPU idle before each (in up to five windows, D154): every frame draws panels, no allocation and no free on the present thread (nor over the 300 frames run as a game runs them), and GraceUI's own work within 1 ms at p99 |
| disconnect | The harness's mod disconnects: its panels leave the frame, its textures are released after their last frame, the example's stay; its id connects again with a new budget and window |
| two examples | (the examples and examples-reversed scenarios, D156) The example loaded before GraceUI finds it by name and starts the host; the other, loaded after the hooks, finds it at its first look; both connect, the debug view's banner shows its closed text |
| hello-space | (the hello-space scenario, D283, six checks) The fourth example, loaded once the hooks are in, connects and registers its space for both views, and the server starts; F5 opens the menu on its page, its status drawn as the mod built it once watched; → on Step in game, and its sentence says "Add puts 2 on it."; in the browser its status as the game left it, then Add answered and published; a step of 11 refused in the mod's own words and an unconfirmed Reset by GraceUI, then a confirmed Reset; its log's rows, one for each change |
| both notices | The banner and `hello-notice`'s notice are drawn, in the order they connected, and nothing else: the debug view is hidden |
| debug view opened | Injected F11 reaches neither DirectInput nor the game's window; the debug view takes it through `poll`, and its panel is drawn at the bottom left (its ground and its own font's ink, the edges outside it clear), its banner's text replaced in place |
| debug view closed | F11 again: the panel gone, the banner as it was, both notices drawn and nothing else |
| a mod's space | (the pages scenario, D169) A mod registers the demo space for both views (again: duplicate_id; another mod's id, or no view: refused) and GraceUI's server starts on port 8642; the harness, a browser through the server suite's client, takes the cookie from the GraceUI folder's page, a ticket and a WebSocket whose welcome names GraceUI's space and the demo's; a sub reaches the mod as a sub frame and its state reaches the view; a command reaches it with a reply handle (an 8-byte buffer answers full, with the size needed), and its answer the view, once; a setting the browser changes is the menu's too; a file sent by `POST /upload` with its one-use ticket reaches the mod through ABI 1.6's poll (with no room for it, full, naming its size; with room, the frame and the file, once), and the mod's answer comes back as the command's result (G28, D226 to D228); when the mod disconnects the server stops and the view is closed. With `GRACE_HOLD_S` set, the mod stays that many seconds first, answering as the demo space, so that a real browser on this machine can open `http://127.0.0.1:8642/` (graceui.dll's server under Wine, serving the web app the build laid out in its GraceUI folder) |
| the menu | (the menu scenario, D167) F5 opens GraceUI › Menu as the holder of kind menu: its first list laid out on GraceUI's thread and drawn over the game, the page and focus as expected; → raises `ui.scale` by its step and the page is laid out again at the new scale; ↓ → chooses Grace Clear and the frame's colours change; R resets each; with the menu open, the present thread's own work is held to p99 1 ms at 640 by 360 (and measured at 1920 by 1080) with nothing allocated; Esc twice, F5 and danger each end it through the one close routine, F5 then says why it does not open, and no press reaches the game. The Grace pointer (D178) is drawn at the mouse: its gold edge, and no white of Dear ImGui's own arrow. With `GRACE_SHOTS` set to a folder (a Windows path, `Z:\...` under Wine), it keeps two screenshots there as BMP for the owner's look review |
| holder events | (the holder-events scenario, D176) Three mods: one asking with `GRACEUI_INPUT_EVENTS`, one without, one with ABI 1.0's request size. The first gets nothing before its grant, then, in order: a key's press, auto-repeat and release, a character, a surrogate pair joined, `WM_UNICHAR`'s and an IME's result; Ctrl+F2 injected with Ctrl among its modifiers; the pointer in its panel's pixels, three moves coalesced, a click, the wheel both ways and a position outside the panel; none of it reaching the game. The other two get nothing, a poll of ABI 1.0's event size leaves the events for the next of 1.1's, and `INPUT_ENDED` comes alone and last, the events not yet taken discarded, for release, the panel hidden, the player's exit, danger and focus lost. A pad's buttons (`GraceUI_Test_Pad`) come one event a change, by pad, only while the holder takes the pad; the holder's `set_caret` places the IME's composition window, another mod's is ignored, and one on a panel not the mod's or not finite is refused; 300 characters unpolled leave 256 and a dropped count of 44; `pad_keyboard` (ABI 1.10, D276) answers unavailable with no Steam library, opens the stand-in's keyboard at the field's place in the window, refuses another mod, a ninth mode and a field of no size, closes for a null keyboard and at the holder's end on GraceUI's thread, and refuses after it |
| settings kept | (the settings scenario, D179) No file at the start or when the menu opens; → on Menu size and Grace Clear write `user\settings.json` with those two values only; the next process starts from them, its menu's faces baked and laid out at 1.30, the log saying two were kept; R on both writes an empty set back; a file cut short leaves the defaults, with a log line |
| an anti-cheat found later | (the anti-cheat-flag and anti-cheat-recheck scenarios, D183) With a mod's browser space the server runs; a world-state provider's anti-cheat flag, or a module re-check of 200 ms that finds this program's own module by a test name, stops it with its log line; the flag cleared, a second browser space and 60 frames later it is still stopped, and the menu still opens |
| key caps | (every menu scenario, D246) Each check of a key hints line reads the footer's words (`GraceUiTestMenu.footer`), since the keys are drawn as caps apart from their labels; the view's key-cap case checks the caps themselves |
| the links back | (the menu-space scenario, D245) A second mod's space whose Records category holds only pages of a later increment: with GraceUI's own page open, the links back to it show Lanterns and not Records, as its own rail does |
| the menu by pad | (the menu-pad scenario, D242) At 900 by 1080, once F5 has opened the menu: LT walks back and RT on through every page Page Down walks, in the same order; the hints become the pad's ("D-pad ← →  Change    X  Reset    B  Back") with the ring shown; the D-pad's → changes Menu size and X resets it; Y opens search with its pad hints and B closes it; Y's search field opens Steam's floating keyboard once (the stand-in's record: one line, inside the window, on the newest accessor), B closes it, and search opened from the keyboard opens none (D275); View hides the hints and shows them again; the right stick, held down then up, moves the page by several steps and back with focus kept; a key gives the keyboard's hints back; B goes to the rail and B again closes the menu, the game seeing neither B, nor the second while it is held after the close, and a new press after it |
| the menu by mouse | (the menu-mouse scenario, D182) At 1920 by 1080, with the window's own mouse messages: a press on the theme's second segment chooses Grace Clear and focuses its row; a press at 20% of Menu size's column sets the value under it, the held button's move to 60% raises it, and after the release a move changes nothing; a click on Installed mods in the rail opens it; nothing refused, and none of the menu's mouse messages reaches the game's window |
| quiet notices | (the quiet scenario, D181) Two invented mods post notices and the Mods page lists both, switches on, their versions and caps in words; Enter on one's switch in the menu quiets it: its notice goes, the other's stays, nothing refused; what it posts then is answered ok and kept in the page's list (one with a key replacing the one kept under it), never drawn, a bad tone still refused, a withdrawal accepted; GraceUI's own notice (a too-old mod) still shows; `user\settings.json` names the quiet mod's id alone; the next process keeps it quiet (its post kept, the log saying one was kept), and → on its switch shows it again, at once and in the file |
| the italic face | (the display scenario, D241) `fonts\EBGaramond-Italic.ttf` beside graceui.dll, registered by an invented mod as a scaled face of 30 px: its glyph table answers, at least 95 glyphs baked at 20 px (720 lines) |
| the display | (the display scenario, D180) A connect before the swap chain is bound says ui.scale 1.25 and its default and no size, and a 1.0-sized answer is written no further; a scaled face of 16 px is baked at 11 px at 1280 by 720 and a plain one at 16, an unknown face flag refused; one display event after warm-up (1280 by 720, content 0.667, faces 0.667) and none again, none at 1.1's event size; a new ui.scale from the menu and a resize to 1600 by 900 each give one event; a mod connecting later has the size in its answer and the event at its first poll |
| not installed | (the absent scenario, D164) With no GraceUI loaded and no `GraceUI\graceui.dll`, `graceui_sys_find` with `GRACEUI_SYS_STOP_IF_ABSENT` answers null after one look, `installed` 0; without the flag it looks until its time is up |
| a panel drawn in each frame | (the on-present scenario, D259, ten checks) `has_cap("on-present")` and ABI 1.8; a null hook, no room, room over a submission's bounds and a panel not the mod's refused; set after a submit (a texture after it too late), the callback is called in each frame the panel is shown, in order, with its place and a rising time, and its own list drawn in place of the submitted one, a list of its own in every frame with no submit between, KEEP drawing the submitted list, no call while hidden; told its mod's time in the frame before, and the menu-open flag set only while GraceUI's menu is open; a list over its room or clipped outside the panel not drawn, logged once; `hide_panel` from inside a callback refused, logged once; no allocation on the present thread over 300 frames drawn by a callback; cleared, never called again; `disconnect` from another thread during an 80 ms callback returns only after it; 1.2 ms a frame (told about that) suspends the mod after 120 calls, named in one notice, setting again refused (after a reconnect too) and clearing taken; an exception out of one suspends it at once, the render path still on |
| a mod's own hotkey paused | (the hotkey-pause scenario, D280, four checks) `has_cap("hotkey-pause")`, ABI 1.11 and the table's size; another mod's id and id 0 refused; paused, F11 reaches the game by DirectInput and as both its messages, D-pad Up then View reaches it whole, nothing is counted, and the Hotkeys page says "Paused by its mod for now"; resumed, F11 is kept from the game and counted once, and the chord held back and fired once; the hotkey's press paused while held stays hidden to its release and counts once, and the game's press resumed while held stays the game's to its release, by DirectInput and both messages, counting nothing, the next press the hotkey's |
| what pressed a hotkey | (the chords scenario, D243) `has_cap("hotkey-by")` and ABI 1.7; the chord's press says `GRACEUI_HOTKEY_BY_CHORD`, and the second mod's F9, pressed once, `GRACEUI_HOTKEY_BY_KEY` |
| a mod beside a second copy | (the copy-beside-mod and copy-beside-mod-reversed scenarios, D240; reversed, the game's folder's copy is GraceUI's own, `GraceUI\graceui.dll`, whose notice says it idles and names the copy in use, D244) A mod of EldenNet's kind (a panel, a hotkey, an input holder, a world-state provider) on the copy asked first, with a second copy from `beside-copy\` loaded before the game's folder's; in order the game's folder's is asked first, reversed the other is: its panel drawn, its hotkey pressed once, its holder granted and given a key the game never sees; the other copy, asked twice, answers null and idles with no init thread, install run or hook, none to read, named once in GraceUI's own warning by its name relative to the game's folder; then the mod carries on (its panel, its holder given a key, danger ending it with `GRACEUI_CLOSED_DANGER` and a request answered not_now for danger, its hotkey once more), `graceui_sys_find` asked afresh answers the active copy's table, as does graceui-sys's scan past the idle copy, and every hook leads into the active copy's own detours |
| a hook over GraceUI's | (the input scenario, D240) A jump of the harness's own laid over XInputGetState in `xinput1_4.dll`, over GraceUI's: `GraceUI_Test_GetHookLead` says that hook alone leads into `grace-harness.exe`, by one jump, not GraceUI's detour; taken off, every hook leads into GraceUI's own detours again |
| two copies at once | (the copies scenario) Of two copies asked at once from two threads, exactly one answers a table; the other idles, and the active one names it once |
| found by scan | (found-by-scan) With no module named `graceui.dll`, the example finds a copy named `grace-renamed.dll` by its scan, at its first look |
| found by path | (found-by-path) With no copy loaded, the example loads `GraceUI\graceui.dll` from the game's folder |
| anti-cheat hit | (anti-cheat) A module named as the check was told: the notice-only mode by the module clause, input off, the hooks installed as usual, GraceUI's own notice and log line |
| inactive (anti-cheat) | `connect` answers inactive (anti-cheat) with a handle; `notice` works; the other sixteen slots answer inactive; the example connected and posted its notice |
| notices only | The test panel on: the three notices are drawn, GraceUI's own first, and nothing else |
| pure pass-through | F5 and A reach DirectInput and the window; a holder's request answers not_now, and nobody holds input |
| notice-only render path | The render path is still on |
| anti-cheat parent | (anti-cheat-parent) Started by a process named as the check was told: the notice-only mode by the parent clause; `connect` answers inactive (anti-cheat), and `notice` works |
| window followed | (decoy-window) A loader's window of our own, larger than the game's and made before it, is filtered first; once the game's swap chain is bound the filter moves to its window (two windows filtered, one log line), F5 posted to the decoy reaches its own procedure and F5 posted to the game's does not, and a request is granted (D146) |
| atlas fallback | (atlas-full) With the atlas held to 1,024 px (a test build's limit), a mod's faces of 18 and 96 px: the first bake fails, the second leaves the 96 px face out with a log line, the render path stays on, two faces are baked, and the 96 px face's glyph table is refused (D148) |
| first request's wait | (slow-first) With the first request slowed by 3 s (a test build's delay), a request on another thread answers null after 2 s, with one log line; the first answers its table, and so does the next request (D153) |
| long name | (long-name) A second copy two folders of 60 characters of three bytes each deep: named in one notice by the end of its name, after "...", from a character's start (D153) |
| device change | (device-change) The game's swap chain ends and it goes on with a new device on another window: the render path turns off, and the old device's references come back to what they were before GraceUI loaded (D149) |

A frame's cost is the detour's time less the original `Present`'s. Of it, the time inside the runtime's
`ExecuteCommandLists` and `Signal` on GraceUI's own submission is counted apart: on Mesa's software Vulkan a submission
can run the "GPU" work on the calling thread, which on a real GPU is a short enqueue. The rest is GraceUI's own work:
Dear ImGui's frame, the recording and the bookkeeping. The check takes it with the GPU idle before each frame, because
under Wine the GPU is the same few CPU cores, whose rasteriser threads otherwise share them with the detour; the figures
for frames run as a game runs them are printed beside it. Every Direct3D 12 call is also a translation to Vulkan here,
so all of them are upper bounds; S4 measures the cost in the game.

The figures are wall time, and another process, or the host of a virtual machine, can only lengthen them: so each
frame-cost check measures its frames in windows, each as long as its one window was, up to five (three before D154's amendment after 0.2.74), and holds its budget
against the least p99 among them, stopping at the first within it (D154). A cost of GraceUI's own shows in every window,
a busy spell of the machine's in one; every window's median and p99 are printed, and what a window must do besides
(frames drawn, no allocation) holds in every one. Under Wine no clock measures the present thread's own time per frame:
`QueryThreadCycleTime` fails, `GetThreadTimes` counts in 10 ms ticks, and the kernel's per-thread schedstat updates a
running thread's runtime a tick late and sees nothing of a virtual machine's stolen time.

## The second build: MinGW-w64

```sh
cmake -S . -B build-mingw-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
  -DGRACEUI_TEST_EXPORTS=ON
cmake --build build-mingw-test
unset TMPDIR    # as for the test build above
WINEPREFIX=<dir> WINEDEBUG=-all xvfb-run -a -s "-screen 0 1280x720x24" wine64 build-mingw-test/bin/grace-harness.exe
```

It links its runtime statically, so neither binary needs a MinGW DLL. Its harness passes the same checks.

## The safety checks

`tools/check_safety.py` stands in for CI until GraceUI has one (D110, invariant 16): raw pointers from callers,
`reinterpret_cast` and hook code only in grace-host's `abi`, `hooks` and `draw` files and the DLL's `abi` files (and
`graceui-sys`'s headers, a mod's side), each cast with a `// SAFETY:` comment; MinHook, and any other way of hooking a
window (`SetWindowLongPtr`, `SetWindowsHookEx`), only in grace-host's hooks files, Dear ImGui's backends only in
grace-host, and only grace-host linking either: `graceui-sys` and the example mods, under `examples/`, hook nothing;
Dear ImGui's core alone (`gu_imgui_core`, a mod's headless Dear ImGui, D144) linked only by the example mods, its
target compiling no backend;
exports only in the DLL's `abi` files; the hook table only D102's functions, and MinHook making hooks in its one loop;
the pure modules (`grace-input`, `grace-abi`) free of Win32, Dear ImGui and MinHook, their libraries linking nothing
(D124, D131); `graceui.h` only growing against ABI 1.0's first header, `src/grace-abi/baseline/graceui-abi1-0.h`
(D132); and, with `--exports`, a DLL's export table. `--abi-self-test` edits the header seventeen ways in memory and
shows the ABI check refusing each break (a slot dropped, two swapped, one retyped, a member inserted, a constant changed
or gone, the minor falling) and letting each growth through (a slot or member appended, a reserved slot typed, the minor
rising).
