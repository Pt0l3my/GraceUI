# Study: shared UI hosts for game mods, and what they teach GraceUI as a framework for any Elden Ring mod

Status: research note for GraceUI's framework proposal ([proposals/0001-ui-framework.md](../proposals/0001-ui-framework.md)),
in our own words; no code copied. Written 2026-11-08, after the owner widened GraceUI's scope (G14). Cited in GraceUI's
documents as **FW**; its findings (§7) are taken up in the proposal, which puts the questions G15 onwards.

## 0. Sources, method and labels

- **Two kinds of source.** Elden Ring's own projects were read for this note, in untrusted clones or copies outside
  any repository. Skyrim's three UI hosts were researched by the Skyrizz design session, which answered GraceUI's
  questions by message on 2026-11-08 after three research passes and a fact check against its sources; those points
  are secondhand here.
- **Key to sources read for this note** (each at the commit given; licence from the project's own files):

| Key | Project | Read at | Licence | Read here |
|---|---|---|---|---|
| HH | veeenu/hudhook | tag `0.9.3` = `72e1cc0` (2026-09-08) | MIT (`LICENSE`); vendored MinHook BSD-2 | `README.md`, `Cargo.toml`, `src/lib.rs`, `src/hooks/dx12.rs`, `vendor/minhook/src/hook.c`, by `grep` |
| IRS | imgui-sys 0.12.0, the crate hudhook 0.9.3 builds on | the published crate archive | not re-read | `imgui.h` inside the archive, through a `python3 -I` reader |
| NU | Flammrock/ERNativeUI | `167733c` (2026-10-22) | MIT (`LICENSE.txt`) | `README.md`, `docs/how-it-works.md`, `include/ernativeui/` |
| EO | soarqin/EROverlay | `afab7e1` (2026-10-29) | MIT (`LICENSE`) | `README.md`, `AGENTS.md`, `src/api.h`, `src/plugin.cpp`, `src/d3drenderer.cpp`, `src/minimap/`, `docs/writing-plugins.md`, `deps/imgui/imgui.h` |
| EM | Banon-Labs/er-mods-rs | `c107d31` (2026-10-28) | no licence file at its top level: all rights reserved, so described here and never reused | `scripts/me3-dll-conflicts.toml`, `crates/er-build-watermark-core/src/overlay_host.rs` |
| MFG | MapForGoblins (DLL fork) | `60a63f2` | `LICENSE.txt`, first lines only | `docs/memory/linux.md`; its consumer header as EO ships it |
| ME2 | soulsmods/ModEngine2 | `4f701cc` (2024-07-19, its last commit) | MIT (`LICENSE-MIT`) | `src/modengine/extension_set.cpp` |
| PT | veeenu/eldenring-practice-tool | `cf2e638` (2026-10-23) | AGPL-3.0 (`LICENSE`) | `README.md`, `Cargo.toml` |
| YO | ndahn/yonder | the copy on disk | not read | `CONTRIBUTING.AI.md` only |
| MLG | Severause/MagelightUI | `05d05d8` (2026-10-31) | MIT (`LICENSE`) | `api/MagelightUI_API.h` lines 1–20 only |

- **The Skyrizz session's sources**, cited in its own terms: *Magelight* is Severause/MagelightUI at `05d05d8` (at that
  commit its 1.5.0 to 1.7.1 entries are marked Unreleased); *Meridian* is heathbrownkeyworks/MeridianUI at `5707877`;
  *PrismaUI example* is PrismaUI-SKSE/example-skse-plugin at `dbb1dd3`; *PrismaUI framework* is PrismaUI-SKSE/framework
  at `aaef071`; *Skyrizz* is the Skyrizz repository (branch `feat/skyrizz-lite`), whose decisions are d-numbers. Each such
  point ends with its source and "(via the Skyrizz session, 2026-11-08)". The session could not reach PrismaUI's web
  documentation, so what only that would settle is not known.
- **Earlier passes of this job.** A landscape pass on 2026-11-08 also read hudhook's issues #196, #234 and #256, GitHub
  search results and press coverage of Nexus Mods' AI policy. Points from it that were not re-read here are labelled
  LIKELY and say so. nexusmods.com could not be fetched.
- **Method.** Read with `grep`, `sed`, `git log` and `python3 -I` only. Nothing was built, installed or run. No code of
  me3 or fromsoftware-rs was read (EldenNet O47, O48); facts about me3 come from EldenNet's ER02, second-hand.
- **Labels.** **FACT**: read for this note in the primary source cited. **LIKELY**: good secondary evidence (the
  Skyrizz session's fact-checked answer, the landscape pass) or a strong inference. **SPECULATION**: a reasoned guess.
  Every point from the Skyrizz session is at most LIKELY here, because it was not re-read, except where this note read
  the same file itself and says so.

## 1. Skyrim's three UI hosts, as the Skyrizz session reports them

### 1.1 What a consuming mod writes

- **Magelight** offers three tiers: a manifest-only mod with no DLL, Papyrus, or a C ABI for a DLL; pages talk through
  `window.magelight.send/on` and are loaded from an absolute `file:///` path (Magelight `README.md:20-22`,
  `docs/SDK.md:14-30`, `docs/CPP.md:38-40`) (via the Skyrizz session, 2026-11-08).
- **Meridian**: one SKSE DLL built against MIT headers; pages served as `mod://<Mod>/file` from the mod's own folder;
  arguments arrive as JSON text; callbacks carry no user pointer, so Skyrizz generated 96 template slots (Meridian
  `docs/MeridianUI-AuthorGuide.md:25-29`, `:344-356`, `:517-578`; Skyrizz `src/plugin/UiHostMeridian.cpp:63-85`)
  (via the Skyrizz session, 2026-11-08).
- **PrismaUI**: a mod copies the header and puts its page in `PrismaUI/views/<Name>/`; the interface is a C++ abstract
  class; a view starts shown, and DOM-ready can arrive before `CreateView` returns (PrismaUI example
  `src/PrismaUI_API.h:1-3`, `:35-117`; PrismaUI framework `src/PrismaUI/ViewManager.cpp:44`; Skyrizz
  `src/plugin/UiHostPrisma.cpp:76-87`) (via the Skyrizz session, 2026-11-08).
- **Skyrizz's own layer** runs one page set on all three through a thin host layer: one page folder that the release
  copies twice, a JS bridge in two styles, and a JSON envelope `{id, name, args}` whose unanswered requests time out
  after 120 s (Skyrizz `src/core/UiHost.h:20-33`, `:130-139`; `data/MeridianUI/Skyrizz/bridge.js:49`, `:99`) (via the
  Skyrizz session, 2026-11-08).

### 1.2 Finding the host, and versions

- **Magelight**: `GetModuleHandleW`, then `GetProcAddress("Magelight_RequestApi")`, then a request for version 4; each
  slot is read only where the host's version says the table holds it, a missing core slot passes the host over and a
  missing feature slot turns off one feature (Skyrizz `src/plugin/UiHostMagelight.cpp:67-110`, `:432-443`) (via the
  Skyrizz session, 2026-11-08). Its header states the rules itself: "C ABI only; every struct starts with its members
  in declaration order and only ever APPENDS; enums never renumber; an unknown version returns null; v-N stays served
  for at least two minor host versions after v-N+1 ships; a breaking change means a new DLL name and a new export"
  (MLG `api/MagelightUI_API.h:12-16`). **FACT** (read for this note).
- **Meridian**: an SKSE message handshake with versions packed as `major*100000+minor`; same major and a minor no older
  than the consumer's headers; on a mismatch no callback comes, only a log line, documented as "unavailable" (Meridian
  `src/UIPlatform/MeridianUIAPI/API.h:69-112`, `Version.h:48-57`; `docs/MeridianUI-AuthorGuide.md:121-127`) (via the
  Skyrizz session, 2026-11-08).
- **PrismaUI**'s API carries no version (PrismaUI example `src/PrismaUI_API.h:22-23`, `:108-117`) (via the Skyrizz
  session, 2026-11-08).

### 1.3 Several mods on one host

- **Who wins the input**: on Magelight the first holder, others get Busy or queue with a flag; on Meridian and PrismaUI
  the last caller, which deposes the holder; PrismaUI's focus call returns true for any valid view, even a hidden one
  where it then does nothing (Magelight `docs/CPP.md:159-161`; Meridian `docs/MeridianUI-AuthorGuide.md:704-707`;
  PrismaUI framework `src/PrismaUI/ViewManager.cpp:238-325`) (via the Skyrizz session, 2026-11-08).
- **No negotiation across hosts**: "neither host sees the other's focused view" (Magelight
  `docs/PRISMAUI_MIGRATION.md:37-43`) (via the Skyrizz session, 2026-11-08).
- **Draw order across hosts**: something drawn at Present, "an ImGui overlay for one", can cover Magelight's pages, and
  Magelight drew over Meridian's pages in Skyrizz's in-game test, so Skyrizz's developer page became a window
  (Magelight `docs/TROUBLESHOOTING.md:321-332`; Skyrizz d289) (via the Skyrizz session, 2026-11-08).
- **Keys**: while a page holds UI mode, Magelight makes polled keys read as up for other plugins and gives each hotkey
  to whoever registers it first; Skyrizz's own hotkey stands aside for menus, the console and text entry (Magelight
  `docs/CPP.md:162-166`; Skyrizz `src/plugin/Pages.cpp:589-617`, d288) (via the Skyrizz session, 2026-11-08).
- **Escape**: on Magelight it leaves UI mode unless the page captures it, and Skyrizz captures it only after DOM-ready,
  so a page that never loads cannot trap the player (Magelight `docs/CPP.md:306-316`; Skyrizz
  `src/plugin/UiHostMagelight.cpp:185-204`) (via the Skyrizz session, 2026-11-08).

### 1.4 Frame path, look and distribution

- **Frame path**: Magelight composites at Present or in the game's UI pass, because frame generation drops what is
  drawn at Present; under an upscaler Meridian's first frame reported too small a screen, so Skyrizz reads the size
  from the swap chain each time it opens (Magelight `docs/TROUBLESHOOTING.md:425-426`; Skyrizz `CHANGELOG.md:196`)
  (via the Skyrizz session, 2026-11-08).
- **Look**: no host imposes a theme or a component kit; Skyrizz built about 10,000 lines of its own HTML, JS and CSS,
  and the session lists what a shared kit should give: a bridge with ids, timeouts and buffering, one back stack for
  keyboard, pad and host, an in-page modal with a focus trap, pad focus navigation, tokens that also theme what the
  host draws, bundled licensed fonts, named sounds, one placement contract and a stated web-feature floor (Magelight
  `docs/SDK.md:1-30`; Skyrizz `data/MeridianUI/Skyrizz/`, `docs/DEPENDENCIES.md:48-54`, `:76-85`) (via the Skyrizz
  session, 2026-11-08).
- **Distribution**: Magelight documents the mod-manager "two copies" problem, where a bundled copy and a standalone
  copy fight file by file, and a minimum-host field that shows a "too old" page; Meridian's bug-report checklist asks
  whether another UI framework is installed (Magelight `docs/DISTRIBUTION.md:7-63`, `:105-113`; Meridian
  `README.md:254-261`) (via the Skyrizz session, 2026-11-08).
- **For modders**: Magelight ships guides, a header-only wrapper, npm and React packages, a scaffolder, four example
  mods, a dev mode with live reload and a desktop harness that renders a page without the game; Meridian has author
  guides, test gates and an integration sample; Skyrizz tests its pages in headless Chromium against a mock host and
  fixtures written by its C++ core, failing on console errors, overflow or any fetch (Magelight `README.md:24-29`,
  `:101-122`, `tools/desktop-harness/README.md:1-5`; Meridian `README.md:103-118`; Skyrizz `tools/pages/test.js:1-30`)
  (via the Skyrizz session, 2026-11-08).

### 1.5 Licences and AI clauses

- Magelight's host, header, SDK, examples and docs are MIT; a mod that bundles it ships Ultralight's runtime under
  Ultralight's EULA (Magelight `README.md:145-162`) (via the Skyrizz session, 2026-11-08). MLG's `LICENSE` reads "MIT
  License, Copyright (c) 2026 Severause" (**FACT**, read for this note).
- Meridian is GPL-3.0-or-later with a Modding Exception and a Linking Exception; its API headers alone are MIT, with an
  SPDX line (Meridian `LICENSING.md:5-13`, `Version.h:1`) (via the Skyrizz session, 2026-11-08).
- PrismaUI's framework carries its own "Prisma UI License" (non-commercial use or a revenue and funding cap; only the
  original may be distributed); its header states no licence, though it says to copy it into your project, so Skyrizz
  rewrote the interface in its own words (PrismaUI framework `LICENSE.md`; PrismaUI example `src/PrismaUI_API.h:2`;
  Skyrizz d350) (via the Skyrizz session, 2026-11-08).
- No AI clause was found in any of the three repositories' licences or READMEs, and how each project received Skyrizz is
  not recorded (via the Skyrizz session, 2026-11-08).

### 1.6 The session's own conclusions

The session ends with a suggested ABI of ten points (its §6) and twelve points to copy and eight to avoid (its §10),
drawn from the above. The proposal takes up each one and says whether GraceUI adopts, adapts or rejects it
(proposal §4.15). It also records what none of its sources covers: anything specific to Elden Ring, hudhook,
ImGui-drawn pages, ModEngine2, me3 or the Steam Deck (via the Skyrizz session, 2026-11-08).

## 2. Elden Ring's shared UI and overlay mods

| Project | What it offers other mods | How a mod reaches it | Draws with | Licence | Agent files |
|---|---|---|---|---|---|
| ERNativeUI (NU) | Pages, rows and dialogs merged into the game's own menus | `GetModuleHandleW` then `GetProcAddress("ERUI_GetApi")` (`ERNativeUI.hpp:1325`) | The game's menu system: "This is not an ImGui or DirectX overlay" (`README.md:24`) | MIT | None found (LIKELY) |
| EROverlay (EO) | Plugin DLLs drawing Dear ImGui in one shared frame, plus a size-gated native API | The core loads `overlays/*.dll` with `LoadLibraryW` (`src/plugin.cpp:45`) | Dear ImGui 1.92.6 (`deps/imgui/imgui.h:32`) through in-engine hooks, "instead of DXGI hooks" (`README.md:16`) | MIT | `AGENTS.md` |
| er-mods-rs overlay host (EM) | Internal to its own suite: one elected ImGui host, guests register draw callbacks | A named mutex elects the host; guests call an export on whichever module won (`scripts/me3-dll-conflicts.toml:1575-1577`) | hudhook 0.9.x | None: describe only | `AGENTS.md` and others |
| MapForGoblins (MFG) | A read-only map-data API that EO's minimap uses | `GetModuleHandleW` and one export returning NULL for another ABI (EO `src/minimap/compat/mfg_map_api.h:10`) | Its own ImGui panel through its own Present hooks (LIKELY, landscape pass) | First lines read only | `AGENTS.md` and others |
| ModEngine2 (ME2) | A loader-side extension interface | A DLL exporting `modengine_ext_init` (`src/modengine/extension_set.cpp:12`) | — | MIT | None found (LIKELY) |
| Practice tool (PT) | Nothing to other mods: a standalone overlay | — | hudhook 0.9.3 with `dx12` and `inject` (`Cargo.toml:37`) | AGPL-3.0 | — |

All cells are FACT from the files cited, except where marked.

- **ERNativeUI is one host for many clients (FACT).** "One process-wide `ERNativeUI.dll` owns the game hooks"
  (`README.md:20`); "There should be exactly one `ERNativeUI.dll` in the process" (`docs/how-it-works.md:17`);
  clients link no import library (`README.md:42`, `:255`); its connect call "does not silently downgrade to an older
  API" (`docs/how-it-works.md:49`); "Host and committed client DLLs must remain loaded until process exit"
  (`README.md:283`); and it asks players to "Use a release that names the installed Elden Ring executable after every"
  game update (`README.md:272`). It was live-tested beside Seamless Co-op and a frame-rate unlocker (LIKELY, landscape
  pass, `docs/COMPATIBILITY.md`).
- **EROverlay shares one ImGui context (FACT).** The core "discovers and loads individual overlay DLLs from the
  `overlays/` folder at runtime" (`README.md:3`); `createRenderer` hands each plugin the arguments "which should be used
  for ImGui::SetCurrentContext() and ImGui::SetAllocatorFunctions()" (`src/api.h:122`); its native API is gated by size
  ("Check `size >= offsetof(EROverlayNativeAPI, member) + sizeof(api->member)`", `docs/writing-plugins.md:230`); "The
  loader maintains backward compatibility" (`README.md:33`). Its minimap looks for the core as `EROverlay.dll` and then
  as `winhttp.dll`, the proxy name its recommended install uses (`src/minimap/dllmain.cpp:18`). It still replaces and
  restores the game window's procedure (`src/d3drenderer.cpp`, `SetWindowLongPtrW`).
- **er-mods-rs measured the conflict and elected one host (FACT).** "Two Hudhook::apply() calls in one process
  double-hook Present, and the second one loses silently", resolved when the module that claims a named mutex "HOSTS the
  imgui context" and every other overlay module registers a draw callback (`scripts/me3-dll-conflicts.toml:1575-1577`);
  two MinHook instances meant "whichever installed second overwrote the other's trampoline" (`:41`). Every guest pins
  the same hudhook, and a tag refuses a mismatched pair; it was bumped as the handoff grew from the context and
  allocators to fonts and textures (`crates/er-build-watermark-core/src/overlay_host.rs:37`, `:155-158`).
- **MapForGoblins' consumer header states the contract (FACT).** "No STL objects, heap ownership or exceptions cross
  the DLL boundary", and "Load order is not fixed: ask again" later (EO `src/minimap/compat/mfg_map_api.h:4`, `:10`).
- **Not found:** a shared host for Elden Ring that serves pages to a browser, a page-description format, or a
  hudhook-based host that third parties are invited to use (LIKELY: the landscape pass's GitHub search and the copies on
  disk; Nexus could not be read). The landscape pass also met several identical, unlicensed "mod menu" repositories with
  inflated star counts (LIKELY lures), so a framework's name and download page are worth protecting (SPECULATION).

## 3. How mods find each other without a script extender

1. **A fixed DLL name and one C export returning a versioned table** (NU `ERUI_GetApi`; MFG's export, NULL on another
   ABI; EO's size-gated native API). A renamed DLL breaks it, which is why EO's minimap tries a second name. FACT, §2.
2. **An export scan over every loaded module** (ME2 calls any module that exports `modengine_ext_init`, FACT §2; EM's
   guests find the host "without knowing which module won", LIKELY, landscape pass). It survives renaming.
3. **A named kernel object to elect one owner** (EM's mutex, FACT §2).
4. **The host loads its plugins** (EO, FACT §2).
5. **A loader-provided interface** (ME2's; me3's equivalents are out of bounds for GraceUI, EldenNet O47).

- **Load order (LIKELY).** ME2 loads its listed DLLs in order before the game's entry code, but mods start their real
  work on worker threads, so list order proves nothing about which hook goes in first (landscape pass, ME2
  `src/main.cpp`; NU `docs/COMPATIBILITY.md:102`). Every design read polls for its peer from a worker with a bounded
  wait, or asks again later, and treats absence as "feature off" (NU, MFG; FACT for MFG's header, §2). No design calls
  a peer from `DllMain`.
- **ABI practice (FACT, §2):** plain C structs and function pointers, no STL, exceptions or heap ownership across the
  boundary, an explicit version or size argument, and modules that stay loaded to process exit.

## 4. One hook set per process: hudhook 0.9.3 and Dear ImGui across DLLs

- **hudhook keeps one of everything per copy (FACT, HH).** `apply` stores the hook object with `HUDHOOK.set(self).ok()`
  (`src/lib.rs:386`), so a second object in the same copy is silently not kept; the DX12 render loop is stored with
  `RENDER_LOOP.get_or_init` (`src/hooks/dx12.rs:585`), so a second is ignored; the vendored MinHook answers
  `MH_ERROR_ALREADY_CREATED` for a second hook on the same target (`vendor/minhook/src/hook.c:642`). Its README says
  "Runs on Windows and Wine/Proton" (`README.md:14`). It takes `imgui = "0.12"` (`Cargo.toml:63`) and offers an
  `imgui-tables-api` feature (`Cargo.toml:31`), which GraceUI does not build (D28).
- **A second copy in another DLL brings a second MinHook (LIKELY).** Each copy vendors its own MinHook with its own hook
  table, so two copies patch the same DXGI prologues without knowing each other; whether the pair chains or one goes
  silently dead depends on which creates and enables its hooks first (landscape pass, from `hook.c` and
  `trampoline.c`). That matches EM's measurement (FACT, §2) and hudhook issue #234, where the maintainer called
  multiple instances "never a supported use case" needing "a full rewrite" (LIKELY: landscape pass, not re-read).
- **Dear ImGui versions differ (FACT).** imgui-sys 0.12.0 bundles Dear ImGui 1.89.2 (IRS
  `third-party/imgui-master/imgui/imgui.h:25`); EROverlay bundles 1.92.6 (EO `deps/imgui/imgui.h:32`). Sharing one
  context needs the same version and build on both sides (LIKELY), so GraceUI could not join EO's frame, and a guest
  could not share GraceUI's context unless it were built against 1.89.2 exactly.
- **The vertex format is the same in both (FACT).** `ImDrawVert` is `ImVec2 pos; ImVec2 uv; ImU32 col;` in 1.89.2 (IRS
  `imgui.h:2411-2415`) and in 1.92.6 (EO `deps/imgui/imgui.h:3178-3183`, "pos + uv + col = 20 bytes by default",
  `:177`), and `ImDrawIdx` defaults to `unsigned short` in both (IRS `imgui.h:220`; EO `imgui.h:3132`). So draw data,
  unlike a context, can cross between DLLs built against different Dear ImGui versions. GraceUI's G10 vertex lists
  already use this layout (DESIGN §2.9).
- **Only by default (FACT).** Both versions let a build change that shape in its `imconfig.h`: the vertex layout with
  `IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT` ("Override layout with IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT", IRS
  `imgui.h:139`, `:2410`, `:2418`; EO `imgui.h:177`), and the index type ("To use 32-bit indices: override with '#define
  ImDrawIdx unsigned int' in your imconfig.h file", IRS `imgui.h:216-220`; EO `imgui.h:3128-3132`). A guest built that
  way must convert its lists to the default shape, and a host's checker should refuse anything else (LIKELY). How many
  Elden Ring mods change either is not known.
- **Dear ImGui 1.92 can update textures after load (FACT).** It added `ImGuiBackendFlags_RendererHasTextures`, which
  "enables incremental texture updates and texture reloads" (EO `deps/imgui/imgui.h:35`, `:1730`). Under hudhook 0.9.3
  every texture upload leaks a buffer (REN §2.1), so a guest on 1.92 or later must keep that flag off and build its
  atlas once (LIKELY).
- **Other Present compositors stay outside anyone's control (LIKELY).** MFG has its own Present hooks, the practice
  tool forbids "other overlay software, such as RivaTuner Statistics Server, FRAPS or similar" and wants "a
  _completely vanilla_ install" (PT `README.md:25-26`, FACT), and RivaTuner is a known offender for hudhook (issue #196,
  landscape pass). EO hooks no DXGI function, so its path should not collide with hudhook's, though both replace the
  window procedure and neither has tested the other (LIKELY).

## 5. Proton and the Steam Deck

- hudhook "Runs on Windows and Wine/Proton" (HH `README.md:14`); the practice tool, built on hudhook 0.9.3, "fully
  supports Linux and should run on Steam Deck seamlessly", with `WINEDLLOVERRIDES="dinput8=n,b" %command%` as the
  launch option (PT `README.md:72`, `:78`; `Cargo.toml:37`). **FACT.**
- MapForGoblins warns that per-call `ReadProcessMemory` to its own process is "~free on Windows but a **Proton freeze
  cliff**" (MFG `docs/memory/linux.md:22`). **FACT.** GraceUI reads no game memory, but the lesson holds for any
  per-frame system call.
- Magelight dropped a DLL whose imports used API sets that Proton reported missing (Magelight `CHANGELOG.md:412-416`)
  (via the Skyrizz session, 2026-11-08). A short reviewed import list keeps that risk small (LIKELY).
- On a Deck in Gaming Mode there is no browser beside the game, so the in-game view is the Deck's view; a loopback
  listener under Wine is a host socket, reachable from a browser in Desktop Mode (SPECULATION; untested by anyone
  found).

## 6. Licences and the community's view of AI

| Project or venue | Stance | Evidence |
|---|---|---|
| me3 | Forbids all LLM use in its code, issues, discussions and Discord | EldenNet ER02 §3.2 and O47 (second-hand; not re-read) |
| fromsoftware-rs | "NON-AI" licence files forbid machine-learning training with the code | EldenNet ER02 and O48 (second-hand) |
| hudhook, ERNativeUI, ModEngine2 | No AI clause in the licence files read | HH, NU, ME2 `LICENSE*` (FACT); their READMEs and contribution files (LIKELY, landscape pass) |
| EROverlay, er-mods-rs, MapForGoblins | Built with coding agents: agent instruction files at the top level | EO `AGENTS.md`, EM `AGENTS.md`, MFG `AGENTS.md` (FACT) |
| yonder | Accepts AI-assisted pull requests under rules; "If during the review process I get the impression that I'm talking to an AI, I will immediately reject your PR" | YO `CONTRIBUTING.AI.md:10` (FACT) |
| Nexus Mods | Mandatory AI disclosure from 2026-11-03, no ban; labels for AI-generated and AI-assisted work; existing mods have until 2027-01-02 | LIKELY: press coverage read by the landscape pass; Nexus's own page unreachable |
| Skyrim's three UI hosts | No AI clause found | (via the Skyrizz session, 2026-11-08) |

- Reception will be split rather than uniformly hostile (LIKELY): the two core infrastructure projects refuse LLM use
  or training, while several active Elden Ring tools and mods are openly built with agents. A framework is a dependency
  other modders choose, so its reception matters more than a mod's (SPECULATION); its likely adopters are modders who
  already work this way and care about one host, a C ABI, versions and tests (SPECULATION).
- EldenNet's runtime use of language models is a separate axis from AI-assisted code (SPECULATION; not measured).

## 7. Findings for GraceUI

1. **Every shared host found in this game is one module that others call** (NU, EO, EM: FACT, §2); two hudhook copies
   in one process conflict (EM: FACT, §2; the mechanism in §4); independent overlays with their own hooks (MFG's panel,
   the practice tool, small single-purpose ImGui DLLs) coexist only case by case (LIKELY, §2, §4). A framework that
   other mods use without EldenNet needs a host that is present without EldenNet, so the owner's G10 is reopened by G14
   (LIKELY). Proposal G15.
2. **Exactly one hudhook and one MinHook may be in the process** (FACT for the mechanism, LIKELY for the outcome, §4).
   Every other GraceUI user must be a guest through a C ABI: it links no hudhook or MinHook of its own and hands
   GraceUI no ImGui context, though it may build its draw lists with a headless Dear ImGui of its own (finding 3).
3. **Data, not contexts, should cross the boundary** (FACT for the layout, §4): checked draw lists in the default
   `ImDrawVert` layout with 16-bit `ImDrawIdx` let a guest draw with any Dear ImGui version built with those defaults
   (1.89.2 and 1.92.6 checked), or none, without sharing a context, an allocator or a Rust type; a build that overrides
   either converts its lists first.
   EO's shared context would tie every guest to one Dear ImGui build; EM's pinned hudhook ties every guest to Rust and
   one hudhook build. Proposal G16.
4. **Discovery by a fixed name and one export, polled from a worker, with an export scan as the fallback** matches every
   peer read and survives renaming and duplicates (FACT for the practice, §3; SPECULATION for the choice).
5. **The ABI rules are settled practice** (FACT for Magelight's header and the Elden Ring headers; via the Skyrizz
   session for the rest): plain C, append-only tables, sizes first, null for an unknown version, a capability query, a
   support window per major, and an explicit "too old" answer.
6. **Focus is where hosts fail their users** (via the Skyrizz session, 2026-11-08): a silent depose, a true that does
   nothing, or an Ok that only queues. An honest answer refuses and names the holder.
7. **No Elden Ring host serves pages to a browser or reads page descriptions** (LIKELY, §2): GraceUI's declarative pages
   and loopback view are a niche of their own, and the browser view doubles as a desktop harness. Proposal G17.
8. **Bundling causes the two-copies problem** (via the Skyrizz session, 2026-11-08); in this game a second copy of a
   hooking host is also a second MinHook (§4). A single-instance guard and "require, never bundle a private copy" follow.
9. **Licence clarity decides adoption** (via the Skyrizz session, 2026-11-08): MIT stated in every public header, and
   nothing whose terms leave a consuming mod guessing.
10. **AI disclosure is now expected, and some modders will refuse an AI-built dependency** (§6). An honest statement and
    a visibly tested, reviewed framework is what the middle ground asks for (SPECULATION).

## 8. Not known

- Whether a second hudhook DLL chains or goes dead in Elden Ring depends on start timing; no controlled test of both
  orders has been published.
- How ERNativeUI, EROverlay or MapForGoblins behave beside a hudhook overlay; how upscaler, frame-generation or ReShade
  mods for Elden Ring treat an overlay drawn at Present.
- Whether a loopback dashboard works under Proton or on the Deck.
- Nexus's exact wording for AI-assisted code, and whether players' filters hide it.
- How Skyrim's hosts' communities received an AI-assisted consumer (not recorded, via the Skyrizz session, 2026-11-08).

## Sources

- hudhook: https://github.com/veeenu/hudhook, tag `0.9.3` (`72e1cc0`). imgui-sys 0.12.0: crates.io.
- ERNativeUI: https://github.com/Flammrock/ERNativeUI (`167733c`). EROverlay: https://github.com/soarqin/EROverlay
  (`afab7e1`). er-mods-rs: https://github.com/Banon-Labs/er-mods-rs (`c107d31`). MapForGoblins DLL fork (`60a63f2`).
  ModEngine2: https://github.com/soulsmods/ModEngine2 (`4f701cc`). Practice tool:
  https://github.com/veeenu/eldenring-practice-tool (`cf2e638`). yonder: https://github.com/ndahn/yonder.
  Magelight UI: https://github.com/Severause/MagelightUI (`05d05d8`).
- The Skyrizz session's answer of 2026-11-08 and the sources it names: Magelight UI (`05d05d8`), Meridian UI
  https://github.com/heathbrownkeyworks/MeridianUI (`5707877`), PrismaUI's example plugin
  https://github.com/PrismaUI-SKSE/example-skse-plugin (`dbb1dd3`) and framework (`aaef071`), and the Skyrizz repository.
- EldenNet's ER02 (§3, §6, §7.1), O25, O47 and O48; GraceUI's DESIGN §2.9, D28, D56, REN §2.1, ML.
