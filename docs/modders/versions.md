# Versions and distribution

GraceUI is a mod of its own, with its own releases and one folder in the game's `Game` folder (G2). A mod requires it,
names the oldest GraceUI it works with, and relies on a feature only where GraceUI names it. ABI minors are named as
ABI 1.8, protocol minors as protocol 1.20.

## One copy

`Game\GraceUI\`, beside the game's executable, is the only copy (D83, invariant 18):

| Part | What it is |
|---|---|
| `graceui.dll` | The host: the one overlay and its hooks, the ABI, spaces, panels, the menu, the server |
| `graceui.toml` | The version file: GraceUI's version, the ABI's and the protocol's, and every file's size and SHA-256 |
| `web\` | The browser view's files |
| `fonts\` | The OFL faces the in-game menu bakes, each family with its licence file |
| `LICENSE.txt`, `NOTICE.txt`, `README.txt` | GraceUI's licence and the third-party work it carries |
| `user\` | Written at run time: GraceUI's settings and paired devices; in no release, and kept when a release is copied over (D84) |

GraceUI loads only what `graceui.toml` lists, and the server serves only the web files it lists (§2.7).

**A second copy.** The first `GraceUI_RequestApi` in the process takes a per-process mutex, and the copy that takes it
starts the host (D72). A copy loaded from another folder installs no hook, answers null to every request from its
first, and idles. The active copy names it once, by its name relative to the game's folder (or by its file name,
"outside the game's folder"), never a path, in one notice and one log line (D135):

- "A second copy of GraceUI was loaded from another folder and is idle: ‹name›"
- or, when GraceUI's own copy is the one idle because another was asked first: "GraceUI's own copy is idle: another
  copy was started first and is in use, ‹name›. Remove that copy to use GraceUI's own." (D244)

`graceui_sys_find` finds the active copy past an idle one, by its scan of the loaded modules
([Finding GraceUI](abi.md#finding-graceui)); nothing may rely on which copy the name `graceui.dll` finds (D240).

`graceui.dll`'s `DllMain` does nothing: the first request starts the host. A player may list it as a native in their
own loader, which starts nothing until a mod asks (§2.7). It is never unloaded, and does nothing at detach (§2.9).

## Requiring a version

`connect` names the oldest GraceUI the mod works with, its `floor`, packed with `GRACEUI_VERSION_PACK`; 0 for any
([connect](abi.md#connect)).

- **Too old.** A GraceUI older than the floor answers `GRACEUI_TOO_OLD`, with `graceui_version` and `floor` naming
  both. GraceUI shows a notice naming the mod and both versions, "‹Mod› needs GraceUI ‹floor› or newer; this is
  GraceUI ‹version›.", and logs one line. The mod is not connected: it runs headless, its UI off, never an error dialog
  (invariant 15).
- **The rule** (D74), the only one: a mod works with every GraceUI of the ABI major it was built for whose version is
  at least its floor. It calls a slot only if `GRACEUI_API1_COVERS` holds.
- **Caps, not versions.** A mod relies on a feature only if `has_cap` names it, never on GraceUI's version, and goes
  without it otherwise, saying so in its own words if the player would notice ([Caps](abi.md#caps),
  [Features by version](features.md)).
- **Page data.** A space's page data names in `minor` the protocol minor of the GraceUI it names as its floor, not the
  newest. GraceUI refuses page data above its own minor, in its log and in one notice of its own, "‹Mod› needs a newer
  GraceUI for its pages: page data for protocol 1.21; this GraceUI reads up to 1.20." (D264).

`hello-panel` names its floor, the caps it uses, and checks the one it cannot go without:

    GraceUiSysFound found{};
    found.size = sizeof found;
    const graceui::Api api = graceui::Api::find(GRACEUI_SYS_LOAD, GRACEUI_SYS_TIMEOUT_MS, &found);
    graceui::Mod mod;
    mod.id = HELLO_PANEL_ID;
    mod.name = HELLO_PANEL_NAME;
    mod.version = GRACEUI_VERSION_PACK(1, 0, 0);
    mod.floor = GRACEUI_VERSION_PACK(0, 1, 0);
    mod.caps = "panels textures faces";
    GraceUiConnectAnswer answer{};
    if (!api || !api.has_cap(GRACEUI_CAP_PANELS) || api.connect(mod, answer) != GRACEUI_OK) {
      return 0;  // no GraceUI with panels, or nothing can be drawn this session: this mod shows nothing
    }

**The preview.** ABI 1 is a preview until GraceUI 1.1 opens the framework to other mods (G18): until then a mod pins
GraceUI by its release. While it is a preview, a deliberate break of the header updates its baseline in the same
commit, which says why (D132).

## Minors only add

- **The ABI.** Within a major, the table of slots only grows at its end, every struct starts with its size, and a
  constant never changes (`GRACEUI_ABI1_MINOR` only rises). CI refuses a `graceui.h` that breaks any of these against
  ABI 1.0's header, kept as `src/grace-abi/baseline/graceui-abi1-0.h` (D132). An older mod's smaller struct is read as
  it was ([The rules](abi.md#the-rules)). A new major is a new table from the same export, and each major is served for
  at least two minor releases and 12 months after the next (EST, D74).
- **The protocol.** A minor adds optional fields, topics, commands or capabilities and never removes or changes one;
  unknown fields are ignored on both sides. Views and GraceUI need the same major: on a mismatch the page shows "This
  page needs updating: reload it" when a reload would fetch a matching web app, and otherwise names the GraceUI to
  install; in game the menu stays off with the same words (§6.4).
- **GraceUI's own version.** Its minor versions follow EldenNet's increments (GraceUI 0.6 for EldenNet v0.6), with patch
  numbers of its own; from GraceUI 1.0 its version is its own semantic version (D37, D74). `graceui.toml` and
  [Features by version](features.md) give the GraceUI release that brought each minor.

## What a mod ships

| A mod ships | A mod never ships |
|---|---|
| Its own DLL and files | A copy of `graceui.dll`, or of any part of GraceUI's folder (D83, invariant 18) |
| GraceUI's header-only C and C++, compiled into it: `graceui.h`, and `graceui-sys`'s `graceui_sys.h`, `graceui_sys.hpp` and `graceui_imgui.hpp` (D70, D136, D156) | A hook library, or a hook on a function GraceUI hooks (D102, invariant 16) |
| Dear ImGui's core at 1.93.0, if it draws with a Dear ImGui of its own | A Dear ImGui backend, renderer or platform (D76) |
| Its own faces: OFL, each with its licence file (below) | A font not from its publisher: a game's font, or one taken out of another mod (§3.4, §3.17) |

The functions GraceUI hooks, which no mod hooks (D102): the swap chain's `Present`, `Present1`, `ResizeBuffers` and
`ResizeBuffers1`, the command queue's `ExecuteCommandLists`, the game's window procedure, DirectInput's
`GetDeviceState` and XInput's `XInputGetState`.

A mod requires GraceUI and never carries a private copy (D83). EldenNet carries the `GraceUI` folder of the GraceUI
release current at its own release date, byte for byte, which its launcher installs into `Game\GraceUI\` only where
none or an older one is, never over a newer one, checking its hashes (D53, D83).

A face that GraceUI ships in its `fonts\` folder for mods, Noto Sans Regular and SemiBold (D268) and EB Garamond Italic
(D241), may be registered from there as a face of the mod's own, as EldenNet does, rather than carried as a second
copy.

## Installing and updating GraceUI

- Close the game. Extract GraceUI's release zip, which holds the one `GraceUI` folder, into `ELDEN RING\Game\`, so that
  it replaces the old folder. Windows locks a loaded DLL, so an update with the game running fails (§2.7).
- `user\` is in no release, and stays as it was.
- Files a newer release no longer lists are ignored and never served; `README.txt` says they may be deleted.
- The sizes and hashes in `graceui.toml` catch a corrupt or half-copied folder; they are not a signature. Release pages
  publish the files' hashes (D87).

## The anti-cheat's notice-only mode

GraceUI checks for an anti-cheat once a session, in the first `GraceUI_RequestApi`, before any table is returned: an
anti-cheat's module loaded in the process, or the game started by an anti-cheat's launcher (D133). On a hit the session
is in the notice-only mode (D88):

- `connect` answers `GRACEUI_INACTIVE` with `GRACEUI_INACTIVE_ANTI_CHEAT`, and a handle.
- `notice` works. Every other slot but `has_cap` and `disconnect` answers `GRACEUI_INACTIVE` (D131), `register_pages`
  among them.
- GraceUI serves no menu, panel, input or socket for the session: every input passes to the game, and nothing is drawn
  but notices.
- GraceUI shows one notice of its own, "Anti-cheat is running, so GraceUI shows notices only this session: no menu,
  panels or browser view.", and logs one line.

It is not checked again during the session. A later hit, from GraceUI's own re-check every 60 s while the server runs or
from any world-state provider's anti-cheat flag, closes the browser view's server for every space for the rest of the
session (D81). The other reasons for `GRACEUI_INACTIVE`, an overlay that could not start or that turned itself off,
leave a mod's calls doing nothing, `notice` included ([connect](abi.md#connect)).

`hello-debug-view` shows its banner in the notice-only mode and nothing else:

    if (!api || api.connect(mod, answer) != GRACEUI_OK) {
      // No GraceUI, or nothing can be drawn this session. In the anti-cheat's notice-only mode (inactive, with a handle)
      // only notices work, and the view stays closed.
      if (answer.status == GRACEUI_INACTIVE && answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT) {
        banner(api, answer.consumer, HELLO_DEBUG_BANNER_NO_HOTKEYS);
      }
      return 0;
    }

## Licences

- **GraceUI is MIT** (`LICENSE`). Its headers, `graceui.h` and `graceui-sys`'s, carry `SPDX-License-Identifier: MIT`.
  MIT's one condition: "The above copyright notice and this permission notice shall be included in all copies or
  substantial portions of the Software."
- **What `graceui.dll` contains** besides GraceUI's own code, compiled into its `grace-host` module alone: Dear ImGui
  1.93.0 (MIT) and MinHook 1.3.4 (BSD 2-Clause). `NOTICE` gives both licences in full.
- **Dear ImGui in a mod.** A mod that compiles Dear ImGui's core carries Dear ImGui's MIT licence with it, as GraceUI's
  `NOTICE` does.
- **Fonts.** OFL faces only, each from its publisher's own release, shipped with its `OFL.txt` (§3.4, D22); never a font
  file taken out of another mod or a game (§3.17). `hello-panel` keeps Noto Sans Mono beside its DLL, in its
  `hello-panel\` folder, with its `OFL.txt` beside it. A face is a TrueType or OpenType file of at most 4 MiB
  ([Capacities](threads.md#capacities)).
