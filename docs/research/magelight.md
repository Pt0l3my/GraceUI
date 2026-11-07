# Study: Magelight UI 1.7.1, and what it teaches a GraceUI host in Elden Ring

Status: research note for the GraceUI design (our own words; no code copied). Written 2026-11-07. Cited in GraceUI's
documents as **ML**; its bearing on the open questions (§7) is taken up in [GraceUI DESIGN](../DESIGN.md) §12 and §13.

## 0. Sources, method and pointer convention

- **S:** Magelight UI 1.7.1's source archive (`MagelightUI-1.7.1/`, as published by Severause; no git history), unpacked
  into an isolated folder outside any repository and read as untrusted data. Paths after `S:` are relative to its root.
- **R:** Magelight UI 1.7.1's built release package as distributed to players, unpacked and read the same way
  (`SKSE/Plugins/Magelight/VERSION.txt` says 1.7.1, built 2026-10-31).
- **CL:n** = line n of `S:CHANGELOG.md`. **INV n** = invariant n in `S:CLAUDE.md` (the line range is given the
  first time). EldenNet references use the codes of EldenNet's own documents (DESIGN, ARCH, NPC, ER02), read with its `wp` tool.
- Method: read with `cat`, `sed`, `grep` and `python3 -I` only. Nothing was built, run or copied. About 15% of
  `S:src/Magelight.cpp` (12,117 lines) was read: the ranges cited below. The VR presenter (`S:src/MagelightVR/`,
  `S:docs/VR_PRESENTER.md`) and the Papyrus tier were skipped because Elden Ring has neither.
- The owner linked the `v0.30.1` tag's `gpu/` folder. This copy is 1.7.1, so what it says about v0.30.1 comes from
  its own CHANGELOG and READMEs. The tag itself could not be fetched when this note was written (§1).

## 1. Licences: what could be reused, and what may not be

| Part | Where | Licence | Reuse in GraceUI |
|---|---|---|---|
| Host plugin, public API headers, Papyrus script, examples, docs, tools, review rubrics | `S:LICENSE`, `S:README.md` "Licensing" | MIT, Copyright 2026 Severause | Allowed with the copyright and permission notice (a `NOTICE` entry). Little of it is directly usable: it is C++ for SKSE/CommonLib and Skyrim's engine, and much of it is specific to Ultralight. |
| npm packages `@magelight/sdk`, `/react`, `/vite-plugin`, `create-magelight-view` | `S:packages/*/LICENSE` | MIT, Copyright 2026 Severause | Allowed with notice. These are the most portable part (TypeScript). Default: write our own typed protocol (§3.13). |
| `gpu/` at 1.7.1 (`MagelightGPU.dll`) | `S:gpu/LICENSE`, `S:gpu/README.md:1-13`, `S:gpu/MagelightGpuDriver.cpp:1-29` | **zlib, Copyright 2026 Ultralight, Inc.**, altered (changes marked "MG:") | Allowed if the zlib notice stays and our changes are marked. Its value is limited: it is a **Direct3D 11** driver for Ultralight's GPUDriver interface, and it is useful only if GraceUI uses Ultralight. |
| Compiled shaders the GPU DLL embeds | read from the SDK through an include path (`S:gpu/README.md:15-19`, `S:extern/README.md:103-106`) | **Proprietary to Ultralight, Inc.**, licensed only for use with Ultralight | Never put them in a repository. They may be used only with Ultralight. |
| `gpu/` at **v0.30.1** (the owner's link) | 1.7.1's own account: the Ultralight 1.4 driver "was adapted from AppCore's LGPL-2.1 driver" (`S:gpu/README.md:9-11`, `S:extern/README.md:112-116`); 1.5.0 dropped `license\LGPL-2.1.txt` when it moved to the zlib base (CL:417-419) | **LGPL-2.1-derived** (AppCore), on Ultralight 1.4 | **Do not reuse.** It carries copyleft obligations and is a 1.4-era driver. Not checked against the tag itself. |
| `extern/appcore-ref/` | `S:extern/appcore-ref/LICENSE` | LGPL-2.1 (verbatim AppCore D3D11 driver, kept as history) | Do not reuse. |
| Ultralight runtime (`Magelight1.dll`, `Magelight1Core.dll`, `MgWCore.dll`, `resources/`) | `R:SKSE/Plugins/Magelight/` | Proprietary. The Free License Agreement v2 binds the developer, and the EULA binds end users | Only by taking the Ultralight licence ourselves (§3.19, L21). Magelight's renamed DLLs are covered by permission given to Magelight's author only (`S:docs/DISTRIBUTION.md:79-85`). |
| SDK platform sources (Windows font loader and others) | not in the repository; compiled from the SDK (INV 4, `S:CLAUDE.md:116-126`) | zlib (the font loader), BSD (file system), Apache-2.0 (fallback font header) | Only from the SDK, and only if we adopt Ultralight. |
| DejaVu Sans (the last-resort font) | `S:src/fonts/LICENSE-DejaVu.txt` | Bitstream Vera and Arev licences | If we want a last-resort face, take one from its original publisher (an OFL face fits GraceUI's font rule). |
| Compiled-in libraries | `R:SKSE/Plugins/Magelight/license/NOTICES.txt` §4 | CommonLibVR, {fmt}, spdlog, nlohmann/json, DirectXMath/DXTK (MIT); MinHook (BSD-2); rapidcsv, OpenVR headers (BSD-3) | Not relevant to a Rust host. |
| SeverActions material that Magelight mentions (its harness walk, its stylesheet workaround) | `S:tools/desktop-harness/README.md` | SeverActions has **no licence** | Do not follow or copy it (GraceUI's CLAUDE.md). |

A licence detail the design should know (it refines, and does not dispute, the licence facts in GraceUI's CLAUDE.md). The Free licence's §2.2
grants "Non-Commercial" distribution, and the agreement defines Non-Commercial as academic or other scholarly
research (`R:SKSE/Plugins/Magelight/license/Ultralight-LICENSE.txt` §2.2). Magelight's release checklist therefore
relies on **§2.3, Limited Commercial Distribution**, for a public mod. §2.3 is void for a company or incorporated
entity with US$200,000 or more of trailing-twelve-month turnover or of funding raised. Magelight's licensee is an
individual (`S:RELEASING.md:6-14`). An individual owner of GraceUI would be in the same position.

**Recommendation:** write our own code (GraceUI's default, its CLAUDE.md). No Magelight code is needed in GraceUI v1, so no
`NOTICE` entry is needed yet. If something is reused later, the candidates in order are: the page-side
focus-by-geometry gamepad model (if GraceUI renders HTML in game), the pre-mount buffering idea of the SDK bridge,
and the ABI-order checker idea. Each would be reused with Severause's MIT notice, or rewritten from our own spec.

## 2. Summary of findings

1. Magelight is a full host. It has its own hooks, an embedded WebKit (Ultralight 2.0.0-beta.2 Free), a D3D11 GPU
   driver in a separate DLL with a CPU fallback, a compositor with layers, input capture, a native IME, a gamepad
   layer, host-played sounds, a sandboxed file system and network, a two-verb JS bridge with an npm SDK, three
   ways for mods to drive it (manifest, Papyrus, C ABI), and an append-only, versioned ABI.
2. Its seam worked. SeverActions' large React frontend moved to it almost unchanged (`S:README.md` "Origin"), and
   the host moved from Ultralight 1.4 to 2.0 without its pages changing (CL:236-248).
3. Most of its pain was the web engine. **17 of the 27 invariants** in `S:CLAUDE.md` exist because of
   Ultralight/WebKit, and an 18th (the IME) partly. Each one "cost a debugging arc" (§4).
4. The next biggest source of bugs is **other mods in the frame path**: swapchain wrappers, frame generation,
   upscalers and proxies (CL:613-629, 944-1101).
5. The third is **input leaking to the game or to other mods**: keys, polled hotkeys, the closing press, cursors.
6. For GraceUI in Elden Ring the lessons carry over almost one for one. EldenNet's one hudhook overlay (O25)
   already removes the hook-ownership problems Magelight fought. The open costs are the renderer (GraceUI DESIGN G1), compositing
   a premultiplied BGRA web surface through Dear ImGui, Proton, and pad-first navigation (§5).

## 3. Architecture

### 3.1 Shape and tiers

- **Binaries:** `Magelight.dll` (the host, 2.9 MB), `MagelightGPU.dll` (the GPU driver, 0.6 MB, loaded at run time
  through a C ABI), and the Ultralight runtime under private names: about 50 MB of DLLs (`MgWCore.dll` alone is
  45.6 MB) plus 6.7 MB of ICU data and CA certificates. The whole release is about 59 MiB (`R:` listing). A blank
  25-byte focus-menu SWF is the only Skyrim UI asset.
- **Three ways in for mods:** a data-only `manifest.json` per mod folder, a Papyrus script API, and a versioned C
  ABI for SKSE plugins, with a header-only C++ wrapper (`S:README.md` "Three ways in", `S:api/MagelightUI_API.h`,
  `S:api/MagelightUI_Mod.h`).
- **The host's layers** (`S:CLAUDE.md:14-59`): the hook, threads, views, input, cursor, images and events live in
  `S:src/Magelight.cpp` (one 12,117-line file). There is a v4 mod registry (`S:src/MagelightApi4.cpp`), the export
  tables (`S:src/MagelightApiExport.cpp`), the manifest loader, Papyrus natives, hot reload, fonts, sounds and
  system info. Rules that can be tested apart from the engine live in engine-free headers ending in `Core.h`
  (`InputCore`, `GamepadCore`, `ImeCore`, `EditStateCore`, `TooltipCore`, `HostThemeCore`, `PageThreadCore`,
  `PageStackCore`, `PathTextCore`, `MemoryReportCore`, `JsBridgeCore`), each with a desktop test under `S:tools/`.

### 3.2 Ultralight embedding

- Ultralight is created in `CreateUltralight` (`S:src/Magelight.cpp:6295-6440`). The host installs its own **file
  system** (`set_file_system`, :6352), its own **font loader** (the SDK's Windows loader with a last resort that
  always loads, :6349, `S:src/MagelightFonts.h`) and a **clipboard** (:6354). It sets a `cache_path` per storage
  jar (:6302-6312) and turns the compositor on for every View (:6244-6248, INV 22, `S:CLAUDE.md:325-332`).
- The Ultralight runtime is **delay-loaded and preloaded by full path** from the plugin's own folder, and that
  folder is never on the search path. A missing or unloadable runtime leaves the host loaded but inert. No object
  at namespace scope may touch Ultralight while it is built or destroyed (INV 3, `S:CLAUDE.md:99-115`;
  `PreloadRuntime` at `S:src/Magelight.cpp:4762`).
- **AppCore is not used.** Only the SDK's zlib font-loader source is compiled in, because AppCore 2.0 imports
  CoreMessaging.dll and the WinRT api-sets, which are reported missing under Proton (CL:412-415, INV 4).
- The host's **own file system** has a fixed MIME table (`.mjs` included), UTF-8 paths, a union of page roots and
  a Content-Security-Policy written into every page it serves (INV 5, `S:CLAUDE.md:127-131`).
- It never calls `Recycle(Full)` or `PurgeMemory`, because on Free both drop every page's compiled script. It
  renders at least once a second while nothing is shown, because 2.0 frees GPU resources only inside `Render`
  (INV 22).
- Free-edition limits: every View animates at **at most 60 frames a second** (CL:358-361). There is no structured
  memory API, so memory figures are parsed from a text report (`S:src/MemoryReportCore.h:1-5`).

### 3.3 Threads

- **One thread owns Ultralight** (INV 1, `S:CLAUDE.md:63-93`). Until 1.7.0 that was the game's main thread inside
  the Present hook. Ultralight is created only once the world is up, because main-menu and loading-screen frames
  come from **another thread**, and creating it there binds WebKit's main-thread checks to the wrong thread
  (`S:src/Magelight.h:69-72`, `S:src/Magelight.cpp:8826-8835`).
- **1.3.0:** the frame work moved onto an **8 MB fiber** of the main thread, because the game leaves pages too
  little of its 1 MB stack (CL:440-455). On 2.0 the collector's helper threads then crashed scanning the thread
  between frames. The cure is a VM lock held whenever the thread is off the fiber, reached through **undocumented
  WebCore export names** (`S:src/PageStackCore.h:1-15`, INV 1).
- **1.7.0, the page thread** (INV 27, `S:CLAUDE.md:419-473`; `S:src/PageThreadCore.h:1-8`). Magelight's own thread
  owns Ultralight and records each frame's GPU work into a **D3D11 deferred context**. At Present the game thread
  takes every finished page frame, starts the next one at once, runs the taken command lists in order, and
  composites the newest. The game thread never waits: there is no join, and no lock is held across page script.
  A stuck page freezes the pages, never the game. Locks held across engine calls give themselves back after a
  fault (`OwnedMutex`). This mode makes the fiber, the VM lock and the game-stack zones unnecessary.
- **Other threads:** the window thread (it only queues input; it never calls Ultralight), the engine's input job
  (the gamepad sink), a **game-task pump** (posts made inside Present are handed to SKSE from a host thread,
  because a direct post hung Skyrim VR: CL:1106-1122, `S:src/MagelightGameTask.h:1-13`), a stall watchdog, fault
  draining (`S:src/Magelight.cpp:4440-4510`, 4689), one hot-reload watcher per folder in dev mode
  (`S:src/MagelightDevWatch.h`), Ultralight's own workers, and a worker that decodes cursor images.
- Listener callbacks (JS listeners, DOM ready, console, load failures) fire on the **render thread**. Game work is
  always posted, never done inline (INV 2, `S:CLAUDE.md:94-98`).

### 3.4 The frame

In order (`S:src/Magelight.cpp:8440-8480` and `FrameWork` at :8813): fit fullscreen views to the back buffer;
hibernate idle views; rebuild; materialise pending views; apply resizes; hot reload; inspector requests; apply
pending lifecycle (the only place where views are erased, INV 13, `S:CLAUDE.md:179-181`); install late listener
shims; sync visibility; drain input; drain text-focus reports; tooltips; drain the bridge queues. Then
`Update`, the second visibility sync, prepaint, `RefreshDisplay` and `Render`, the driver draws, and the
composite. Game-side bookkeeping (cursor confinement, the focus menu, cursor re-hiding, the world freeze) stays
on the game thread (`GameBookkeeping`, :8484-).

### 3.5 The GPU driver (`gpu/`) and the CPU fallback

- **The driver** (`S:gpu/README.md:21-71`, `S:gpu/MagelightGpuApi.h:26-128`) binds to the game's device and
  context and creates no device, swapchain or window. Its main changes to the SDK reference driver:
  - it saves and restores the game's **whole D3D11 pipeline state** around every command list, and runs uploads
    and resolves with predication lifted;
  - MSAA is the host's choice;
  - it never binds a draw's source when that texture is the bound render target;
  - capabilities are measured on the game's device;
  - errors are logged once each, with the device-removed reason;
  - draws with a missing target, geometry or program are skipped;
  - it has external textures (mods render actors into page images);
  - stats counters;
  - **contract 3:** recording on a deferred context, split at each Flush command, because some driver stacks lose
    the order of a texture drawn into, sampled and drawn into again.

  `tools/deferred-test` compares the deferred path with the immediate one byte for byte, on hardware and on WARP.
- **Handshake:** the DLL reports its contract number, `sizeof` the engine's GPU state and command structs, and
  the SDK version. The host refuses any mismatch and falls back to the CPU path (CL:680-685).
- **The CPU fallback** is used when the GPU DLL is missing, refused or turned off by `forceCpu`
  (`S:src/Magelight.cpp:148-151`, 4814-4840, 6359-6380). Ultralight's CPU renderer draws into a `BitmapSurface`.
  When the surface's dirty bounds are non-empty, the host maps a dynamic texture write-discard and copies **the
  whole bitmap**, row by row (`UploadSurfaceIfDirty`, :7582-7609). Texture images are GPU-only (`S:api/
  MagelightUI_API.h:83-88`). The docs say only that the CPU path "costs frame time"
  (`S:docs/TROUBLESHOOTING.md:149-158`). It was never measured in this copy.
- **Pixel format:** Ultralight surfaces are **premultiplied BGRA**, blended ONE / INV_SRC_ALPHA
  (`S:src/Magelight.cpp:6208-6215`). The bytes are sRGB-encoded and sampled through a UNORM view
  (`S:api/MagelightUI_API.h:83-86`). The VR copy pass divides out the premultiplication (:5413-5427). The
  compositor's own small shaders are compiled at run time (:6138-6170).

### 3.6 Compositing and the view stack

- **Layers:** `hud` < `panel` < `popup` < `system`, with an explicit order inside a layer (`SetViewOrder`; "raise"
  means the top of the layer). `hud` views never take UI mode. Click-through views never get input
  (`S:docs/MANIFEST.md:64-70`, `S:src/Magelight.h:83-87,162`).
- **Hit testing:** the topmost visible view under the cursor that is not click-through (`S:src/Magelight.h:143-145`).
- **Per view:** geometry anchored to a corner, or fullscreen tracking the back buffer (the page scales itself from
  the viewport); a **cutout** rectangle the compositor discards so the game shows through (`:223-225`);
  **hibernation**, which releases a view's texture after it has been hidden for a while and reloads it on show
  with queued calls replayed (`:227-231`); **prepaint**, which paints a hidden page once after load and again when
  it settles so the first open is not one long frame (CL:536-550, 254-256); **load on show**; a staggered first
  load of one page per frame (CL:855-880); device scale clamped to 1.0..3.0 (CL:1057-1062).
- **A presenter seam:** one list of views per frame, read once by both the flat compositor and the VR overlays
  (`S:src/MagelightPresenter.h:1-14`).
- **Host pages:** a version-gate notice listing mods that need a newer host (`S:views/gate/`,
  `S:src/Magelight.h:220-221`), the VR keyboard (`S:views/keyboard/`), and the Web Inspector hosted as a
  system-layer view (dev download only).

### 3.7 Where it draws, and other mods in the frame path

- **Composite targets** (`ChooseCompositeTarget`, `S:src/Magelight.cpp:8711`):
  - **Present**, through a vtable hook on the game swapchain's Present plus Present1;
  - **late**, inside dxgi's own Present through a MinHook detour, so it draws after other hooks;
  - **the UI pass**, where an invisible engine menu draws pages into the game's UI render target, so frame
    generation and upscalers carry them;
  - **engine mode**, which draws with the engine's own device from its end-of-frame call (CL:985-997, 1064-1092,
    613-629).
- **Hook installation** checks that a swapchain's extra vtable slots are code before patching them
  (`S:src/Magelight.cpp:10207-10221`). A ResizeBuffers hook logs resizes and failures (:9691-9701). The device is
  checked for removal after frames (:9827).
- The log names the present layers it recognises: NVIDIA's present layer (Smooth Motion), Streamline, a
  frame-generation proxy and an upscaler (CL:1010-1014).

### 3.8 Focus, UI mode and input capture

- **UI mode** is the interactive state: cursor up, game controls suspended, keyboard to the page, at most one view
  across all mods. Entry and exit run on the game thread (`SetUIMode`, `S:src/Magelight.cpp:3765`; INV 14,
  `S:CLAUDE.md:182-183`). A second requester is told Busy, and the holder hears FocusDenied. Requests can be
  queued, cancelled or reclaimed (CL:500-520, `S:docs/TROUBLESHOOTING.md:143-147`). The exit callback fires on
  **every** exit path (programmatic, Escape, toggle key, load boundary, render death), and UI mode is forced off
  at every load boundary (`S:api/MagelightUI_API.h:49-52`, `S:src/Magelight.h:65-67`).
- **Keyboard containment** (INV 17, `S:CLAUDE.md:260-290`). While UI mode is on, the engine keyboard device's poll
  is skipped (`Hook_KeyboardPoll`, :11069), so no key event exists anywhere in the engine. Since 0.28.0 its
  buffered DirectInput queue is drained as well (CL:1371-1374). Typing, Escape and hotkeys come from the window
  procedure (`WM_KEYDOWN`/`WM_CHAR`), through a window subclass with an ANSI shim (:10255-10275). Other plugins
  that **poll** `GetAsyncKeyState`/`GetKeyState` see keys as up while a page holds UI mode (the polled-keys block
  at :2728; CL:553-567).
- **Mouse:** it comes from the engine's input sink and its menu cursor; the window never gets WM mouse messages
  (INV 6, `S:CLAUDE.md:132-135`). Each pointer source keeps its own press and hover, and a held button's moves and
  release go to the view it was pressed on (INV 24, `S:CLAUDE.md:350-371`). The input queue coalesces moves but
  never drops releases or focus changes (INV 27; `S:src/InputCore.h`).
- **Cursor:** the host's own cursor is rasterised in code from signed distances, crisp at any size, with a hover
  glow and an I-beam, and a page can tint it (`S:src/MagelightCursorArt.h:1-12`). The game's cursor is hidden
  inside its own menu update, never from another thread (CL:218-227, `S:src/Magelight.cpp:3000-3107`). The OS
  cursor is confined to the window while a page holds the mouse, and the previous clip is restored on exit or
  focus loss (CL:229-235, :3386). Since 0.30.6 the OS cursor's visibility is never forced either way (CL:946-955).
- **Escape** closes the page unless the page captures it (`SetEscapeCapture`, CL:1360-1365). While an IME is
  composing, Escape belongs to the IME (INV 26).
- **Hotkeys** never open a page over an engine menu, the console or game text entry. While a page has key focus,
  only the active view's own binding fires, and only to close it (INV 15, `S:CLAUDE.md:211-216`). One binding per
  key; the first binder keeps it (`S:README.md` "Quick start"). Keys can be written as names or scancodes, and a
  Windows VK code written by mistake gets a warning (CL:779-785, `S:src/HotkeyNames.h`).
- **Releases and holds:** leaving UI mode, or Alt+Tab, sends the page key-ups for every button and modifier it saw
  held (CL:303-306). A closing press that is still held keeps the game's controls off until it is released (INV 7,
  `S:CLAUDE.md:136-155`). **Lost keys:** on some PCs the window gets no key messages, so keys seen through
  DirectInput are passed to the page as a fallback, and the log says where keyboard focus was (CL:102-105,
  `S:src/Magelight.cpp:4235`).

### 3.9 IME

- OS IME windows do not draw over the game, so players picked candidates **blind** (CL:263-270). Since 1.5.0
  Magelight asks Windows for no IME window. The composition goes into the focused field through Ultralight's
  editor (compositionstart/update/end, clauses underlined). The candidate list is read from Windows
  (`S:src/ImeCore.h:1-8`) and handed to the page on a reserved channel, where the page core draws a box at the
  composition. Pages can opt out, restyle it or draw their own (INV 26, `S:CLAUDE.md:399-418`; CL:322-331).
- The rules: no IME in password or number fields (Ultralight reports a number field as text, so the host asks the
  page); the composition is finished before focus is taken away; Escape while composing is the IME's; the window
  thread only queues work (`S:src/EditStateCore.h:1-15`).

### 3.10 Gamepad (1.6.0)

- Opt-in per view, flat only (`S:docs/SDK.md:171-233`, `S:src/GamepadCore.h:1-4`). The **keys** style types the pad
  as Enter, Escape and the arrows, so a keyboard-ready page works unchanged. The **navigation** style hands the page
  actions through a page helper that moves focus by **geometry** inside attached **scopes**. A modal's scope takes
  the selection and gives it back when it closes. The shoulders step through tab buttons. Range and select
  controls are edited in place. Prompts carry glyphs for Xbox, PlayStation or generic pads, and an optional stick
  cursor is available (`S:src/GamepadScript.h:1-12`).
- **Ledger rules:** the press that opened a page does nothing in it until it is released; the B that closes a page
  never reaches the game; a release goes where its press went. The sink is placed **ahead** of the game's own
  input handlers (INV 17; CL:182-193).

### 3.11 Sounds

Ultralight has **no media stack**: `<audio>`, Web Audio and video do nothing. Pages ask the host by name, and the
host plays the game's own UI sound records through the game's audio, which follows the player's volume
settings. File playback is refused on purpose, as an attack surface (CL:1297-1316, `S:src/MagelightSound.h:1-12`).

### 3.12 Fonts, text and paths

- WebCore dereferenced the last-resort font without a check. A PC where Arial could not load crashed inside a
  frame and switched the UI off for the session. Magelight now checks a chain of system fonts at startup and
  falls back to the DejaVu Sans it carries (about 740 KB) (INV 19, `S:CLAUDE.md:301-310`; CL:754-776). It also
  reads font files under non-ASCII paths itself (CL:764-767) and keeps the faces of Japanese font collections
  (CL:257-258).
- **Every path is UTF-8.** The standard library's narrow conversions use the ANSI code page and threw under a
  folder whose name is in Chinese characters (CL:153-160, `S:src/PathTextCore.h:1-5`). A Windows user name the ANSI code page cannot
  spell ended the game at load (CL:670-672).
- Text gamma is 2.2 on 2.0, where 1.3.0 used 1.8. Hinting and gamma are player settings (CL:244-248).

### 3.13 The JS bridge and the SDK

- **The page contract** (`S:docs/SDK.md:14-122`). Before any page script runs, the host installs a read-only info
  object (version, view and mod ids, capabilities, dev flag) and **two verbs**: one sends on a named channel, one
  subscribes to a channel (plus helpers for unsubscribing, pending messages, sound and theme). Host-to-page
  messages are buffered until someone subscribes, so a late mount loses nothing. Objects travel as JSON text.
  Channel names starting with `__` are the host's own: UI mode, tooltip, IME, gamepad, theme, prepaint, sound.
- **Native side** (`S:src/Magelight.h:88-99,131-139,188-189`; `S:src/JsBridgeCore.h:1-8`). Payloads are passed as
  real string arguments, never pasted into script source. Any string a mod supplies that goes into script is
  JSON-quoted. Script errors are caught and reported with their stack (CL:389-398, INV 9 `S:CLAUDE.md:157-169`).
  Only host-written scripts are named, because naming a mod's script broke relative `import()`.
- **SDK** (`S:packages/sdk/src/*.ts`): host detection; typed channels; a payload parser that never throws on a
  payload; a **browser mock with a floating panel** to inject payloads and see what the page sent
  (`mock.ts:1-8`); React hooks; a Vite plugin that makes `file://`-safe output (es2022, `base './'`, no
  `crossorigin`, the module-preload polyfill off, one folder per entry); and a scaffolder.

### 3.14 Manifest

`S:docs/MANIFEST.md:9-103`. The folder name is the mod's identity, and `modId` must equal it. The manifest gives a
minimum host version, a storage "session", a network level, and named views (path, layer, anchor, geometry,
fullscreen, click-through, start visible, hotkey, pause, prepaint, main menu, load on show, cursor, hibernation,
gamepad, sounds, dev reload). Unknown keys are logged with a did-you-mean, and a wrongly typed value is logged
and replaced by the default. A bad view never stops the other views or other mods.

### 3.15 Versioning and ABI

- Versions are `MAJOR.MINOR.PATCH`. The packed host number is `MAJOR*10000 + MINOR*100 + PATCH`, and an
  "added in" table tells mod authors what to put in `minHost` (CL:1-64). Features are also queried **by name**.
- The C ABI is versioned structs of function pointers served by one exported factory. Structs are append-only,
  enums never renumber, and a breaking change means a new DLL name (`S:api/MagelightUI_API.h:1-16`). The order of
  the export tables is checked against the struct member order in CI (`S:tools/check_abi.py:1-15`, INV 10,
  `S:.github/workflows/ci.yml:26-30`).
- The GPU DLL has its own contract number and struct-size handshake (§3.5). A runtime from another Ultralight
  version leaves the host inert instead of crashing (CL:729-731). Line one of the log and a `VERSION.txt` name the
  running version. Mod managers that mix copies are documented (`S:docs/DISTRIBUTION.md:48-63`).

### 3.16 The sandbox

`S:SECURITY.md:7-54`; CL:1135-1240, 703-717.

- **Network** is file-only by default. Loopback (`localhost` and `127.0.0.1` over http(s) and ws(s)) is opt-in per
  mod, and the internet only from a mod's DLL. Each opt-in is logged.
- Since 0.31.5 the policy is a **CSP written into every page**, plus a block on navigating the main frame anywhere
  but the view's own HTML files (`CspFor`, `S:src/Magelight.cpp:5042`). The request hook is only the second line,
  because Ultralight 1.4 never showed it synchronous XHR, WebSockets or `file:` reads.
- **Files:** pages read the union of the page roots. Ultralight does not say which page asks, so one mod's page
  can read another's files.
- **Storage:** a per-mod jar per install, never in the save.
- **Script ownership** rules apply between Papyrus scripts and DLLs.
- **Page identity:** the bridge function is bound to its own view (CL:1212-1220).
- The inspector is sandboxed like a page (CL:1221-1230).
- Slugs are validated, Windows device names included (CL:1438-1440).

### 3.17 Diagnostics and logs

- At startup the log records the game, SKSE, Windows **or the Wine version under Proton**, the CPU, RAM and GPUs.
  Under Wine the driver version is DXVK's stand-in, and the log says so (`S:src/MagelightSysInfo.cpp:79-82,
  193-216`).
- Paths are shown with `%USERPROFILE%` in place of the user folder (CL:668-669). **Page console logging is
  opt-in**, because a page that logged its settings put the start of an API key into logs players post. Lines are
  cut at 2 KB and limited to 20 a second per view (CL:837-853).
- The **vectored exception handler never logs**. It only records the fault and wakes the watchdog thread, which
  writes the lines (INV 21, `S:CLAUDE.md:320-324`; CL:468-476).
- A stall watchdog samples thread stacks. A fault inside frame work turns the overlay off for the session but
  keeps the game running (`S:docs/TROUBLESHOOTING.md:459-475`). Every failed start step is logged with its
  HRESULT, and a "render dead" reason travels with events and errors and is shown once on the HUD (CL:1002-1009).
- Memory lines are parsed from Ultralight's text report (`S:src/MemoryReportCore.h`).

### 3.18 The desktop harness and offline tests

- `S:tools/desktop-harness/README.md`: `harness.exe` runs **Magelight's own GPU driver** on a desktop D3D11
  swapchain with the host's frame pump. `probe_appcore.exe` runs Ultralight's reference driver as the control
  ("is it our driver or the engine?"). An injected script makes live CSS bisects possible, and repro pages
  include the engine's border defect.
- About twenty engine-free or desktop tests (`S:CLAUDE.md:38-56`): fonts, theme contrast, tooltips, memory report
  parsing, the bridge, visibility, paths, input, IME, editor, gamepad, the fiber gate, the deferred driver and the
  page-thread handoff. Each is to be **re-run at every SDK bump**.
- **There is no automated in-game test suite** (`S:CONTRIBUTING.md` "Testing a change"). In-game kits are zipped
  mods (`S:tools/stack-test/`, `S:tools/load-test/`). Several shipped features say "not yet run in game"
  (CL:810-836, 837-853, 855-880).

### 3.19 Packaging, and the Ultralight notices it ships

- The release is **plain zips**, not a FOMOD: the runtime, DevTools (inspector and dev pages), Examples and
  symbols (CL:1141-1147). `tools/package.ps1` builds them. The project rule says never use `Compress-Archive`,
  because it writes `\` into entry names (`S:CLAUDE.md:194-195`). `tools/check_stage.ps1` lints the stage: private
  runtime names patched everywhere, required files, matching versions. It also loads the host **with the runtime
  out of reach** and fails unless the host loads, registers a view and exits cleanly (INV 3).
- The **SDK is pinned** by version and SHA-256, and archived for when Ultralight stops serving the build. The pin
  is a **beta** (2.0.0-beta.2) (`S:extern/README.md:76-83`, `S:CONTRIBUTING.md` "The Ultralight SDK pin").
- **Notices shipped** (`R:SKSE/Plugins/Magelight/license/`): `EULA.txt` (Ultralight's end-user terms),
  `Ultralight-LICENSE.txt` (the Free License Agreement v2), `Ultralight-NOTICES.md` (WebKit LGPL 2.1/BSD, ICU,
  FreeType, HarfBuzz, skia, SQLite, libpng, libjpeg-turbo, zlib and others), Magelight's `LICENSE.txt`,
  `NOTICES.txt` and `THIRD_PARTY_LICENSES.txt`. `NOTICES.txt` also points to Ultralight's LGPL WebCore changes in
  Ultralight's public repository. The README and mod page carry Ultralight's copyright and trademark line and the
  legend pointing to NOTICES.md (`S:README.md` last lines; `S:docs/DISTRIBUTION.md:65-95`).
- The three runtime DLLs are renamed, and the module-name strings inside them rewritten to the same length, with
  **Ultralight's written permission to Magelight's author**. Bundlers must not rename or patch them themselves
  (`S:README.md` "Licensing").

### 3.20 Figures it reports

- Compositor on (2.0) against off (1.4): SeverActions' idle main page uses 0.04 against 0.10 ms of CPU a frame,
  and its quick-wheel hovers 0.08 against 0.25 ms of GPU, for about 70 MB more video memory across five pages
  (CL:244-248). Render targets are 113 against 43 MB on a 2560x1440 main page
  (`S:docs/TROUBLESHOOTING.md:247-256`).
- A first open without prepaint takes one long frame: 192 ms for one dashboard, 86 ms for another page
  (CL:536-541). Image decode on first open costs 29 ms with 5 images and 86 ms with 19; prepaint brings it to
  0.3 ms (CL:254-256). A prepainted texture costs about 74 MB at 2560x1440 with MSAA 4x (CL:541-544).
- Sixteen pages loading at once cost about 0.75 s of frames, the worst one 96 ms (CL:855-860).
- An idle page-thread frame with the VM lock held takes about 13 µs, against 235 µs without (INV 27).
- Frame rate: the page thread shows each page frame one game frame late, and the cursor does not lag (CL:89-99).

## 4. What worked

- **A narrow seam.** Two globals and channels, with the SDK optional. A large React app moved over unchanged, and
  the engine changed underneath it (`S:README.md` "Origin"; CL:244).
- **Moving the engine off the game's thread** (1.7.0). Heavy script costs the pages' frame rate, not the game's.
  The fiber, VM-lock and stack-zone workarounds became unnecessary. Frame generation stopped blacking out page
  rows (CL:89-101).
- **Failing closed and saying why.** An inert host when the runtime is missing or mismatched, a CPU fallback for a
  bad GPU DLL, "render dead" with a reason, a HUD notice, and the overlay off while the game keeps running
  (CL:998-1012, 729-731, 680-685).
- **Engine-free cores with desktop tests**, re-run at every SDK bump, and a desktop harness with a reference
  control (§3.18).
- **Written invariants with their reasons**, plus review rubrics per area (`S:CLAUDE.md:61-473`,
  `S:.claude/agents/`).
- **Security tightened before 1.0.** File-only by default, CSP in every page, page identity, an inspector sandbox
  (CL:1135-1240).
- **Privacy in logs.** Masked user folders, console logging opt-in, rate limits (CL:668, 837).
- **Pad support built as a model**, not key remapping: scopes, geometry, a ledger (§3.10).
- **Drawing what the OS would not.** The IME list, tooltips and the cursor, all drawn in the page or by the host
  (§3.8, §3.9; CL:456-467).

## 5. What its CHANGELOG shows went wrong

The CHANGELOG covers 37 releases, from 0.26.8 to 1.7.1. Grouped by cause:

- **A. Other mods in the frame path.**
  - A Present1 vtable write past a short wrapper vtable broke the device query, so no UI appeared (CL:969-984).
  - Asking a wrapper for its device on every frame, and releasing a reference it never gave, freed the game's
    device: a crash at startup or about 8 minutes in (CL:1043-1051).
  - A throwaway device created behind ENB killed the game with no crash log (CL:1052-1056).
  - Frame generation dropped what was drawn at Present (CL:1015-1022) or turned page rows black (CL:100-101).
  - Mixing a wrapper device with engine objects under NVIDIA Smooth Motion removed the device, and the game froze
    (CL:985-997).
  - A wrapper presented a different buffer: **a hotkey paused the game and nothing appeared** (CL:613-629).
  - Plugins that subclassed the window in ANSI crashed against Magelight's Unicode subclass (CL:1069-1075).
  - The game's pipeline state was left changed after a Magelight frame (CL:718-724).
- **B. Threads and hangs.**
  - Posting game tasks from inside Present hung Skyrim VR on large saves (CL:1106-1122).
  - A synchronous load callback took a non-recursive mutex, which killed the UI for the session (INV 16,
    `S:CLAUDE.md:218-235`).
  - Text focus reports arrive inside whatever engine call caused them (INV 25, `S:CLAUDE.md:372-398`).
- **C. The JS stack inside the game.**
  - A page's recursion overflowed the game's 1 MB stack, and the overlay went off (CL:440-449).
  - 2.0's collector crashed scanning the fiber, and the first uncaught error became a stack overflow under a
    crash logger's stack guarantee (CL:272-278). The fixes go through undocumented engine exports (INV 1).
- **D. Input leaking.**
  - Typed keys replayed into the game after close (CL:1371-1374).
  - A dispatcher-hooking mod opened its UI on a Backspace typed into a search field (INV 17).
  - Another mod's polled letter hotkey opened a chat window while the player typed (CL:553-567).
  - A closing press reached the game (CL:739-751, CL:182-193).
  - Suspending controls made the game sheathe weapons and drop spells (CL:285-287).
  - The keyboard was ignored until Alt+Tab on some PCs (CL:102-105).
  - Modifiers were lost because the engine reads keyboard state at event construction (INV 24).
- **E. Cursors.**
  - Two cursors at once (CL:1023-1027).
  - The game's cursor flashed over a page whenever another menu reopened it (CL:480-486).
  - A second Windows pointer remained after close, because the OS cursor was forced on instead of restored
    (CL:946-955).
  - A cursor mod crashed when the cursor menu was rebuilt or touched from another thread (CL:218-231, 288-292).
  - Without confinement, clicks landed on another monitor (CL:229-235).
- **F. Text, fonts and paths.** The last-resort font crash (CL:754-763), non-ASCII font paths (CL:764-767),
  non-ASCII install folders (CL:153-160) and user names (CL:670-672), and Japanese collection faces (CL:257-258).
- **G. Sandbox.**
  - Before 0.30.0, any manifest page could reach every local service; a page could act as another view; the
    inspector could read every served file (CL:1150-1230).
  - The request hook never saw sync XHR, WebSockets or `file:` reads (CL:703-717).
  - A late listener rebuilt the page's bridge object and silently cut subscriptions (CL:1231-1240).
- **H. Privacy.** A page's `console.log` put the start of an API key into a log players post (CL:837-853). The log
  named the Windows user (CL:668).
- **I. Load and paint costs.** Long first-open frames and loads that bunched up (§3.20). rAF ran at 40 a second on
  a 60 Hz game, through timer aliasing (CL:725-728). Hidden pages repainted every frame until only visible views
  were rendered (CL:1318-1323).
- **J. Engine churn (1.4 to 2.0).** The 60 fps cap (CL:358-361); one-pixel rounding at fractional scales
  (CL:396-401); scale below 1 drawing only part of a page (CL:1057-1062); images decoded only at paint
  (CL:254-256); the AppCore imports missing on Proton (CL:412-415); a whole class of re-verification at every SDK
  bump (§3.18).
- **K. Mod managers.** Two copies mixed file by file. The fixes were version checks that leave the host inert and
  documentation (`S:docs/DISTRIBUTION.md:48-63`).

## 6. Lessons for a GraceUI host in Elden Ring (Direct3D 12, one hudhook overlay, Proton)

Tags: **[any]** applies whatever the renderer; **[web]** applies to any HTML engine in game; **[UL]** applies to
Ultralight only.

1. **[any] Keep the UI behind one versioned, renderer-agnostic bridge.** Magelight's seam carried a large app
   unchanged (§4). GraceUI should use one typed protocol for the browser host (a local server) and the in-game
   host, plus a browser mock with an injection panel for UI work against a fake EldenNet (`S:packages/sdk/src/
   mock.ts`). GraceUI's planned harness (GraceUI DESIGN §9) is this, and Magelight shows it is what makes UI development fast.
2. **[web] Budget for the engine, not the pages.** 17 of 27 invariants are engine-specific (§2). For GraceUI DESIGN G1 that is
   the strongest evidence that an embedded HTML engine is a standing maintenance cost, re-tested at every engine
   update. A Dear ImGui-native Grace theme has none of those 17.
3. **[web] Give the engine its own thread from the first day, with a large stack.** Never run it in hudhook's
   Present callback. Magelight's path (game stack, then fiber, then VM lock through undocumented exports, then a
   page thread) is the one to skip. The engine is created on that thread and never touched elsewhere, because
   WebKit binds its main thread at creation (`S:src/Magelight.h:69-72`). The Present callback takes the newest
   finished frame, never waits and holds no lock across page script (INV 27). In ER it is not yet known which
   thread presents during loading screens and the title screen, which is one more reason not to tie the engine
   to it.
4. **[UL] Start with the CPU-bitmap mode; treat a D3D12 GPU driver as a later spike.** Magelight's D3D11 driver
   needed state save and restore, predication, MSAA, capability measurement, deferred-context splitting and a
   struct-size handshake (§3.5). No D3D12 driver exists in this copy, so it would be ours. It would also have to
   share hudhook's command list, queue and descriptor heap, which hudhook 0.9.3 may not expose (`dx12` only,
   `hudhook::imgui` only: O25). In CPU mode, **upload only the dirty rectangles**: Magelight copies the whole
   bitmap on any change (`S:src/Magelight.cpp:7582-7609`). A 2560x1440 BGRA page is 14.7 MB per full copy, and
   the Steam Deck's CPU and memory bandwidth are the constraint to measure.
5. **[UL] Composite the premultiplied BGRA surface correctly through ImGui.** Ultralight's pixels are premultiplied
   BGRA, sRGB-encoded behind a UNORM view (`S:src/Magelight.cpp:6208-6215`; `S:api/MagelightUI_API.h:83-86`).
   Dear ImGui blends straight alpha. GraceUI can swizzle and un-premultiply on the page thread before upload, or
   use a premultiplied blend for that one draw if hudhook's DX12 renderer allows it (S4 to check). In HDR the
   texture must go through the same linearise-and-paper-white path as ImGui colours (NPC §10.7). Magelight has no
   HDR handling at all (Skyrim is SDR; none found in `S:src`), so there is nothing to borrow.
6. **[any] Add no hook, but expect the same frame-path hazards.** O25's single hudhook hook removes Magelight's
   hook-ownership bugs (A in §5). Its other failure classes exist in ER too: frame generation, upscalers, ReShade,
   driver frame generation and other overlays (ER02 §7.1). For GraceUI this means:
   - keep EldenNet's rule that input capture is released **after 2 s without an overlay render** (DESIGN §8.1),
     because "the game is captured and nothing shows" is Magelight's worst field report (CL:613-629);
   - log the present layers recognised, by module name only, at startup (CL:1010-1014);
   - add frame-generation mods to the compatibility matrix.

   EROverlay's move to the in-engine UI path (ER02 §7.1) matches Magelight's UI-pass cure. It is a later option,
   not v1.
7. **[any] Fail closed and say why, once.**
   - Check the device for removal.
   - On any UI fault: turn the menu off for the session, release capture, show one toast, keep dialogue running.
   - Leave nothing paused or captured under a UI that cannot draw (CL:998-1012;
     `S:docs/TROUBLESHOOTING.md:459-475`).
   - Rust analogue: catch panics around the UI frame, and keep the reason for the bug-report page.
8. **[any] Input capture.** DESIGN §8.1 already zeroes DirectInput state, makes XInput neutral, masks keys still
   held at close and releases on Esc, focus loss, danger, loading and faults. Magelight adds four points:
   - also neutralise **buffered** DirectInput reads if ER uses them, or typed keys may replay after close
     (CL:1371). S4 should check.
   - **Other mods' polled hotkeys** fire on typed letters (CL:553). Magelight hooks user32 process-wide for
     foreign DLLs. That is invasive and conflicts with ER02 §7.4, so document it and record an Open question
     rather than copy it.
   - In ER, **Esc opens the game menu and B/Circle dodges**. The closing press must never reach the game, which
     is the existing mask-until-release rule. Test it on pad and keyboard.
   - Add a **lost-keys fallback** and log where keyboard focus is (CL:102-105). Alt+Tab or close sends the UI
     key-ups for everything it saw held.
9. **[any] The cursor stays on the overlay.** ImGui draws the cursor (DESIGN §8.1), so it never lags a 60 fps or
   slower page (INV 27). Confine the OS cursor while the menu holds the mouse and restore the previous clip on
   close and on focus loss (CL:229-235). Never force the OS cursor's visibility; put back what was there
   (CL:946-955).
10. **[any] One close routine for every exit path.** F5 again, Esc or B, danger, loading screens, death, quit to
    title, fast travel, Alt+Tab, render death and core faults all use it, and it keeps changes (GraceUI DESIGN §5.1; compare
    `S:api/MagelightUI_API.h:49-52`).
11. **[any] Hotkey rules.** Never open over the game's menus or text entry, or outside the safe moments EldenNet
    already defines (DESIGN §8.1). While a text field has focus, only the menu's own key acts, and only to close.
    Store keys by name, warn when a VK code was given where a scancode was meant (CL:779-785), keep one binding
    per key, and let S4 confirm F5 (GraceUI DESIGN §5.1).
12. **[any] Pad-first navigation is its own design job.** Plan a focus model (sidebar ↔ content, modal scopes that
    restore the selection, shoulders for tabs, edit-in-place for sliders and selects) and glyph prompts for Xbox,
    PlayStation and Steam Deck. Two ledger rules apply: the press that opened the menu does nothing until released,
    and B never reaches the game (§3.10). For text on a pad, use the Steam floating keyboard (DESIGN §8.1,
    NPC §5.6), because Magelight's VR keyboards have no flat-screen equivalent.
13. **[any] The IME.** OS candidate windows may not show over a fullscreen game (CL:263-270). In v1, Escape while
    composing belongs to the IME and never closes the menu (INV 26). Draw the composition in the field and the
    candidate list ourselves as a later increment. Under Proton the IME path is unproven; the Steam keyboard is the
    Deck path.
14. **[web] No media and no tooltips from the engine.** Ultralight plays no audio or video (CL:1297) and draws no
    `title` tooltips (CL:456). GraceUI v1 has no sound (GraceUI DESIGN §3.11). TTS voice previews (v1.5, O8) must play through
    EldenNet's audio, never the page. Tooltips are a GraceUI component.
15. **[any, Proton] Bundle every font, including a last resort.** System fonts may be missing (Proton, the Deck),
    and the engine may crash without its last resort (INV 19). GraceUI ships its OFL fonts, obtained from their
    publishers, and never depends on Windows fonts. Test under Proton with no core fonts.
16. **[any] Use UTF-8 paths everywhere.** Test a non-ASCII install folder and Windows user name, on Windows and
    under Proton (CL:153, 670). Rust's standard library uses wide APIs on Windows, but a C engine's file system or
    font loader may not.
17. **[UL, Proton] Keep the runtime small, preloaded and checked.** Avoid AppCore (its WinRT imports are missing
    on Proton: CL:412). Load the runtime by full path, refuse a mismatched one and stay inert (INV 3, CL:729).
    Ultralight under Wine is otherwise **unproven** here: the only Proton evidence is the AppCore import problem
    and the startup line that names Wine. Proton and the Steam Deck must be in the renderer spike.
18. **[UL] DLL name collisions.** Windows binds imports by base name (INV 3). Without Ultralight's permission to
    rename (Magelight's permission is personal to its author), a second Ultralight-based ER mod could load in our
    place. Ask Ultralight for the same permission, or make the version check catch it.
19. **[UL] The licence and the notices.**
    - A public free mod relies on §2.3, with its US$200,000 threshold for companies (§1).
    - Ship the whole licence folder unchanged: the EULA for players, the Free licence, Ultralight's NOTICES.md
      and our own notices.
    - Credit Ultralight with its copyright and trademark line and the NOTICES legend (`S:docs/DISTRIBUTION.md:
      65-95`).
    - Expect the 60 fps cap (CL:358).
    - Pin the SDK by SHA-256 and archive it (`S:extern/README.md:76-83`). Ultralight 2.0 is still a **beta** build
      of record.
    - Anything that needs undocumented engine exports is fragile under §4.1's reverse-engineering clause and at
      every SDK bump. The own-thread design (lesson 3) avoids needing them.
20. **[UL] Scale.** Below a device scale of 1, a View drew only part of the page (CL:1057), and fractional scales
    round by a pixel (CL:396). GraceUI's 75–150% scale should keep the device scale at 1 or above, do the smaller
    end with CSS `zoom` or layout tokens, keep the compositor on, and test 1.25 and 1.35 in the harness.
21. **[UL] An engine drawing defect constrains the Grace theme.** Translucent rounded boxes with differing border
    sides draw wedge-shaped artifacts (INV 12; `S:tools/desktop-harness/pages/translucent-borders.html`). The
    theme's framed, translucent panels should use uniform borders, opaque borders or square corners, and our
    harness should carry a repro page of our own.
22. **[web] Load and paint at quiet moments.**
    - The first show can be one long frame (86–192 ms), with images decoded only when painted (§3.20). On its own
      thread that cost leaves the game's frame, but the first full texture upload still lands in Present.
    - Load the page hidden after the world loads and prepaint it.
    - While hidden, the UI stops its timers and polling. Animations pause but timers do not
      (`S:docs/TROUBLESHOOTING.md:222-231`).
    - EldenNet pushes changes; the page does not poll.
    - The page learns it is open from an explicit bridge message, never from a visibility read (`S:docs/
      SDK.md:44-56`).
23. **[any] Bridge hygiene.**
    - The host never evaluates script in the page. Messages are typed JSON with a protocol version, sent as data
      (`S:src/Magelight.h:94-96`).
    - Identity is bound to the connection, never taken from the payload (CL:1212).
    - Payloads never throw: parse, log a short note, drop.
    - The core bridge object is installed once, so a late subscriber never replaces it (CL:1231).
    - EldenNet treats every UI message as untrusted input, the in-game page as much as a browser tab.
24. **[any] Network and storage.**
    - The in-game page fetches nothing remote, and its CSP allows only its own origin (CL:703-717). EldenNet's
      core does all model and network work.
    - Do not reuse a localhost web app in game with network permissions. SkyrimNet's dashboard needed loopback
      plus any host to run on Magelight (CL:1340-1358). That is the trade a native bridge avoids.
    - The browser host binds 127.0.0.1 only, with Host/Origin checks and, since the owner's G4 answer, a session
      cookie in place of the token (GraceUI DESIGN §7).
    - Settings live in EldenNet's `settings.toml` (ARCH §11.5), never in page storage. Without an explicit cache
      path, WebKit writes page storage under the game folder (CL:734-736).
25. **[any] Privacy in logs.** Page console lines go to the log at warning and error level only, scrubbed, cut at a
    few KB and rate-limited. Settings payloads, which may hold API keys, are never logged. User paths are masked
    (CL:668, 837-853). This is EldenNet's existing rule, applied to the UI.
26. **[any] Crash handlers never log or allocate.** A watchdog thread writes the record (INV 21, CL:468-476).
27. **[any] Versioning.** The hello names the bridge protocol version and a capability list. EldenNet checks the UI
    bundle's version. A mismatch shows a "needs updating" page, never a crash (CL:729; `S:views/gate/`). If GraceUI
    is linked into EldenNet's DLL (GraceUI DESIGN G2), there is no separate file pair for a mod manager to mix (`S:docs/
    DISTRIBUTION.md:48-63`). The owner chose otherwise (G2, 2026-11-07: GraceUI is a mod of its own), so GraceUI
    DESIGN §2.7 and §2.9 carry the version file and the mismatch handling this lesson describes.
28. **[any] Testing.**
    - The harness against a fake EldenNet (GraceUI DESIGN §9).
    - If HTML runs in game, a desktop harness that runs the in-game renderer in a window, compared with a browser
      (Magelight's driver-against-reference idea).
    - Pure logic (capture, focus, hotkeys, the pad ledger, IME parsing) in engine-free modules unit-tested on
      Linux CI.
    - Every increment carries its own in-game checks, so nothing ships "not yet run in game" (EldenNet O29).
29. **[any] Packaging.**
    - Plain zips with the runtime apart from developer tools.
    - Zip entries written with `/`.
    - A stage check that loads the host with the renderer runtime missing and confirms it stays inert and exits
      cleanly (INV 3).
    - A licence folder, and a pinned, archived renderer build.
30. **[process] Start an invariants list now.** GraceUI's CLAUDE.md should keep invariants with their reasons, and
    each needs a test to re-run when the renderer or hudhook changes (`S:CLAUDE.md:61-473`).

## 7. Bearing on the open questions (evidence only; renderers.md weighs the renderer, GraceUI DESIGN §12 recommends)

- **G1, the renderer.**
  - Ultralight works and is efficient in GPU mode on D3D11 (§3.20).
  - The costs: a proprietary licence that rests on §2.3; about 57 MB of runtime and data; a 60 fps cap; no media;
    no D3D12 driver; Proton unproven; and a long tail of engine invariants (§2, §6).
  - The cheapest in-game HTML path is Ultralight's CPU mode on its own thread, uploaded through hudhook as an ImGui
    texture, with dirty rectangles and a premultiplied-alpha fix (lessons 3–5). It needs a spike on Windows,
    Proton and the Deck.
  - Dear ImGui alone avoids 17 of the 27 invariants. It keeps the browser dashboard as the full web UI and gives up
    "the same pages in game".
- **G2, library or host.** Being a host for other mods cost Magelight a C ABI with append-only discipline and a CI
  checker, a per-mod registry, UI-mode ownership (busy, denied, queue, cancel, reclaim), a sandbox and storage
  jars per mod, version gates and two-copies handling (§3.15, §3.16). That favours **linking GraceUI into
  EldenNet's DLL in v1**, with the bridge as the seam that could later become a host API. *The owner decided G2 on
  2026-11-07: a standalone mod, bundled with each EldenNet release and updated by file (GraceUI DESIGN §2.7, §2.9;
  G10 for the overlay). The owner decided G10 the same day: EldenNet keeps the overlay and `graceui.dll` hooks
  nothing (G10 (b)).*
- **G3, the UI framework.** WebKit 615 needs es2022 bundles, `base './'`, no `crossorigin`, and no module-preload
  polyfill under a strict CSP (INV 8; CL:703-717; Magelight's Vite plugin). React runs well in Ultralight (a large
  SeverActions frontend; `S:examples/Magelight.ReactConfig/`). If GraceUI renders HTML in game, parse cost on
  first load matters more than bundle size, which favours a small framework. *The owner decided G3 on 2026-11-07:
  Preact with TypeScript and signals, built with Vite (GraceUI DESIGN G3).*
- **G4, the browser dashboard.** Magelight moved from network-on to **network off by default, opt-in and logged**
  before 1.0, because tightening later would break users (CL:1137-1160). That supports an **opt-in** browser
  dashboard. *The owner decided G4 on 2026-11-07: always on, as SkyrimNet's dashboard is (GraceUI DESIGN §7.2).*

## 8. Gaps and unverified points

- The v0.30.1 tag's `gpu/` licence is inferred from 1.7.1's documents (§1). GitHub was not reachable when this
  note was written.
- Whether the Ultralight 2.0 SDK ships a D3D12 reference driver: this copy names only `d3d11` paths
  (`S:extern/README.md:99-106`). Check against the SDK itself.
- How fast the CPU path is: Magelight never measured it in this copy.
- hudhook 0.9.3's texture updates, blend state and descriptor access: not covered by this source, so S4 must check.
- Ultralight under Wine/Proton beyond the AppCore issue: no evidence either way.
- About 85% of `S:src/Magelight.cpp` was not read. The architecture above rests on its headers, `S:CLAUDE.md`, the
  docs and the CHANGELOG, which are detailed and agree with the ranges that were read.
