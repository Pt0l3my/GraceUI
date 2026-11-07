# Study: in-game renderer options for GraceUI (Elden Ring, Direct3D 12, a Rust host, one hudhook 0.9.3 overlay)

Status: research note for the GraceUI design (question G1 of [GraceUI DESIGN](../DESIGN.md) §12), 2026-11-07, cited as **REN**. Own words throughout. Nothing here was
built or run; third-party packages were read as data only.

## 0. Sources, method and limits

- **EldenNet's rules** that bind the choice: DESIGN O25 (hudhook pinned at exactly 0.9.3, `dx12` feature only,
  `imgui` only through `hudhook::imgui`, MinHook only through `hudhook::mh`, never the eject path), DESIGN §2.6 R12
  (system-DLL hooks only, all through hudhook's one MinHook), ER02 §7.1 (two Present hooks or two MinHook copies in
  one process conflict), DESIGN §8.1 (input capture), §12 (presentation), NPC §10.1 and §10.7 (atlases, HDR, scale).
- **Companion studies**, cited rather than repeated: [magelight.md](magelight.md) (Magelight UI 1.7.1 on Ultralight;
  cited as "ML §n" and "ML lesson n") and [severactions-look.md](severactions-look.md) (the Grace theme; cited as
  "LOOK §n").
- **hudhook 0.9.3, imgui 0.12.0 and imgui-sys 0.12.0** were read from their published crates (crates.io), unpacked
  into an isolated scratch folder. Line numbers below are in those crates (`hudhook-0.9.3/src/...`). The
  texture-upload code on hudhook's `main` branch was also checked and is unchanged.
- **Other crates** (ul-next 0.5.4, cef 156.3.0, servo 0.7.0, blitz-dom 0.3.0-beta.4, litehtml 0.2.6, sciter-rs
  0.5.58) were read for their licence and metadata fields only. Latest versions come from the crates.io index.
- **The Ultralight Free License Agreement V2** was read in the Magelight release copy (its `license/` folder),
  with grep only.
- **Web.** Searched on 2026-11-07. `ultralig.ht`, `docs.ultralig.ht`, `docs.rs`, `servo.org` and `blitz.is` could
  not be fetched when this note was written (outside that environment's network allowlist). Facts from those sites come from search-result
  summaries and are marked **[search]**; they should be re-read at the source before the design relies on them.
  GitHub pages could be fetched and are marked **[read]**.
- Labels: **FACT** (read in a primary source), **LIKELY** (good secondary evidence or strong inference),
  **UNVERIFIED** (plausible, not checked), **EST** (an estimate to be measured).

## 1. Summary

1. **The pinned overlay decides most of this.** hudhook 0.9.3 gives a renderer exactly two things: ImGui draw lists,
   and textures through `load_texture` and `replace_texture`. Its `replace_texture` copies the whole image, creates
   a new upload buffer on every call and **leaks it** (a comment in the code says so), waits for the GPU on the
   calling thread, and never moves the texture back to the copy state before copying (§2.1). hudhook exposes no
   D3D12 device, queue, command list or descriptor heap (§2.2). So:
   - renderers that produce **geometry** (Dear ImGui itself; RmlUi, litehtml or Blitz through a bridge that turns
     their output into ImGui triangles) fit O25 as it stands;
   - renderers that produce **pixels** (Ultralight's CPU mode, CEF's `OnPaint`, Servo's read-back, Sciter, Slint)
     need a fixed texture-upload path, that is, a **change to hudhook**, which O25 reserves to the owner;
   - renderers that need the **GPU** (an Ultralight D3D12 driver, CEF's shared textures, Vello on wgpu) need the
     game's device and a command list: a much larger hudhook change, or a second hook, which O25 and R12 forbid.
2. **Two hazards apply whatever the renderer**, and EldenNet should fix them now: hudhook's `WM_CHAR` handler
   unwraps a UTF-16 code unit as a `char`, so a surrogate pair (an emoji from the Windows emoji panel or the Steam
   keyboard) panics inside the window procedure (§2.4); and the font atlas is uploaded once, so a rebuild on a
   scale change must be uploaded by EldenNet (§2.4).
3. **Ultralight** is the closest match to "Magelight's appearance": the same HTML, CSS and TypeScript in game and in
   the browser. Its costs are a proprietary licence (§4.2.1), about 57 MB of runtime, no JIT and no WebAssembly in
   the free edition, a 60 fps cap on animation, no public Proton evidence, and, under O25, no way to put its pixels
   on screen without changing hudhook. Its CPU mode is the right first step if the owner wants HTML in game; a
   D3D12 GPU driver is not worth it for a menu.
4. **CEF** has the best web compatibility and IME, but about 200 MB for `libcef.dll` alone, several extra
   processes, mixed Wine results and the same O25 problem. **Servo** (MPL-2.0, about 153 MB, monthly breaking
   releases) and **Blitz** (MIT or Apache-2.0, beta, no JavaScript) are not ready for a game mod. **RmlUi** (MIT) is
   the strongest non-browser option: it can be drawn through ImGui geometry within O25, but its pages are not the
   browser's pages.
5. **Recommendation (§7):** in v1, draw the in-game menu with **Dear ImGui and a native Grace theme** inside
   EldenNet's existing overlay, and show the **full web UI in the player's browser**. Both read one page and settings
   schema, one bridge protocol and one token source, so they cannot drift. Keep HTML in game as a later, owner-gated
   spike: **Ultralight in CPU mode** behind a minimal, reviewed hudhook texture patch, tested on Windows, Proton and
   the Steam Deck before anything is promised.

## 2. What hudhook 0.9.3 lets a renderer do (the O25 constraint)

### 2.1 Textures

- **API.** `RenderContext` has two methods: `load_texture(data, width, height) -> TextureId` and
  `replace_texture(id, data, width, height)` (`hudhook-0.9.3/src/lib.rs:155-170`). There is no sub-rectangle, no
  unload and no format choice. Both are reached from `ImguiRenderLoop::initialize` and `before_render`
  (`lib.rs:269-291`). FACT.
- **Format and blend.** Textures are `R8G8B8A8_UNORM` (`renderer/backend/dx12.rs:912, 926, 1033`). The ImGui pipeline
  blends `SRC_ALPHA / INV_SRC_ALPHA`, that is, straight alpha (`dx12.rs:638-642`). Ultralight's and CEF's surfaces
  are premultiplied BGRA, so every frame would need a swizzle and an un-premultiply on the CPU (ML lesson 5). FACT.
- **The upload** (`dx12.rs:954-1067`), every call:
  - creates a new committed upload buffer of `height × pitch` bytes (`:976-1002`): 14.7 MB for a 2560×1440 page, or
    4.1 MB at the Deck's 1280×800;
  - copies the **whole** image (`:1006-1014`) and refuses any size but the texture's own (`:962-968`);
  - records a copy into the texture, then a barrier from `COPY_DEST` to `PIXEL_SHADER_RESOURCE` (`:1043-1050`). The
    texture is created in `COPY_DEST` (`:872` onward), so the first upload is correct; on every later call the
    texture is already in `PIXEL_SHADER_RESOURCE` and is copied into without a transition. The debug layer would
    report this, and its effect on real drivers and on vkd3d-proton is undefined;
  - submits on its own queue and **waits for the fence on the calling thread** (`:1052-1055`), which is the game's
    Present path;
  - leaves one reference on the upload buffer forever. The comment at `:1059-1063` says "leaking the upload buffer
    into the location is necessary", and only the destination's reference is released (`:1064`). FACT.
- **Same on `main`.** hudhook's current `main` branch has the same function: new buffer per call, the same comment,
  no barrier back to `COPY_DEST`, a blocking fence wait, no sub-rectangle [read]. A newer hudhook would not fix it.
- **Consequence.** A web page that repaints on hover, scroll or a caret blink cannot be shown through this API. At
  60 updates a second a 1440p page leaks about 0.9 GB a second; tiling the page into small textures only divides
  the leak. Even rare updates (one per page change) leak 4–15 MB each. **Per-frame or per-change texture updates are
  ruled out under O25 as it stands.** One-time uploads (the font atlas, our own ornament and icon textures at load)
  are fine.

### 2.2 No device, no command list

- hudhook keeps its device, queues, command lists and descriptor heaps private (`dx12.rs:58`, `:764-774`). The
  public surface is the render loop, `RenderContext`, the hooks builder, `mh`, `util` and `find_process_hwnd`
  (`lib.rs:120-145`, `hooks/mod.rs:27`). FACT.
- ImGui's `RawCallback` draw command is passed only the ImGui draw list and command (`dx12.rs:340`), not the D3D12
  command list, so a callback cannot record GPU work. FACT.
- `TextureId`s index hudhook's own heap (`dx12.rs:322, 948`), so EldenNet cannot hand ImGui a texture it created
  itself. FACT.
- **Consequence.** No GPU renderer (an Ultralight GPUDriver, CEF's shared texture, wgpu or Vello) can draw into
  hudhook's frame without changing hudhook. Getting the game's device another way (a second Present or
  `ExecuteCommandLists` hook) is what O25 and R12 exist to prevent (ER02 §7.1).

### 2.3 What fits, by class

| Class | Examples | Under O25 today | What would unlock it |
|---|---|---|---|
| Geometry through ImGui draw lists | Dear ImGui; RmlUi, litehtml or Blitz through a geometry bridge; egui | Fits. Textures only at load or on a rare rebuild | Nothing |
| Pixels uploaded as a texture | Ultralight CPU mode, CEF `OnPaint`, Servo read-back, Sciter, Slint's software renderer, Blitz on `vello_cpu` | Does not fit (§2.1) | A hudhook patch: persistent upload ring, sub-rectangle copies, correct barriers, recorded in the frame's command list, no leak, no blocking wait |
| GPU drawing in the game's frame | Ultralight D3D12 driver, CEF `OnAcceleratedPaint`, Vello on wgpu, RmlUi's own DX12 renderer | Does not fit (§2.2) | A larger hudhook change exposing the device, queue, descriptor heap and a per-frame command list; or our own DX12 hooks (O25's rejected alternative) |

**The patch that would unlock the pixel class** is small and self-contained: about one function plus a ring buffer
in `dx12.rs`, with a `replace_texture_region(id, rect, data, stride)` entry point. It would make the dependency
"hudhook 0.9.3 plus a reviewed patch" (a `[patch.crates-io]` fork), which is **not** "exactly 0.9.3", so it needs
the owner's approval under O25, and the release checks in ARCH §2.5 and §12.1 would have to name it. It could be
offered upstream; a later upstream release would still need the owner's approval and a fresh licence read.

### 2.4 Hazards that apply to every option

- **`WM_CHAR` panic.** `renderer/input.rs:339` does `char::from_u32(wparam as u32).unwrap()`. Windows delivers a
  character outside the Basic Multilingual Plane as two `WM_CHAR` messages, each a lone surrogate, for which
  `from_u32` returns `None`, so the unwrap panics inside the hooked window procedure. Emoji from the Windows emoji
  panel, and possibly from the Steam on-screen keyboard (UNVERIFIED), would trigger it. Fix within O25: in
  `before_wnd_proc`, take `WM_CHAR` (and `WM_UNICHAR`) ourselves, pair surrogates, queue the characters, return
  `BeforeWndProc::Break` so hudhook's handler is skipped (`input.rs:274-278`; `lib.rs:176-183`), and feed them to
  ImGui in `before_render`. S4 should reproduce the panic first on Windows, Proton and the Deck. FACT for the code;
  the in-game effect is LIKELY.
- **Font atlas.** hudhook builds and uploads the ImGui font atlas once, after `initialize` (`renderer/mod.rs:18-24`,
  `renderer/pipeline.rs:70-72`). NPC §10.7 rebuilds atlases when the scale changes; with 0.9.3 EldenNet must rebuild
  the atlas itself in `before_render`, upload it with `load_texture`, and set the atlas's texture id. Each rebuild
  costs one leaked upload buffer (an RGBA atlas of 2048×2048 is 16 MB). Bounded and rare, so acceptable; it should
  be logged. FACT for the code.
- **Dear ImGui is 1.89.2.** `imgui-sys` 0.12.0 bundles Dear ImGui 1.89.2 (`imgui.h:25`). The 1.92 dynamic font system,
  which builds glyphs on demand, is not available ([Debian FONTS.md](https://sources.debian.org/src/imgui/1.92.2b%2Bds-1/docs/FONTS.md)).
  Atlases must be baked per size with fixed glyph ranges (NPC §10.1). FACT.
- **IME.** `imgui-sys` is built with `IMGUI_DISABLE_WIN32_FUNCTIONS` (`imgui-sys-0.12.0/build.rs:8`), which also
  removes Dear ImGui's default Win32 IME positioning (the guard at `imgui.cpp:12941` around the
  `ImmSetCompositionWindow` code at `:12960-12966`). hudhook handles no `WM_IME_*` message. Unless EldenNet sets `SetPlatformImeDataFn` and calls the
  IMM functions itself, the OS composition window sits at its default place, which may be hidden in fullscreen
  (ML lesson 13). Characters still arrive as `WM_CHAR`. `IMGUI_USE_WCHAR32` is on (`build.rs:5`), so non-BMP
  glyphs can be stored. FACT.
- **Tables.** The safe tables API in imgui-rs 0.12 is behind its `tables-api` feature (`imgui-0.12.0/src/lib.rs:36,
  87`), reached through hudhook's `imgui-tables-api` feature, which the `dx12`-only rule leaves off. Tables would be
  drawn with our own column layout or through `hudhook::imgui::sys`; enabling the feature would be an owner
  decision under O25. FACT.

## 3. Criteria

Each option is judged on: **licence and obligations**; **size** shipped; **performance for a menu** (a sidebar and a
dense settings page, open at a safe moment, mostly still); **input and IME** (mouse, keyboard, pad, the Steam
keyboard, CJK composition); **Proton and Steam Deck** (vkd3d-proton, Wine's DirectWrite and IMM, 4 Zen 2 cores,
1280×800); **maintenance** (update cadence, our code to keep, invariants to re-test); **fit with O25**; **fit with a
browser-hosted copy of the same UI** (how much of the in-game view is the browser view).

## 4. The options

### 4.1 Dear ImGui only, with a native "Grace" theme

- **What it is.** The in-game menu is drawn by EldenNet's existing ImGui context: our widgets, our layout code, the
  Grace tokens (LOOK §3) compiled into an ImGui style and a small draw library on `ImDrawList` (LOOK §3.14 already
  plans one token source for CSS and for ImGui).
- **Licence.** Nothing new: Dear ImGui (MIT) inside `imgui-sys`, hudhook (MIT), MinHook (BSD-2), already in
  THIRD_PARTY.md (ARCH §12.1). Fonts are OFL, from their publishers (LOOK §3.4). Our own ornament textures.
- **Size.** No new binary. Fonts and a few small textures of our own, about 1–3 MB (EST).
- **Performance.** The overlay already renders every frame for subtitles. An open menu adds vertices only: a dense
  settings page is a few thousand to a few tens of thousands of vertices, expected well under 1 ms of CPU a frame
  (EST; S4 measures at 1440p and on the Deck). No uploads, no extra threads, no first-open stall.
- **Input and IME.** Mouse and keyboard come through hudhook's window procedure; capture follows DESIGN §8.1. Pad
  navigation is native to ImGui (`ImGuiConfigFlags_NavEnableGamepad`, `imgui.h:1496`); EldenNet feeds pad state
  itself, since hudhook reads no XInput (FACT: no gamepad code in `renderer/input.rs`). IME: the OS composition
  window, positioned by EldenNet (§2.4); composition is not drawn in the field. Fix the `WM_CHAR` panic first.
- **Proton and Deck.** The best evidence of any option: this is the path EldenNet already depends on. hudhook states
  Windows and Wine/Proton support (ER02 §7.1;
  [hudhook README](https://rust-digger.code-maven.com/src/hudhook-0.8.0/README.md)), and the hudhook-based Elden Ring
  practice tool says it runs on Linux and "should" run on the Deck
  ([eldenring-practice-tool](https://github.com/veeenu/eldenring-practice-tool)) [read]. No independent Deck report
  was found, so S4 must still test it. Nothing new touches DirectWrite, the GPU or extra processes.
- **Maintenance.** All ours: a widget set, a layout helper, the theme. No engine invariants to re-test (ML §2 counts
  17 of Magelight's 27 invariants as engine-specific).
- **O25.** Fits exactly.
- **Browser copy.** None of the view code is shared. What is shared: the information architecture, the page and
  settings schema, the bridge protocol and the theme tokens. Settings pages can be generated from the schema in
  both views; Home and the NPC pages are written twice, small in v1. The browser keeps everything the game should
  not carry (editors for long text, logs, traces).
- **What the look would lose**, against the Grace theme in LOOK §3:

| Grace element (LOOK) | In ImGui 1.89.2 through hudhook | Loss |
|---|---|---|
| Palette, grounds, hairline rules, 2 and 4 px radii, scrim (§3.2, §3.6) | Native: `AddRect`, `AddRectFilled` with per-corner rounding, `AddLine` | None, but 1 px hairlines need pixel snapping at fractional scales |
| Lozenge rule (§3.6) | `AddQuad` and `AddLine` | None |
| One soft shadow, 0 8 24 at 50% (§3.6) | No blur; a pre-blurred nine-slice texture of our own, drawn with `AddImage` | Small; the shadow does not adapt to odd sizes as CSS does |
| Grace glow, radial (§3.6) | A triangle fan with vertex colours (through `PrimReserve` in `imgui::sys`) or a small texture of our own | Small |
| Ash grain (§3.6, §3.13) | A tiled noise texture of our own | None |
| Fades 120 / 300–400 ms (DESIGN §12, LOOK §3.10) | Our own tweening of alpha | None; easing is code, not CSS |
| Cinzel display, EB Garamond prose, Noto Sans UI (§3.4) | TTF/OTF through stb_truetype; static instances only (no variable-font axes) | Variable Cinzel must ship as static weights |
| Tracking +0.02 to +0.14 em (§3.5) | `ImFontConfig::GlyphExtraSpacing` per baked font (`imgui.h:2658`) | One atlas entry per tracked size; fine |
| Kerning | **None.** Dear ImGui 1.89.2 lays text out by glyph advance only (no kerning code in `imgui_draw.cpp`) | Visible in Cinzel and EB Garamond at display sizes; titles look looser and uneven |
| Lining and tabular figures, small caps (§3.5) | OpenType features are not applied; EB Garamond's default old-style figures would show | Needs a feature-frozen build of the font (OFL allows modified versions; a Reserved Font Name, if any, must then be dropped from the name). Check each font's RFN |
| Complex scripts and right-to-left text | No shaping (no HarfBuzz) | Matters only when localisation reaches such scripts |
| Text layout | Wrap at a width only: no hyphenation, justification, widow control, inline mixing of fonts and colours without our own code | Rich text (help with emphasis, links) is our own small layout routine |
| Tables, flex-like layout | Our own layout helpers; the table API is off under O25 (§2.4) | Development time, not looks |
| Accessibility | No screen-reader tree | The browser copy is the accessible surface |

  The Grace theme is deliberately "flat and still" (LOOK §3.1: hairlines, flat layers, fades only, no blur, no
  loops), so most of it is cheap in ImGui. The real losses are **typography** (no kerning or OpenType features) and
  **authoring speed** (every layout is code). SeverActions' richer effects (backdrop blur, glows, glass) are not in
  Grace anyway (LOOK §1.6, §3.15).

### 4.2 Ultralight

- **Versions.** Stable 1.4.x: the supported-features page is "current as of Ultralight v1.4.1", WebKit
  615.1.18.100.1, about Safari 16.4.1
  ([Supported Web Features](https://github.com/ultralight-ux/Ultralight/wiki/Supported-Web-Features)) [read]. 2.0
  is a **public beta**: 2.0.0-beta.1 on 2026-08-28, beta.2 changelog last updated 2026-10-02, with a new GPU vector
  renderer, video (VP9, Opus), a native DOM API and Linux x64
  ([2.0 announcement](https://ultralig.ht/blog/ultralight-2-0-out-now),
  [beta.2 changelog](https://ultralig.ht/docs/2.0/changelog-2-0-0-beta-2),
  [GPU renderer overview](https://ultralig.ht/docs/2.0/gpu-renderer-overview)) [search]. Magelight pins
  2.0.0-beta.2 (ML §3.19). Custom GPU drivers "require substantial updates" from 1.4 to 2.0, and a custom Surface
  gains a `Scroll()` method ([migration guide](https://ultralig.ht/docs/2.0/migrating-from-1-4-to-2-0.md)) [search].

#### 4.2.1 Licence and obligations

From the Free License Agreement V2 in the Magelight release (FACT, our reading; not legal advice):
- **§2.1** development only; no distribution. **§2.2** "Non-Commercial" distribution means academic or scholarly
  research, so a public free mod does not rest on it; and §1.1.4 counts promotion as Commercial. A public mod
  therefore rests on **§2.3**, the limited commercial grant, void for a company or incorporated entity whose
  trailing-twelve-month turnover or total funding reaches **US$200,000**, with 30 days to buy a tier (matches ML
  §1).
- **§3.3** Improvements, defined to include any "suggestion" by the Licensee that improves Ultralight (§1.1.12), are
  assigned to Ultralight Inc. A bug report with a proposed fix would be theirs.
- **§4.1** no reverse engineering or modification; **§4.2 no static linking** (dynamic DLLs only, which suits a Rust
  host); **§4.3** end users bound by the EULA in Exhibit A (ship `EULA.txt`); **§4.4** a credit legend pointing to
  NOTICES.md; **§4.7** report changes of contact details and when development stops; **Article 9** confidentiality
  (scope not read); **§10.1.2** termination for an Article 4 breach not cured within 30 days; Articles 3 and 9–11
  survive termination (§10.3).
- No AI clause (GraceUI's CLAUDE.md). WebKit's LGPL-2.1 and BSD notices, ICU and others ship with it (ML §3.19).
- **Free-edition limits:** no JIT and no WebAssembly on any platform; JavaScript runs in the interpreter (Supported
  Web Features) [read]. Animation, `requestAnimationFrame`, transitions and script repaints capped at 60 a second
  in the free 2.0 edition (Magelight CHANGELOG, ML lesson 19).
- **DLL names.** Magelight's renamed runtime DLLs rest on permission given to its author only (ML §3.19, lesson 18);
  GraceUI would ship Ultralight's own names and must detect a second Ultralight-based mod.
- **Rust.** The `ul-next` crate (0.5.4) wraps SDK `1.4.0-beta` (its `Cargo.toml` pins `ul-next-sys =1.4.0-beta...`)
  and ships the **V1** licence text; it exposes the CPU `Surface` with `dirty_bounds()` (`surface.rs:142-167`) and a
  GPU-driver trait. For 2.0 we would write our own FFI (FACT for the crate).

#### 4.2.2 The rest

- **Size.** About 57 MB: under Magelight's names, the core runtime DLLs are 1.6 + 3.2 + 45.6 MB, plus 6.5 MB of ICU
  data and 0.2 MB of CA certificates (the release listing; ML §3.1). FACT.
- **CPU-bitmap mode versus a D3D12 GPU driver.**
  - *CPU mode* draws into a premultiplied BGRA `Surface` with dirty bounds; the 2.0 docs say it needs no graphics
    integration and that CPU and GPU views can coexist ([integrating into a game engine](https://ultralig.ht/docs/2.0/integrating-into-a-game-engine))
    [search]. It touches no GPU API, which is its Proton advantage. Under O25 it needs the hudhook upload patch
    (§2.3), a swizzle and un-premultiply (or a premultiplied blend for that one draw, which hudhook's single PSO does
    not offer), and dirty-rectangle uploads.
  - *A D3D12 GPU driver* would be ours: AppCore has a `src/win/d3d12` reference folder beside `d3d11`
    ([AppCore src/win](https://github.com/ultralight-ux/AppCore/tree/master/src/win)) [read], written for the 1.x
    driver interface, which 2.0 changed. It would need the game's device, a command list, descriptor space and state
    save and restore inside hudhook's frame (ML lesson 4), so the larger hudhook change of §2.3, plus vkd3d-proton
    risk. For a mostly still menu it buys GPU time the menu does not need.
- **Performance.** Magelight's GPU-mode figures on D3D11 are good (0.04 ms CPU for an idle page; ML §3.20). CPU-mode
  cost was never measured in that copy (ML §8). First open is one long frame of 86–192 ms on the page thread (ML
  §3.20), so prepaint while hidden. A full 1440p upload is 14.7 MB, 4.1 MB at 800p. The interpreter-only JavaScript
  favours a small UI framework (GraceUI DESIGN G3). Raster threads compete with the game for the Deck's four cores (EST, spike).
- **Input and IME.** The host translates Win32 messages into Ultralight key, char and mouse events. "Advanced IME
  composition APIs are not supported" (Supported Web Features) [read]; Magelight builds its own composition handling
  (ML §3.9). Pad navigation is ours (Magelight's ledger, ML §3.10). No native pickers (date, colour, file).
- **Proton and Deck.** No public report of Ultralight under Wine or Proton was found (searched 2026-11-07, including
  Prisma UI, the other Ultralight host for Skyrim). Magelight's own changelog records only that AppCore's WinRT
  imports are missing under Proton, and logs the Wine version (ML §3.2, lesson 17). The Windows font loader goes
  through DirectWrite, which Wine implements in part; GraceUI must bundle every font (ML lesson 15). UNVERIFIED in
  practice.
- **Maintenance.** A pinned and archived SDK (2.0 is a beta), a long tail of engine invariants (ML §2: 17 of 27), a
  licence re-read at each update, our FFI.
- **O25.** CPU mode: needs the small hudhook patch. GPU driver: needs the large one.
- **Browser copy.** The same bundle runs in both, if it targets WebKit 615 (es2022, no features newer than Safari
  16.4; the 1.4 list also lacks `text-shadow`, conic gradients, 3D transforms and `border-image`) [read]. The in-game
  page must not fetch anything remote and must not get network permissions (ML lesson 24).

### 4.3 CEF (Chromium Embedded Framework), off-screen rendering

- **Licence.** CEF is BSD-3-Clause; Chromium brings hundreds of third-party licences whose credits must ship. The
  Rust `cef` crate (tauri-apps `cef-rs`, 156.3.0 for CEF 156) is Apache-2.0 OR MIT (FACT, its `Cargo.toml`).
- **Size.** `libcef.dll` alone was about 204 MB in the CefSharp NuGet build of CEF 140 (32-bit), and about 135 MB
  in a custom build ([CefSharp discussion 5181](https://github.com/cefsharp/CefSharp/discussions/5181)); 64-bit
  builds are of the same order (LIKELY). Required beside it: `chrome_elf.dll`, `icudtl.dat` (about 10 MB), the V8
  snapshots and, in practice, the `.pak` resources
  ([README.redistrib](https://third-party-mirror.googlesource.com/cef/+/8265caad76429c18c91b1cfabe571852a67e0e5f/src/tools/distrib/win/README.redistrib.txt)).
  Total about 250–300 MB (EST), four to five times Ultralight.
- **Processes.** The browser process would be the game process (our DLL); CEF starts renderer, GPU and network
  service processes through a helper executable we ship
  ([CEF general usage](https://chromiumembedded.github.io/cef/general_usage),
  [wiki](https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage)) [search]. The Chromium sandbox is normally
  off for a DLL host (LIKELY).
- **OSR modes.** `OnPaint` hands a BGRA buffer; a CEF mirror commit says its dirty rectangles now always cover the
  whole view, at most 60 fps through `windowless_frame_rate`
  ([commit](https://gitea.it/bcarrella/cef/src/commit/dea4daffd71be5262cf42fa8f3ba6a93d0465c9f)) [search], so every
  change would be a full upload. `OnAcceleratedPaint` hands a D3D11 shared NT handle without a keyed mutex
  ([cef_accelerated_paint_info_t](https://cef-builds.spotifycdn.com/docs/135.0/structcef__accelerated__paint__info__t.html),
  [cef-mixer](https://github.com/twobrainsgmbh/cef-mixer)); `cef-rs` opens it on D3D12 with `OpenSharedHandle`
  (`cef-156.3.0/src/osr_texture_import/d3d11.rs:150-185`, FACT). hudhook cannot sample such a resource (§2.2).
- **Performance.** V8 with JIT: fastest scripts of any option. Extra processes cost memory and CPU on the Deck
  (EST: a few hundred MB of RAM).
- **Input and IME.** The best: CEF's OSR host API has IME composition calls the host feeds from IMM (the
  cefclient OSR sample has a Windows IME handler) (LIKELY; API docs not re-read).
- **Proton and Deck.** Mixed. WineHQ bug 52513 ("CEF not working correctly") records `--no-sandbox` helping one
  reporter and not another ([bug 52513](https://bugs.winehq.org/show_bug.cgi?id=52513));
  Wine developers note that Wine cannot draw into a window created by another process, which breaks Chromium's
  out-of-process GPU drawing in launchers (same thread) [search]. OSR draws no window, which may avoid that, but
  cross-process shared textures under Wine are their own risk (UNVERIFIED). No report of CEF OSR inside a Proton game
  through an injected DLL was found.
- **Maintenance.** Chromium ships security fixes about every four weeks; a mod would carry a stale browser. Huge
  build and update surface.
- **O25.** Both modes blocked (§2.1, §2.2).
- **Browser copy.** Perfect: the same page in Chromium.
- **Verdict.** Too large and too risky for a mod's menu; only worth a look if Ultralight fails and size stops
  mattering.

### 4.4 RmlUi

- **What it is.** A C++17 HTML/CSS-like UI library: RML documents and RCSS styles (most of CSS2, some CSS3, flexbox,
  media queries, custom properties), animations and transitions, box shadows, masks, gradients, `filter` and
  `backdrop-filter`, data bindings, templates, localisation, sprite sheets, spatial navigation, optional Lua
  ([RmlUi](https://github.com/mikke89/RmlUi)) [read]. Latest 6.3, which added DirectX 12 and 11 renderers
  (`Win32_DX12`, `SDL_DX12`, `GLFW_DX12`; #648, #975) ([changelog](https://raw.githubusercontent.com/mikke89/RmlUi/master/changelog.md))
  [read]. Used by game projects (forks for Unvanquished and ezEngine appear on GitHub) (LIKELY).
- **Licence.** MIT. FreeType (FTL with credit, or GPLv2) for fonts, replaceable by a custom font engine. No AI clause
  in MIT.
- **Size.** A few MB with FreeType (EST).
- **Rust.** No crate on crates.io (index checked); we would write our own FFI and build it with `cc` in `build.rs`,
  as `imgui-sys` and hudhook already build C and C++.
- **O25.** Its own DX12 renderer needs the device and a command list, so it is out (§2.2). But RmlUi's render
  interface hands the host triangles, indices, a texture and a scissor, which an EldenNet bridge can emit as ImGui
  draw-list geometry (custom vertices through `PrimReserve`), with textures loaded once. What the bridge cannot do
  through ImGui's one shader: the 6.x layer, filter, mask and shader features (CSS filters, `backdrop-filter`, mask
  images, shader gradients, and box shadows drawn through layers). Font effects (shadow, outline, glow) are baked
  into glyph textures, so they survive; new glyph textures appear when new sizes or characters are first used, so
  pre-generate them at load to keep `load_texture` rare (§2.1). Transforms are applied on the CPU. LIKELY; needs a
  spike.
- **Performance.** CPU layout with cached geometry; through the bridge the triangles are re-submitted each frame,
  a cost comparable to ImGui's (EST).
- **Input and IME.** `TextInputHandler` since 6.0, IME on the Win32 and SDL backends, placeholder text in 6.3,
  spatial navigation for the pad [read]. We would forward IMM ourselves.
- **Proton and Deck.** The same profile as ImGui: no new GPU path, FreeType on the CPU, our own fonts.
- **Maintenance.** An active MIT project, our FFI and the bridge, a second UI toolkit beside ImGui in one overlay
  (two focus models to reconcile).
- **Browser copy.** Low. RML and RCSS are not HTML and CSS, and RmlUi runs no JavaScript, so the browser app and the
  in-game documents are written separately. The tokens and schema are shared.
- **Verdict.** The best fallback if the ImGui Grace theme proves too plain and the owner keeps O25 as it is. Not for
  v1: it adds a C++ engine and a bridge for a look the Grace theme mostly reaches in ImGui.

### 4.5 Blitz (DioxusLabs)

- **What it is.** A modular HTML/CSS engine in Rust: Stylo (Firefox's style engine), Taffy layout (flex, grid,
  block, table), Parley text, painting through AnyRender to Vello on wgpu, `vello_cpu` or Skia; the renderer behind
  Dioxus Native ([Blitz](https://github.com/DioxusLabs/blitz)) [read],
  ([Dioxus 0.7](https://dioxuslabs.com/blog/release-070/)) [search].
- **Maturity.** The README now says "beta"; the crates are 0.3.0-beta.4 (crates.io index, FACT). The project's about
  page aimed at "a broadly usable beta by the end of 2025" and production in 2026 ([about](https://blitz.is/about))
  [search]; the roadmap lists a 0.1 alpha in June 2025 ([roadmap #119](https://github.com/DioxusLabs/blitz/issues/119)).
  `vello_cpu` 0.3.0 "will panic" on complex filter graphs ([vello_cpu](https://crates.io/crates/vello_cpu)) [search].
- **Licence.** MIT OR Apache-2.0 (FACT, `blitz-dom` `Cargo.toml`); Stylo is MPL-2.0.
- **No JavaScript.** The README says it has no bindings for other languages, JavaScript included [read]. Pages are
  driven from Rust (Dioxus) or are static HTML.
- **Size.** Not measured; Stylo alone is large (UNVERIFIED).
- **O25.** `vello_cpu` pixels hit §2.1; wgpu hits §2.2. A custom AnyRender backend that tessellates into ImGui
  geometry is conceivable but does not exist.
- **Input and IME.** Not documented in the README; AccessKit accessibility is a goal [read].
- **Browser copy.** In principle one Rust (Dioxus) codebase for web and native, but not our TypeScript app, and
  Blitz's CSS coverage is still partial.
- **Verdict.** Watch; revisit at 1.0. Not for a game overlay in 2026.

### 4.6 Servo

- **Maturity.** First crates.io release of the `servo` crate, 0.1.0, on 2026-04-13, with monthly releases that may
  break the API and an LTS line ([Servo 0.1.0](https://servo.org/blog/2026/04/13/servo-0.1.0-release/)) [search];
  0.7.0 is the latest on crates.io (index, FACT); the March 2026 post renamed several WebView methods [search].
- **Licence.** MPL-2.0 (FACT, `Cargo.toml`): file-level copyleft, source of modified MPL files must be offered,
  notices ship.
- **Size and build.** A release binary of about 153 MB, about 2,000 crates, 10–15 GB of build space, cmake and clang
  for SpiderMonkey ([servo crate exploration](https://github.com/simonw/research/tree/main/servo-crate-exploration))
  [read].
- **Rendering.** WebRender over a GL context; `SoftwareRenderingContext` (software GL, "bad performance") or an
  offscreen context, read back with `read_to_image` as an RGBA image
  ([SoftwareRenderingContext](https://doc.servo.org/servo/struct.SoftwareRenderingContext.html),
  [OffscreenRenderingContext](https://doc.servo.org/servo/struct.OffscreenRenderingContext.html)) [search]. That is
  the pixel class (§2.1). On Windows GL is expected to go through ANGLE (UNVERIFIED).
- **Input, IME, Proton.** Not established for an embedder; no Wine evidence found.
- **Verdict.** No. Heavier than Ultralight, less compatible than CEF, and still changing monthly.

### 4.7 Other options considered

- **litehtml** (BSD-3-Clause, with gumbo under Apache-2.0): parses HTML and CSS and lays them out, and the host draws
  through a `document_container` callback interface ([litehtml](https://github.com/litehtml/litehtml)) [read]; Rust
  bindings `litehtml` 0.2.6, MIT ([litehtml-rs](https://github.com/franzos/litehtml-rs)). It could draw an HTML
  subset straight into ImGui draw lists, within O25. No JavaScript, CSS "not yet fully compatible" [read], young
  bindings. A curiosity worth one spike only if HTML templates in game become a goal without O25 changing.
- **Sciter**: proprietary binary, free for commercial and non-commercial use with an About-box credit
  ([Sciter EULA](https://github.com/c-smile/sciter-sdk/blob/master/license.htm)) [read]; Rust bindings `sciter-rs`
  0.5.58 (MIT). Its windowless output is pixels (§2.1) (LIKELY). One vendor, non-standard HTML and script dialect.
  No.
- **NoesisGUI**: XAML, proprietary; even the free indie tier needs a licence key
  ([Noesis forum](https://www.noesisengine.com/forums/viewtopic.php?p=11050)) [search]. Not HTML; no.
- **Coherent Gameface**: commercial studio middleware (LIKELY); not available to a free mod. No.
- **Slint** (1.18.1): a Rust UI toolkit with its own language, GPL-3.0, royalty-free (with attribution) or commercial
  licences (UNVERIFIED in this pass); its software renderer is the pixel class. No.
- **egui** (0.37): MIT OR Apache-2.0 (LIKELY); its tessellated meshes would map onto ImGui geometry, but its look is
  no closer to Grace than ImGui's and it would be a second immediate-mode toolkit. No.
- **A separate window over the game** (a WebView2 or CEF window): fights exclusive fullscreen, focus and input
  capture, and under gamescope on the Deck a second top-level window is not composited over the game (LIKELY). No.
- **The Steam overlay browser**: the player opens the dashboard URL in Steam's own overlay browser (Shift+Tab), or
  EldenNet asks Steam to open it (`ActivateGameOverlayToWebPage`, with a modal mode)
  ([GameMaker Steamworks wiki](https://github.com/YoYoGames/GMEXT-Steamworks/wiki/overlay),
  [Facepunch wiki](https://wiki.facepunch.com/steamworks/SteamFriends.OpenWebOverlay)) [search]. No renderer of
  ours, and it would show the full web UI in game. Whether it can reach 127.0.0.1 under Windows, Proton and gamescope
  is unknown, and calling the Steam API from our DLL is a new kind of call for EldenNet. Worth a five-minute S4 test
  by hand; any code path would be an owner question.

## 5. Comparison

| Option | Licence | Shipped size | Menu performance | Input and IME | Proton and Deck | Maintenance | O25 today | Browser copy | Rank |
|---|---|---|---|---|---|---|---|---|---|
| Dear ImGui, Grace theme | Already in (MIT, BSD-2); OFL fonts | ~1–3 MB of fonts and textures (EST) | Vertices only; < 1 ms (EST) | Good keys and pad; OS IME window, positioned by us; fix `WM_CHAR` | The path EldenNet already depends on | Our widgets and layout | Fits | Schema, protocol, tokens shared; views separate | **1** |
| Ultralight, CPU mode | Proprietary Free V2 (§2.3, §3.3, §4.1–4.7) | ~57 MB | Raster on its own thread; full or dirty uploads; 60 fps cap; JS interpreter | Own composition code; pad ours | No public evidence; CPU mode avoids GPU interop | SDK pin (2.0 beta), engine invariants, licence | Needs the small hudhook patch | Same bundle (WebKit 615) | **2** (if the owner wants HTML in game) |
| RmlUi through ImGui geometry | MIT, FreeType | A few MB (EST) | CPU layout, cached geometry | `TextInputHandler`, spatial navigation | As ImGui | Our FFI and bridge, a second toolkit | Fits (basic features) | Low (RML, not HTML) | **3** |
| litehtml through ImGui geometry | BSD-3, Apache-2.0 | < 1 MB (EST) | Light | Ours | As ImGui | Young bindings, our container | Fits | HTML subset, no JS | 4 (watch) |
| CEF OSR | BSD-3 plus Chromium credits | ~250–300 MB (EST) | V8 JIT; extra processes; full uploads | Best IME | Mixed Wine record | Chromium cadence | Both modes blocked | Perfect | 5 |
| Ultralight, D3D12 driver | As above | ~57 MB plus our driver | Best GPU efficiency | As CPU mode | vkd3d interop risk | Our driver against a changing interface | Needs the large change | Same bundle | 6 |
| Blitz | MIT or Apache-2.0, Stylo MPL | Unmeasured | Unknown | Undocumented | Unknown | Beta churn | Pixels or GPU | Dioxus, not TS | 7 (watch) |
| Servo | MPL-2.0 | ~153 MB | Software GL slow; read-back | Unknown | Unknown | Monthly breaking | Pixels | Partial | 8 |
| Sciter, Noesis, Gameface, Slint, egui, a separate window | Various | — | — | — | — | — | — | — | Not credible here |

## 6. Hardening the local web server for the browser dashboard

The browser host is a small HTTP and WebSocket server inside EldenNet. Every rule below is cheap; together they
stop the known attacks on local servers (DNS rebinding, CSRF, cross-site WebSocket hijacking, other local
processes). Precedent: Ollama's DNS-rebinding flaw gave a web page full API access through a loopback-only server
until Host validation was added (CVE-2024-28224,
[NCC Group advisory](https://www.nccgroup.com/research/technical-advisory-ollama-dns-rebinding-attack-cve-2024-28224/));
a 2026 note describes the same class against a local model backend
([CSA note](https://labs.cloudsecurityalliance.org/wp-content/uploads/2026/09/CSA_research_note_nemoclaw_dns_rebinding_model_poisoning_20260905-csa-styled.pdf))
[search].

1. **Off by default; opt-in.** GraceUI DESIGN G4 recommends this: the player turns the dashboard on in the in-game menu.
   Magelight moved network access to opt-in before 1.0 because tightening later breaks users (ML §7).
2. **Bind 127.0.0.1 only** (and `::1` if offered), never `0.0.0.0`, no remote option in v1. A configurable port with
   a fixed default; if the port is taken, fail closed with one toast and a log line, never fall back to another
   interface. Stop the server when the game exits.
3. **A per-session token.** 256 bits from the OS random source, new at every game start, never written to disk or
   logs, compared in constant time.
   - Hand it over in the URL **fragment** (`http://127.0.0.1:PORT/#t=...`), which browsers do not send to the
     server or in `Referer`; the page moves it into memory or `sessionStorage` and removes it from the address bar
     with `history.replaceState`.
   - Send it as an `Authorization: Bearer` header on every API request. Browsers cannot set headers on a WebSocket,
     so authenticate the socket with a short-lived one-time ticket fetched over an authenticated POST, or a first
     message within 2 s; never in the WebSocket URL's query string, which ends up in history and logs.
   - Other local processes can reach 127.0.0.1; the token is what keeps them out.
4. **Host check (DNS rebinding).** Accept a request only if `Host` is exactly `127.0.0.1:PORT`, `localhost:PORT` or
   `[::1]:PORT`; answer anything else with 403 or 421 before any routing
   ([NCC: preventing DNS rebinding](https://github.com/nccgroup/singularity/wiki/Preventing-DNS-Rebinding-Attacks),
   and the same allowlist in the MCP SDK's [host-header middleware](https://ts.sdk.modelcontextprotocol.io/v2/api/@modelcontextprotocol/express/middleware/hostHeaderValidation.html))
   [search]. A rebound page's `Host` names the attacker's domain, so this one check defeats rebinding.
5. **Origin check (CSRF and WebSocket hijacking).** For every state-changing request and **every WebSocket upgrade**,
   require `Origin` to equal the server's own origin; reject a missing `Origin` on upgrades. WebSocket handshakes are
   not covered by the same-origin policy, and RFC 6455 tells servers to check `Origin`
   ([RFC 6455 §10.2](https://www.rfc-editor.org/rfc/rfc6455#section-10.2);
   [CSWSH](https://pentest-tools.com/blog/cross-site-websocket-hijacking-cswsh)).
6. **Fetch Metadata as defence in depth.** Refuse API requests whose `Sec-Fetch-Site` is present and not
   `same-origin` ([Fetch Metadata](https://web.dev/articles/fetch-metadata);
   [OWASP CSRF cheat sheet](https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html)).
7. **No ambient credentials.** No cookies, so a cross-site form has nothing to ride on. If a cookie is ever needed:
   host-only, `HttpOnly`, `SameSite=Strict`, plus the header token.
8. **Request shape.** Static files by GET only. API calls by POST with `Content-Type: application/json` (a plain HTML
   form cannot send it), a size cap, a schema check and a protocol version. No CORS headers at all, so cross-origin
   reads fail; never answer a Private Network Access or Local Network Access preflight positively.
9. **Response headers.** `Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self';
   img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`, plus
   `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `Cross-Origin-Opener-Policy: same-origin`,
   `Cross-Origin-Resource-Policy: same-origin`, and `Cache-Control: no-store` on the API. Name the WebSocket's own
   `ws://127.0.0.1:PORT` in `connect-src` if a browser needs it spelled out.
10. **WebSocket hygiene.** Host and Origin checked at upgrade, ticket or first-message authentication, a message size
    cap, a rate limit, one or two connections at most, the bridge protocol's version handshake, identity bound to
    the connection and never taken from a payload, malformed messages dropped with a short log note (ML lesson 23).
11. **Secrets.** API keys are write-only through the API (set, replace, clear; never read back), shown masked.
    Settings payloads are never logged; request lines are logged without query strings or fragments; no token in
    any log or bug report (EldenNet's privacy rules).
12. **TLS.** Not needed on loopback: `http://127.0.0.1` is a potentially trustworthy origin, a secure context
    ([Secure Contexts](https://www.w3.org/TR/secure-contexts/)). A self-signed certificate would only train players
    to click through warnings. NCC recommends TLS everywhere, but its threat here is covered by rules 3–6.
13. **Do not rely on the browser.** Chrome 142 started asking the user before a public site may reach loopback or the
    local network (Local Network Access), with the permission later split into loopback and local-network parts
    ([blink-dev](https://groups.google.com/a/chromium.org/g/blink-dev/c/lRnFRIfzDMU),
    [overview](https://blog.openreplay.com/chrome-local-network-access-lna-permission/)) [search]. Firefox and others
    differ, and a player can click Allow. The server's own checks are the defence.
14. **The in-game page is untrusted input too.** Whatever renderer is chosen, EldenNet validates in-game messages
    exactly as it validates the browser's (ML lesson 23).

## 7. Ranked recommendation

1. **v1: Dear ImGui with a native Grace theme in game, the full web UI in the browser.** It fits O25 exactly, adds
   no licence and no binary, uses the overlay path EldenNet already has to make work on Proton and the Deck, needs
   no texture uploads, and gives pad navigation out of the box. Its losses are typography (no kerning or OpenType
   features) and authoring speed; the Grace theme was designed flat and still, so most of it carries over (§4.1).
   Make both views read one schema and one bridge, and compile one token source to CSS and to an ImGui style, so the
   two copies cannot drift. Fix the `WM_CHAR` panic and plan the atlas rebuild and IME positioning (§2.4) in
   EldenNet's own overlay work, whatever is chosen.
2. **Later, owner-gated: Ultralight in CPU mode,** if the owner wants the same HTML pages in game. It needs three
   owner decisions: accept the Ultralight licence (§4.2.1), approve a minimal reviewed hudhook patch for region
   texture uploads (§2.3, a change to O25), and accept about 57 MB. Before any promise, a spike in a throwaway
   branch, never shipped, must show on Windows, Proton and the Deck: the page thread, dirty-rectangle uploads,
   correct alpha, 1440p and 800p costs, fonts with no system fonts, and the IME. A D3D12 GPU driver is not
   recommended for a menu.
3. **Fallback: RmlUi drawn through ImGui geometry,** if the ImGui Grace theme proves too plain and O25 stays as it is.
   MIT, small, within O25 for its basic features, but its documents are not the browser's pages.
4. **Watch:** litehtml through ImGui geometry (HTML templates without JavaScript, within O25); Blitz at 1.0.
5. **Not recommended:** CEF (size, processes, Wine record, O25), Servo (size, churn), Sciter, NoesisGUI, Coherent
   Gameface, Slint, egui, a separate window.

Candidate Open rows for GraceUI's DESIGN (with the recommendation first). GraceUI DESIGN takes the first as Open row G1 and
settles the other three in its decision log (D28, D28 and D30):
- **G1 renderer:** ImGui Grace in game, web UI in the browser (recommended) / Ultralight CPU behind a hudhook patch /
  RmlUi through ImGui / CEF.
- **O25 texture patch:** keep hudhook at exactly 0.9.3 with no patch (recommended for v1) / approve a minimal region
  upload patch for an Ultralight spike / move to a newer hudhook (none fixes it today).
- **`imgui-tables-api` feature:** leave off and lay tables out ourselves (recommended) / enable it (an O25 change).
- **Steam overlay browser:** test by hand in S4 only (recommended) / add an "open in Steam overlay" button that calls
  the Steam API.

## 8. Gaps and what S4 or a spike must measure

- The `WM_CHAR` panic in game: Windows emoji panel, the Steam keyboard on the Deck, Proton.
- ImGui CPU time for a dense Grace page at 1440p and 800p; atlas sizes for the type scale at 75–150%.
- IME window positioning through IMM under Windows and Proton.
- Whether hudhook's missing `COPY_DEST` barrier and blocking wait matter for the one-time uploads EldenNet makes
  (debug layer on Windows, vkd3d-proton on the Deck).
- Ultralight 2.0's stable date, its free-edition JIT status and its CPU-mode cost, read at the source
  (`ultralig.ht` was not reachable when this note was written); Article 9 of the licence; any Wine or Proton report.
- CEF 64-bit sizes and OSR inside a Proton game, if CEF is ever reconsidered.
- The Steam overlay browser and 127.0.0.1 under Windows, Proton and gamescope.
- RmlUi's render-interface split (which 6.x features need layers or shaders), measured in a bridge spike, if the
  fallback is taken.

## 9. Sources

EldenNet and lead studies: DESIGN O25, §2.6 R12, §8.1, §12; ER02 §7.1; NPC §10.1, §10.7; ARCH §2.3, §2.5, §12.1;
`magelight.md`; `severactions-look.md`.

Read in published packages (crates.io): hudhook 0.9.3 (`src/lib.rs`, `src/renderer/mod.rs`, `src/renderer/input.rs`,
`src/renderer/pipeline.rs`, `src/renderer/backend/dx12.rs`); imgui 0.12.0 (`src/lib.rs`); imgui-sys 0.12.0
(`build.rs`, `third-party/imgui-master/imgui/imgui.h`, `imgui.cpp`, `imgui_draw.cpp`); ul-next 0.5.4; cef 156.3.0;
servo 0.7.0; blitz-dom 0.3.0-beta.4; litehtml 0.2.6; sciter-rs 0.5.58. Ultralight Free License Agreement V2 in the
Magelight 1.7.1 release.

Web:
- hudhook `main`, texture upload: https://raw.githubusercontent.com/veeenu/hudhook/main/src/renderer/backend/dx12.rs
- hudhook README (0.8.0 copy): https://rust-digger.code-maven.com/src/hudhook-0.8.0/README.md
- Elden Ring practice tool: https://github.com/veeenu/eldenring-practice-tool
- Dear ImGui 1.92 fonts: https://sources.debian.org/src/imgui/1.92.2b%2Bds-1/docs/FONTS.md
- Ultralight 2.0 announcement: https://ultralig.ht/blog/ultralight-2-0-out-now
- Ultralight 2.0.0-beta.2 changelog: https://ultralig.ht/docs/2.0/changelog-2-0-0-beta-2
- Ultralight 1.4 to 2.0 migration: https://ultralig.ht/docs/2.0/migrating-from-1-4-to-2-0.md
- Ultralight GPU renderer overview: https://ultralig.ht/docs/2.0/gpu-renderer-overview
- Ultralight, integrating into a game engine: https://ultralig.ht/docs/2.0/integrating-into-a-game-engine
- Ultralight 1.4: https://ultralig.ht/blog/ultralight-1-4-out-now
- Ultralight supported web features: https://github.com/ultralight-ux/Ultralight/wiki/Supported-Web-Features
- Ultralight custom GPUDriver: https://docs.ultralig.ht/docs/using-a-custom-gpudriver
- AppCore Windows sources: https://github.com/ultralight-ux/AppCore/tree/master/src/win
- Ultralight SDK mirror tag 2.0.0-beta.2: https://github.com/Ayydxn/Ultralight-SDKs/releases/tag/2.0.0-beta.2
- ul-next: https://github.com/Amjad50/ul-next
- Prisma UI on Ultralight: https://www.nexusmods.com/fallout4/articles/6528
- CEF general usage: https://chromiumembedded.github.io/cef/general_usage and https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage
- CEF accelerated paint info: https://cef-builds.spotifycdn.com/docs/135.0/structcef__accelerated__paint__info__t.html
- CEF OSR dirty rects and frame rate (mirror commit): https://gitea.it/bcarrella/cef/src/commit/dea4daffd71be5262cf42fa8f3ba6a93d0465c9f
- cef-mixer: https://github.com/twobrainsgmbh/cef-mixer
- cef-rs: https://github.com/tauri-apps/cef-rs
- libcef size: https://github.com/cefsharp/CefSharp/discussions/5181
- CEF redistributable files: https://third-party-mirror.googlesource.com/cef/+/8265caad76429c18c91b1cfabe571852a67e0e5f/src/tools/distrib/win/README.redistrib.txt
- WineHQ bug 52513: https://bugs.winehq.org/show_bug.cgi?id=52513
- RmlUi: https://github.com/mikke89/RmlUi and https://raw.githubusercontent.com/mikke89/RmlUi/master/changelog.md
- Blitz: https://github.com/DioxusLabs/blitz, https://github.com/DioxusLabs/blitz/issues/119, https://blitz.is/about, https://lib.rs/crates/blitz-html
- Dioxus 0.7: https://dioxuslabs.com/blog/release-070/
- vello_cpu: https://crates.io/crates/vello_cpu
- Servo 0.1.0: https://servo.org/blog/2026/04/13/servo-0.1.0-release/
- Servo crate exploration: https://github.com/simonw/research/tree/main/servo-crate-exploration
- Servo rendering contexts: https://doc.servo.org/servo/struct.SoftwareRenderingContext.html and https://doc.servo.org/servo/struct.OffscreenRenderingContext.html
- litehtml: https://github.com/litehtml/litehtml; litehtml-rs: https://github.com/franzos/litehtml-rs
- Sciter EULA: https://github.com/c-smile/sciter-sdk/blob/master/license.htm; rust-sciter: https://github.com/sciter-sdk/rust-sciter
- NoesisGUI licensing: https://www.noesisengine.com/forums/viewtopic.php?p=11050 and https://www.noesisengine.com/trial/
- Steam overlay web page: https://github.com/YoYoGames/GMEXT-Steamworks/wiki/overlay and https://wiki.facepunch.com/steamworks/SteamFriends.OpenWebOverlay
- DNS rebinding: https://github.com/nccgroup/singularity/wiki/Preventing-DNS-Rebinding-Attacks
- Ollama CVE-2024-28224: https://www.nccgroup.com/research/technical-advisory-ollama-dns-rebinding-attack-cve-2024-28224/
- CSA note (2026): https://labs.cloudsecurityalliance.org/wp-content/uploads/2026/09/CSA_research_note_nemoclaw_dns_rebinding_model_poisoning_20260905-csa-styled.pdf
- MCP SDK host validation: https://ts.sdk.modelcontextprotocol.io/v2/api/@modelcontextprotocol/express/middleware/hostHeaderValidation.html
- RFC 6455 §10.2: https://www.rfc-editor.org/rfc/rfc6455#section-10.2
- Cross-site WebSocket hijacking: https://pentest-tools.com/blog/cross-site-websocket-hijacking-cswsh
- OWASP CSRF: https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html
- Fetch Metadata: https://web.dev/articles/fetch-metadata
- Secure Contexts: https://www.w3.org/TR/secure-contexts/
- Chrome Local Network Access: https://groups.google.com/a/chromium.org/g/blink-dev/c/lRnFRIfzDMU and https://blog.openreplay.com/chrome-local-network-access-lna-permission/
- Chrome Private Network Access: https://developer.chrome.com/blog/private-network-access-update
