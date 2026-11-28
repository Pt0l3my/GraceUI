# Troubleshooting and bug reports

Every slot of GraceUI's answers a status, and `connect` never answers with silence (D73, invariant 15). GraceUI says
what went wrong in three places: its
log, its own notices, and its own pages. This part lists what each says about a mod, the tools that check a mod's pages
before the game does, and what a bug report holds.

## GraceUI's log

GraceUI keeps its log in memory: the newest 64 lines, each at most 160 bytes and starting `GraceUI: `. GraceUI's own
thread passes each line to the debugger with `OutputDebugString`, never the thread that wrote it, so a debugger or
another listener of Windows' debug output shows them. Lines the ring overwrote before they were passed are counted in a
line of their own: "‹n› lines were not passed to the debugger".

Since GraceUI 0.2.74 the same lines go to a file too (D84, D282): `Game\GraceUI\user\graceui.log`, this session's, and
`graceui-previous.log`, the session before, kept for a report about a session that ended badly. If the previous file cannot be replaced (an editor holding it open), this session's lines follow the last session's in `graceui.log`, after a line that says so: no session's log is lost. The file is opened at
GraceUI's first line of the session, the one before moved aside then, and holds at most 1 MiB (EST): past it, one line
says so and later lines go to the debugger alone. A line that seems to name a place on the player's PC (a drive, a
share, a home folder) is left out of the file, and a line says one was. The files stay in `user\` across GraceUI's
updates, and are in no release.

A line holds GraceUI's own words, numbers and mods' ids, never a path, a user or machine name, or game text (PV3,
D230). The first says which GraceUI this is: "GraceUI ‹version› starting (ABI 1.‹minor›, a preview)".

The lines about a mod:

| Line | What it means |
|---|---|
| ‹id› ‹version› connected (ABI 1.‹minor›) | `GRACEUI_OK`; the minor is the one the mod was built with |
| ‹id› connected, but GraceUI is inactive this session: ‹why› | `GRACEUI_INACTIVE`: "anti-cheat: notices only", "the overlay could not start" or "the overlay turned itself off" |
| ‹id› needs GraceUI ‹floor› or newer; this is GraceUI ‹version› | `GRACEUI_TOO_OLD` |
| a second mod asked to connect as ‹id› while it is connected: refused | `GRACEUI_DUPLICATE_ID` |
| a connect was refused: ‹why› | `GRACEUI_REFUSED`, with the reason's words ([connect](abi.md#connect)) |
| more connects failed than the log keeps: the rest are not logged | After 16 failed connects in a session |
| ‹slot› refused a call: ‹why› | A slot answered `GRACEUI_REFUSED`: logged once for each slot in the session, whichever mod called |
| ‹slot›: an exception inside GraceUI, caught; nothing was done | `GRACEUI_FAULT`, once for each slot (D110) |
| a draw list of ‹id› was refused: ‹why› (logged once for this mod) | `submit` refused a list; the panel keeps its last ([submit and its checks](panels.md#submit-and-its-checks)) |
| ‹id›'s register_pages refused: ‹why› | `register_pages` refused the space; the reason names the part |
| space ‹id› published a secret's value: only whether it is set was sent | Once a space ([Secrets](privacy.md#secrets-are-write-only)) |
| space ‹id› left a command unanswered for 10 s: answered internal | Or 90 s for a slow command ([Answers](protocol.md#answers)) |
| browser view: A space's command queue is full (‹id›): the newest command was dropped. | At most once a second |
| ‹id›'s on_present callbacks are suspended for this session: ‹why› | Over 0.5 ms a frame for 120 frames running, or an exception ([on_present](threads.md#on_present)) |
| ‹id›'s notices are quiet: kept on the Mods page | The player quieted the mod's notices; "shown again" when they turn them back on |
| a second copy of GraceUI is loaded, and idle: ‹name› | Another copy of GraceUI is loaded ([One copy](versions.md#one-copy)) |
| the anti-cheat check found ‹what›: GraceUI shows notices only this session (D88) | The notice-only mode ([Versions and distribution](versions.md#the-anti-cheats-notice-only-mode)) |

A mod that never reaches `connect` leaves no line naming it. The `log` slot answers `GRACEUI_NOT_YET` until the
increment that builds it ([Log](abi.md#log)), so a mod says in its own log why it did not connect.

## GraceUI's notices about a mod

GraceUI's own notices are never quiet (D181). Each lasts 20 s (EST, D134), but "has the controls", which lasts 2 s, as
long as the player's exit waits for the second press.

| Notice | When |
|---|---|
| ‹Mod› needs GraceUI ‹floor› or newer; this is GraceUI ‹version›. | `connect` answered `GRACEUI_TOO_OLD` |
| ‹Mod› needs a newer GraceUI for its pages: ‹why›. | `register_pages` refused page data above GraceUI's protocol minor (D264) |
| A second copy of GraceUI was loaded from another folder and is idle: ‹name› | A copy of GraceUI other than the one in use is loaded (D135) |
| GraceUI's own copy is idle: another copy was started first and is in use, ‹name›. Remove that copy to use GraceUI's own. | A copy from another folder was asked first (D244) |
| ‹Mod› slowed the game's frames, so what it draws in each frame is off for this session. | The mod's `on_present` callbacks stayed over their budget (D259) |
| ‹Mod›'s drawing in each frame failed, so it is off for this session. | An exception left one of the mod's `on_present` callbacks |
| ‹Mod› has the controls. Press F5 again to take them back. | The mod holds input and the player pressed the menu key ([Input and focus](input.md)) |
| Anti-cheat is running, so GraceUI shows notices only this session: no menu, panels or browser view. | The notice-only mode (D88) |

A mod the player has quieted gets no notice of it: its notices are answered `GRACEUI_OK` as before and kept on GraceUI ›
Installed mods instead of drawn (D181).

## GraceUI's own pages

In the in-game menu (F5) and the browser view:

- **GraceUI › Installed mods**: every mod connected this session, with its version, what it uses (its caps, in words)
  and whether its notices show; and Quiet notices, what the quiet mods have said this session, newest first (D181). A
  mod that is not listed did not connect.
- **GraceUI › Hotkeys**: every mod's hotkeys, bound or not ([The Hotkeys page](input.md#the-hotkeys-page)).
- **GraceUI › About**: GraceUI's version, its licence and the work it builds on.

## When something does not show

| What is seen | Where to look |
|---|---|
| Nothing of the mod, and no line names it | `graceui_sys_find` gave null: no GraceUI that serves ABI 1 was found in its time ([Finding GraceUI](abi.md#finding-graceui)). It is called from the mod's own thread, never `DllMain` |
| Only the mod's notices | The notice-only mode ([Versions and distribution](versions.md#the-anti-cheats-notice-only-mode)) |
| Nothing, and the mod's connected line says inactive | The overlay could not start, or turned itself off: every call does nothing this session, `notice` included |
| A panel never shows | Panels start hidden until `show_panel`; or each `submit` was refused, and the log line names why |
| A panel stops changing | Its newest lists were refused: it keeps its last; the log line names why, once |
| A texture is refused `GRACEUI_TOO_LATE` | It was registered after the mod's first `submit` or `set_on_present` ([The registration window](threads.md#the-registration-window)) |
| Glyphs of the shared atlas are missing | `glyph_table` answers `GRACEUI_NOT_READY` until warm-up; `register_face` after warm-up answers `GRACEUI_TOO_LATE`; a face file Dear ImGui cannot read is left out at warm-up with a log line (D139); a list that names another generation is refused |
| A notice does not show | `GRACEUI_FULL` (4 of the mod's, or 16 in all); a text with a control character other than the line feed is refused; or the player has quieted the mod |
| The mod's pages are absent | `register_pages` was refused (its log line); the notice-only mode; page data above GraceUI's minor (its notice); or a page whose `increment` is later than the one the space registered, absent until then (D173) |
| A page reads "Something went wrong; the log has the details" | The space answered `internal`, or left the command unanswered past its deadline (its log line) |
| A paired device is told "Only on the PC the game runs on." | GraceUI keeps that command, setting, page or topic to this PC ([This PC and paired devices](privacy.md#this-pc-and-paired-devices)) |
| A hotkey does nothing | `register_hotkey` answered `GRACEUI_UNBOUND`: the player binds it on GraceUI › Hotkeys ([Hotkeys](input.md#hotkeys)) |

## The tools

Each checks a mod's pages without the game.

- **grace-dev** (`tools/grace-dev/README.md`): a fake host that serves a space's `pages.json`, `settings.json` and
  fixtures to the browser view through the real server, on loopback, and plays a scenario of its states and answers.
  `--dump SPACE PAGE` lays a page out as the in-game menu does and prints its structure and the draw-list checker's
  answer, exiting 0 if the list passes and 1 if it is refused. It never prints a setting's value, a command's
  arguments, the session cookie, a ticket or a folder's path.
- **The page lint**: `python3 -I tools/pages/build.py --lint <space.toml>...` checks a space's sources against this
  GraceUI, by its nine rules ([The lint's rules](pages.md#the-lints-rules-in-short)).
- **The space check**: `npm run -s check:space -- <space folder> [--increment X.Y] [--golden <file>] [--update]`, in
  `web/`, opens the real browser view over a space's files and prints what it shows, for this PC's browser and for a
  paired device, in both themes. It fails on a console error, a CSP violation, a request outside the page's origin or a
  text under 4.5:1. Its dump is a golden of the mod's own: `--golden` compares with it, and `--update` writes it.
  Nothing it prints names the folder or any path (PV3).

## A bug report

A report names every mod in the process, since every mod is native code with the player's rights and one mod's fault
can look like another's (§7.1, §11).

Put in:
- GraceUI's version, from GraceUI › About or the log's first line, and whether the session was in the notice-only mode.
- Every mod connected, with its version, from GraceUI › Installed mods.
- The mod's id, its version, its floor, the caps it asked for and the ABI minor it was built with
  (`GRACEUI_ABI1_MINOR`).
- GraceUI's log lines, whole, and the notices shown, in their words.
- What was done, what was seen, and what was expected.
- For pages: the page lint's output, grace-dev's `--dump`, or the space check's dump.

Never put in (PV3):
- Anything personal: a real name, an account name, a user or machine name, a SteamID or contact details.
- A key, a password, a token or any secret's value; the session cookie, a ticket or a pairing code.
- A path: name a file by its place in the game's folder, as GraceUI's own lines do (D135).
- An IP address or a network address at home, the PC's or a device's.
- Page content: a topic's body, a player's or a model's words.
