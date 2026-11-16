# Building GraceUI

GraceUI's in-game parts are C++20 with CMake and Ninja (D106): `graceui.dll`, the host (D69), and
`tools/grace-harness`, which runs the host without a game, its render half and its input half. They build for Windows
x64 two ways, both on Linux: with clang-cl and lld-link against Microsoft's CRT and SDK, the release build, and with
MinGW-w64. The harness runs under Wine on Xvfb, drawing through Mesa's software Vulkan (lavapipe). The pure modules
(`grace-input`, D124) also build on Linux, with their unit tests. Every build has 0 warnings in our own code, and the
modules `graceui.dll` links keep its rules: no static object with a dynamic initialiser or an exit-time destructor,
checked by Clang.

Dear ImGui 1.93.0 and MinHook 1.3.4 are downloaded at configure time from their own repositories, every file checked
against its SHA-256 (`cmake/third_party.cmake`), into the build tree; `NOTICE` credits them. Configuring needs the
network once per build tree.

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

`grace-input`'s 57 cases cover the holder's answers, the close routine from every exit path, the player's exit and its
timing, the watchdog, the game's view of DirectInput and XInput (untouched, at rest, a closing press hidden until
released, the packet number), the holder's pad view with the opening press, the window-message routing, the surrogate
pairing (lone and swapped halves too) and the ANSI byte joiner, and the hand-overs between threads under
ThreadSanitizer. Wine has no pad, so the pads' masking is checked here only.

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
  fails a release build with any other export.
- The flags: `/EHsc` (the exports' catch-all guards depend on it, D110), `/W4 /WX /permissive- /utf-8`, the static CRT
  (`/MT`) in every configuration, `/Brepro` at compile and link, no incremental link; a release build adds ThinLTO,
  `/Z7 -gline-tables-only` and a PDB beside each binary. Paths in debug information are relative to the source and
  build trees, and each binary names its PDB by file name only (`/pdbaltpath`), so no build machine's path ships.
- `XWIN_ROOT`, a cache variable or in the environment, points elsewhere than `~/.cache/xwin`; the toolchain file
  refuses a splat whose CRT or SDK differs from the lock.

## The test build and the harness

`-DGRACEUI_TEST_EXPORTS=ON` adds the test-only exports (`GraceUI_Test_*`, declared in
`src/grace-abi/include/graceui_test.h`), the counters behind them and the harness. A release build never has them.

```sh
cmake -S . -B build-cl-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release \
  -DGRACEUI_TEST_EXPORTS=ON
cmake --build build-cl-test
python3 -I tools/check_safety.py --exports build-cl-test/bin/graceui.dll --test
export WINEPREFIX=<a prefix of its own> WINEDEBUG=-all
wine64 reg add 'HKCU\Software\Wine\Direct3D' /v renderer /t REG_SZ /d vulkan /f    # once per prefix: see below
xvfb-run -a -s "-screen 0 1280x720x24" wine64 build-cl-test/bin/grace-harness.exe
```

On a new prefix, the very first run once failed with "no window" while Wine was still setting the prefix up; the
next runs passed. Running `xvfb-run -a wine64 wineboot -u` once after making the prefix avoids it. Debian's Wine
packages keep `wine64` in `/usr/lib/wine`: put it on the `PATH` first.

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

It runs four scenarios, each in a process of its own: the render half's checks ("main"); two direct queues; the input
half ("input"), in a process whose game loads three XInput DLLs before GraceUI starts; and the input half on an ANSI
window ("input-ansi"). The game reads its keyboard and mouse through DirectInput and its pads through XInput, as the
game does (EldenNet ER02 §7.2). The input checks inject keys and mouse input with `SendInput`, which Wine hands to
DirectInput through its low-level hook and to the window as messages, and post or send the window messages the others
need. Two quirks of Wine 9.0 on Xvfb shape them: a key injected by its scan code alone reaches the window with no
virtual key, so keys are injected with both; and Wine brings its key state into line with the X server's on X focus and
pointer-grab events (a click, a focus change, the cursor's confinement), releasing a key injected before them, since
the X server never saw it. So the harness keeps the cursor inside the window, lets those events pass before it injects
a key, presses a button before a key, and checks with `GetAsyncKeyState` that a key it expects held is held. Wine has no
pad: `XInputGetState` answers `ERROR_DEVICE_NOT_CONNECTED`, which the checks require unchanged.

| Check | Passes when |
|---|---|
| harness | Before GraceUI is loaded, a frame reads back as its clear colour |
| version | `GraceUI_Version` answers 0.1 |
| start | `GraceUI_RequestApi(1)` answers null and starts one init thread, which installs the hooks in one run: the render half's five, `GetDeviceState` (DirectInput is loaded), and the window procedure's filter, each named in D102's list |
| untouched | With nothing to draw, every pixel of the frame is the clear colour, though the host has bound the swap chain |
| warm-up | The present queue is found (and how is printed), the renderer is built and the atlas uploaded |
| Present | The test panel appears through `Present`: its colour, its text's ink, and the clear colour everywhere else |
| Present1 | The same through `Present1`, drawn in every one of five frames |
| references | Each back buffer's references besides the game's are what they were before GraceUI was loaded |
| ResizeBuffers | `ResizeBuffers` to 800×450 succeeds, the panel is drawn at the new size, references unchanged |
| ResizeBuffers1 | The same with `ResizeBuffers1` to 480×270 and three buffers, naming the present queue for each |
| second request | A second `GraceUI_RequestApi(1)` starts nothing: one init thread, one install run, five hooks |
| second load | Loading the same file again gives the same module, and nothing starts |
| second copy | A copy from another folder is another module; it answers null and idles (D72), and the first draws on |
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
| ANSI characters | (the input-ansi scenario, a fourth process) On an ANSI window, filtered as one: bytes in Windows-1252, a Shift-JIS character in two posted `WM_CHAR`s and one whole in `WM_IME_CHAR`, and `WM_UNICHAR`, reach Dear ImGui as code points, and none reaches the game |

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
`reinterpret_cast` and hook code only in grace-host's `abi`, `hooks` and `draw` files and the DLL's `abi` file, each
cast with a `// SAFETY:` comment; MinHook, and any other way of hooking a window (`SetWindowLongPtr`,
`SetWindowsHookEx`), only in grace-host's hooks files, Dear ImGui's backends only in grace-host, and only grace-host
linking either; exports only in the DLL's `abi` file; the hook table only D102's functions, and MinHook making hooks in
its one loop; the pure modules free of Win32, Dear ImGui and MinHook, and linking nothing (D124); and, with
`--exports`, a DLL's export table.
