# Building GraceUI

GraceUI's in-game parts are C++20 with CMake and Ninja (D106): `graceui.dll`, the host (D69), and
`tools/grace-harness`, which runs the host's render half without a game. They build for Windows x64 two ways, both on
Linux: with clang-cl and lld-link against Microsoft's CRT and SDK, the release build, and with MinGW-w64. The harness
runs under Wine on Xvfb, drawing through Mesa's software Vulkan (lavapipe). Every build has 0 warnings in our own code,
and the modules `graceui.dll` links keep its rules: no static object with a dynamic initialiser or an exit-time
destructor, checked by Clang.

Dear ImGui 1.93.0 and MinHook 1.3.4 are downloaded at configure time from their own repositories, every file checked
against its SHA-256 (`cmake/third_party.cmake`), into the build tree; `NOTICE` credits them. Configuring needs the
network once per build tree.

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

| Check | Passes when |
|---|---|
| harness | Before GraceUI is loaded, a frame reads back as its clear colour |
| version | `GraceUI_Version` answers 0.1 |
| start | `GraceUI_RequestApi(1)` answers null and starts one init thread, which installs the five hooks once |
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
cast with a `// SAFETY:` comment; MinHook only in grace-host's hooks files, Dear ImGui's backends only in its draw
files, and only grace-host linking either; exports only in the DLL's `abi` file; the hook table only D102's functions;
and, with `--exports`, a DLL's export table.
