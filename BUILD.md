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

`grace-input`'s 64 cases cover the holder's answers, the close routine from every exit path, the player's exit and its
timing, the watchdog, the game's view of DirectInput and XInput (untouched, at rest, a closing press hidden until
released, the packet number), the holder's pad view with the opening press, the window-message routing, the surrogate
pairing (lone and swapped halves too), the ANSI byte joiner and the UTF-8 one (cut short, stray, overlong, a surrogate,
past U+10FFFF), and the hand-overs between threads under ThreadSanitizer. From the host's review (D145): a claim of
danger or input turned off between a request's read and its grant ends the new holder at once, the newest claims
decide whatever order the writers settle in, and no holder outlives a danger claim over 2,000 rounds of two threads.
Wine has no pad, so the pads' masking is checked here only.

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
  fails a release build with any other export. `hello-notice.dll` and `hello-panel.dll`, the example mods, are built
  beside it, the second with Dear ImGui's core alone (`gu_imgui_core`, no backend, D144).
- The flags: `/EHsc` (the exports' catch-all guards depend on it, D110), `/W4 /WX /permissive- /utf-8`, the static CRT
  (`/MT`) in every configuration, `/Brepro` at compile and link, no incremental link; a release build adds ThinLTO,
  `/Z7 -gline-tables-only` and a PDB beside each binary. Paths in debug information are relative to the source and
  build trees, and each binary names its PDB by file name only (`/pdbaltpath`), so no build machine's path ships.
- `XWIN_ROOT`, a cache variable or in the environment, points elsewhere than `~/.cache/xwin`; the toolchain file
  refuses a splat whose CRT or SDK differs from the lock.

## The test build and the harness

`-DGRACEUI_TEST_EXPORTS=ON` adds the test-only exports (`GraceUI_Test_*`, declared in
`src/grace-abi/include/graceui_test.h`), the counters behind them, the harness and `grace-harness-shield.dll`, an
empty DLL of ours that stands in for an anti-cheat's module. A release build never has them.

```sh
cmake -S . -B build-cl-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release \
  -DGRACEUI_TEST_EXPORTS=ON
cmake --build build-cl-test
python3 -I tools/check_safety.py --exports build-cl-test/bin/graceui.dll --test
export WINEPREFIX=<a prefix of its own> WINEDEBUG=-all LC_ALL=C.UTF-8
wine64 reg add 'HKCU\Software\Wine\Direct3D' /v renderer /t REG_SZ /d vulkan /f    # once per prefix: see below
xvfb-run -a -s "-screen 0 1280x720x24" wine64 build-cl-test/bin/grace-harness.exe
```

A run fails with "no window" when a Wine server is still running for the prefix from an earlier `xvfb-run`. That
server stays bound to a display that has gone, so the harness can't open its window. Stop it before every run with
`wineserver -k` (Debian keeps it in `/usr/lib/wine`); with that, three runs in a row of each build passed all 82 checks
(2026-11-16, S10.4.c), and after the host's review every run of both builds passed all 95 but one MinGW-w64 run, whose
"notice allocations" p99 was 1.33 ms while the machine's load average was about 3; three reruns of that scenario
passed. The long-name scenario names a folder in Unicode, which Wine can do only in a UTF-8 locale: hence
`LC_ALL=C.UTF-8` above; without it that scenario fails, saying so. Two other clang-cl runs that day each failed one frame-cost check (a p99 of about 1.55 ms
against 1 ms, with no allocation, its median as usual) while another build kept the machine's four cores busy: under
Wine the "GPU" and the detour share those cores, so rerun such a failure before calling it one. On a new prefix, run `xvfb-run -a wine64 wineboot -u` once first. Debian's Wine packages keep `wine64`
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

It runs sixteen scenarios, each in a process of its own: the render half's checks ("main"); two direct queues; the input
half ("input"), in a process whose game loads three XInput DLLs before GraceUI starts; the input half on an ANSI window
("input-ansi"); ABI 1 ("abi"), whose first request is the example mod's; panels and the rest of S10.4.c ("panels"),
whose warm-up comes after the second example mod and a mod of the harness's own have registered their faces and
textures, since nothing is presented until they have; two copies asked at once ("copies"); the
example finding a copy under another name ("found-by-scan") and GraceUI's own folder ("found-by-path", which runs a copy
of the harness from `bin\by-path`, beside the example and `GraceUI\graceui.dll`, as a game's folder); and a simulated
anti-cheat hit by each clause ("anti-cheat", "anti-cheat-parent"); and five from the host's review: a larger window
found before the game's ("decoy-window"), the shared atlas running out of room ("atlas-full"), a request made while
the first is still running ("slow-first"), a second copy with a long name ("long-name") and the game moving to a new
device ("device-change"). Each scenario whose first `GraceUI_RequestApi`
matters has a process of its own: the anti-cheat check and the mutex are the first request's (D133, D135). A test build
is told to look for names of the harness's own (`GraceUI_Test_SetAntiCheatNames`): `grace-harness-shield.dll`, loaded
before the first request, and `grace-harness.exe`, every scenario's parent; no anti-cheat software, and no file named
after one, is needed. Six scenarios make no window ("copies", "found-by-scan", "found-by-path", "anti-cheat-parent",
"slow-first", "long-name"), so GraceUI's init thread is still looking for one when their process ends, and Wine may then print a "wine client error"
line among the harness's own; it is harmless. Each process ends with a "-- N passed, M failed" line, the main process's
counting each other process as one check.

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
| start | `GraceUI_RequestApi(1)` answers ABI 1's table (its size, minor and version this header's), the anti-cheat check finds nothing, and one init thread starts, which installs the hooks in one run: the render half's five, `GetDeviceState` (DirectInput is loaded), and the window procedure's filter, each named in D102's list |
| untouched | With nothing to draw, every pixel of the frame is the clear colour, though the host has bound the swap chain |
| warm-up | The present queue is found (and how is printed), the atlas is baked on GraceUI's own thread (D147, D148), the renderer is built and the atlas uploaded |
| Present | The test panel appears through `Present`: its colour, its text's ink, and the clear colour everywhere else |
| Present1 | The same through `Present1`, drawn in every one of five frames |
| references | Each back buffer's references besides the game's are what they were before GraceUI was loaded |
| ResizeBuffers | `ResizeBuffers` to 800×450 succeeds, the panel is drawn at the new size, references unchanged |
| ResizeBuffers1 | The same with `ResizeBuffers1` to 480×270 and three buffers, naming the present queue for each |
| second request | A second `GraceUI_RequestApi(1)` answers the same table and starts nothing: one init thread, one install run |
| second load | Loading the same file again gives the same module and table, and nothing starts |
| second copy | A copy from another folder is another module; it answers null twice and idles (D72); the first draws on, and names it once in a notice, drawn, by its name relative to the game's folder (`second-copy\graceui.dll`), never a path (D135) |
| frame cost | With the panel drawn and the GPU idle before each frame, over 600 frames: GraceUI's own work in the detour within 1 ms at p99; the whole detour and the runtime's submission calls are printed too, and the same figures for 600 frames run as a game runs them |
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
| holder frame cost | With a holder fed input and its cursor drawn, the GPU idle before each frame, over 300 frames: GraceUI's own work in the detour within 1 ms at p99 |
| input path | The render path and input are still on after every check |
| render off | Last, the render path turned off as a fault turns it off: input goes off with it, the holder ends (a fault), F5 reaches DirectInput and the window again, and a request is not now (D150) |
| ANSI characters | (the input-ansi scenario, a fourth process) On an ANSI window, filtered as one: bytes in Windows-1252, a Shift-JIS character in two posted `WM_CHAR`s and one whole in `WM_IME_CHAR`, `WM_UNICHAR`, and in UTF-8 as the code page characters of two, three and four bytes and a sequence cut short (U+FFFD), reach Dear ImGui as code points, and none reaches the game |
| example mod | (the abi scenario) `hello-notice.dll`, loaded before GraceUI, finds it through `graceui-sys` by name once it is loaded (at its fourth look or later), connects as it names itself, and its request starts the host |
| example's notice | Its notice is drawn at the top right, and nothing else |
| table | 176 bytes, ABI 1.0, GraceUI's version, every slot set; majors 0, 2 and 0xFFFFFFFF answer null; `has_cap` names `notices`, `panels`, `textures`, `faces`, `input` and `world-state`, and nothing else |
| connect answers | Seventeen requests across versions and ids answer ok, duplicate_id, too_old (naming both versions, with GraceUI's notice naming the mod and one log line) or refused with the reason; a null request, one below its first size, a null answer and one too small are refused, nothing written where it should not be |
| struct sizes | A newer mod's larger request is read as far as this GraceUI knows it, and its larger answer written no further |
| two consumers | Two mods connect in either order, each with its own handle; both notices drawn, in order; both gone once they disconnect |
| disconnect | A handle after its disconnect names nobody, in every slot; the id connects again under a new handle |
| not yet | The six slots still only declared (the bridge protocol's four, `register_hotkey`, `log`) answer not_yet to a connected mod, and all sixteen after `notice` not_connected to a handle of 0 |
| notices | Shown, replaced in place by key, drawn in its tone, withdrawn by an empty text; a timed one goes when its time is up; four a mod, a fifth full; refused when it is not UTF-8, too long, empty without a key, of an unknown tone, null or too small; a long text three lines at most, on the Grace look's 22 px lines (D141); a mod's notices go with it |
| notice allocations | As notices come, change and go over 300 frames after warm-up: no allocation or free on the present thread, and GraceUI's own work within 1 ms at p99 |
| before warm-up | (the panels scenario) `hello-panel.dll`, loaded before GraceUI, connects and registers its face, two textures and its panel; the harness's mod registers a texture and faces of 18 and 24 px, a third face full, a file that is not a font and a size of 2 px refused, the font's whole BMP at 128 px full (over the atlas's room, D148); `glyph_table` answers not_ready |
| panels made | The harness's mod makes 16 panels (EST), the next full, GraceUI's menu layer refused; panels start hidden; a texture after the mod's first submit is too late |
| atlas and faces | Warm-up bakes one atlas, generation 1, with four faces (GraceUI's notices' face from `bin\fonts\` and the mods' three) and uploads the three textures with it, in one upload; face 1's table: over 300 glyphs, 15 px, sorted, 'A' in it; the 24 px face's own size; face 999 refused; a face after warm-up too late |
| example's panel | The example's list, built by its own headless Dear ImGui and placed by its anchor: its ground, its checker's squares, its own font's ink (its own atlas, its texture) and its gold line from the shared atlas's glyph table, read back from the frame |
| layers | hud < panel < GraceUI's own drawing (the test panel) < modal < notices, each where two overlap; a panel anchored at the bottom right; a 'W' of GraceUI's atlas drawn on a panel, and the same list built against generation 2 refused |
| anchors | After `ResizeBuffers` to 800×450, the bottom-right panel follows its anchor, and the top-left one stays |
| draw list refusals | Through `submit`: an index past its command's vertices, a clip past the panel, nobody's texture, a command's vertices past the list's, 65 commands, a stride of 16, a stride of 2 GiB (D152), no vertices, 13,108 vertices, a stale panel id; each refused, the panel keeping its last list, one log line for the mod |
| late textures | A mod connected after warm-up: a texture ok, 8 MiB more full, an unknown format and rows shorter than the width refused, eight in all, the ninth full; after its first submit too late; its eight uploaded in one upload at the next frame (the upload counter) and drawn |
| request_input | Through ABI 1: a modal granted; another mod refused, naming `harness-mod` and its kind; the holder asking again granted; the wrong kind, a panel that takes no input and another mod's panel refused; hiding the panel ends the holder (released); a hidden panel's request not now (not ready) |
| player's exit | A panel's holder: the first F5 shows "Harness mod has the controls. Press F5 again to take them back.", the second ends it (the player's exit); Escape held 1 s ends the next; neither reaches the game; a release ends the next (released) |
| providers on threads | 3,000 claims of danger and clears from one provider's thread, 3,000 clears from another's and requests for input from a third, ending on danger: no holder is left, the combined claims say danger, and a request is not now (danger) (D145) |
| hidden on a thread | 3,000 shows and hides of a modal on one thread while another asks for input with it, ending hidden: no holder is left (D153) |
| providers | One provider's danger ends the holder and a request is not now (danger); another's game menu, the first clear: two providers, a request not now (game menu); an unknown flag refused; the second disconnected: its claims gone, a request granted |
| panel frames | Over 300 frames with seven panels shown while the example publishes a list every 10 ms from its thread, the GPU idle before each: every frame draws panels, no allocation and no free on the present thread (nor over the 300 frames run as a game runs them), and GraceUI's own work within 1 ms at p99 |
| disconnect | The harness's mod disconnects: its panels leave the frame, its textures are released after their last frame, the example's stay; its id connects again with a new budget and window |
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

## The second build: MinGW-w64

```sh
cmake -S . -B build-mingw-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
  -DGRACEUI_TEST_EXPORTS=ON
cmake --build build-mingw-test
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
