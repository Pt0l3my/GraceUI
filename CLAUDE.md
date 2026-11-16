# GraceUI: notes for Claude

GraceUI is EldenNet's main menu and dashboard: one set of pages shown in game on F5 and in the player's browser at the
dashboard's address, which GraceUI serves on loopback while the game runs, unless anti-cheat or an unknown build keeps
EldenNet inactive (DESIGN §7.2 item 1). GraceUI is a mod of its own, with its own releases and its own folder in the
game's `Game` folder, and each EldenNet release carries the current one (DESIGN G2). EldenNet (a separate repository)
depends on GraceUI; GraceUI never depends on EldenNet.

**Scope, widened by the owner (G14, 2026-11-08).** GraceUI is to be a UI framework that other modders can use, as
Magelight UI and PrismaUI are for Skyrim; EldenNet requires it as a dependency and always ships with it, and EldenNet's
menu and dashboard become its first user. What this changes is in [proposal 0001](docs/proposals/0001-ui-framework.md),
put to the owner as G15–G20. **Decided the same day, as recommended:** G15, GraceUI owns the one overlay (hudhook, its
MinHook, the Present hook and the input hooks move into `graceui.dll` with every rule of EldenNet's O25, and EldenNet
draws through GraceUI), superseding G10, on Dear ImGui and MinHook since 2026-11-16 (EldenNet's O25 as changed); G16,
other mods write page data and checked panels, no HTML in game; G17, GraceUI serves the browser view for every mod that
opts in. G18–G20 are Open. **Folded on 2026-11-15** (EldenNet's roadmap S7.8.c): DESIGN.md now holds the design as
G15–G17 decided it (proposal 0001's parts, adopted), the map's decisions G21–G27 (proposal 0002) and the D-entries they
settle; the rules and invariants below follow it. What depends on G18–G20 stays as written until the owner answers.

- The design is [docs/DESIGN.md](docs/DESIGN.md); the evidence is in [docs/research/](docs/research/README.md); what
  EldenNet must change is in [docs/eldennet-changes.md](docs/eldennet-changes.md).
- EldenNet's own rules bind GraceUI too. Where this file and EldenNet's `CLAUDE.md` overlap, follow the stricter.

## Reading cheaply

- Grep with line numbers first, then read only that range. Never open a whole design or research document to find
  one thing, and never re-read a file you have just edited.
- EldenNet's documents are read in the EldenNet checkout with its `wp` tool (`wp show DESIGN§12`, `wp grep`).
- The shared reasoning log is EldenNet's `docs/REASONING.md` (`wp show WHY§n`): read its index at the start of a session
  and after a switch of model, and add GraceUI's findings to it, tagged `#graceui`, by EldenNet's rule for the log.
- For a wide search, use one subagent and keep only its conclusion. Run long jobs in the background and print only the
  last or matching lines.

## Privacy and safety: the sweep comes first

- Nothing is pushed or uploaded before the privacy and safety sweep passes. The sweep is the owner's `privsweep`,
  kept only in the owner's private Skyrizz repository under `tools/privsweep/` and never in a public one (owner,
  2026-11-15), installed as git's global pre-push gate. If `~/.config/privsweep` is missing, run
  `python3 -I tools/privsweep/privsweep.py install` in the Skyrizz checkout first. Sweep by hand anything the gate doesn't cover, such as a message to another session or a file sent to the
  owner.
- Never bypass it: no `--no-verify`, no change to `core.hooksPath`, no HOME or GIT_CONFIG override. Remove what it
  finds; a personal hit is never allowed.
- Never commit or upload anything personal about the owner: real name, account names, machine names, contact details,
  API keys, local endpoint addresses or IPs. The owner's own local commands go in chat only.
- Commit identities are noreply addresses only; use the environment's default git identity.
- No `Claude-Session` link in commit messages or pull requests (owner, 2026-11-16): it points to a private
  transcript and tells readers nothing. The `Co-Authored-By` trailer stays.
- No model identifiers of the assistant in files, apart from the commit trailer.

## Licences and third-party material

- **GraceUI is MIT.** Write our own code. Check a dependency's licence before adopting it, and list it in `NOTICE`.
- **Magelight UI** (Severause): the host, API headers, docs, tools and npm packages are MIT; `gpu/` at 1.7.1 is zlib
  (Ultralight's reference driver, altered). Either may be reused only with its copyright and permission notice in
  `NOTICE` (zlib: changes marked). Nothing of it is used in v1 (DESIGN D35). **Never** reuse `gpu/` from v0.30.1 or
  `extern/appcore-ref/` (LGPL-derived), and never put Ultralight's runtime, SDK files or compiled shaders in this
  repository; Ultralight is proprietary and is used only if the owner adopts it (DESIGN G1).
- **SeverActions** has no licence: all rights reserved. Never copy its CSS, HTML, JS, images, sigils, layouts' code,
  texts, taglines or theme names. We may describe its look in our own words and build our own theme in its spirit.
- **Fonts**: OFL faces only, each from its original publisher, shipped with its `OFL.txt` and credited in `NOTICE`.
  Never a font file taken out of SeverActions, Magelight or a game.
- **SkyrimNet** (owner's decision, 2026-11-07, as in EldenNet's `CLAUDE.md`): its author, Min, gives us the engine
  blueprint to work with and approves building on SkyrimNet, so its code, dashboard and styling may be used, copied and
  adapted, credited as Min wishes and with every licence and notice file that comes with it. Never present GraceUI as
  SkyrimNet itself or claim more endorsement than Min has given.
- **FromSoftware**: never commit game files, text, params, fonts, icons, button glyphs, frames, textures, screenshots,
  the logo or the ring mark. Names of characters and places appear only at run time, from the player's own install,
  through EldenNet. Fixtures and examples use invented names and our own words.
- **Untrusted copies** (uploads, clones, downloaded packages) go in their own folder outside the repository, are read
  with `grep` or `python3 -I` from scripts kept elsewhere, and are never built or run.

## EldenNet's rules that bind GraceUI

- **One overlay, GraceUI's** (EldenNet O25, moved to GraceUI by G15 (a), 2026-11-08; its libraries chosen by the owner
  on 2026-11-16, when GraceUI's in-game parts became C++ by EldenNet's O60): `graceui.dll` is the host (D69), and its
  `grace-host` module alone holds the hooks, Dear ImGui 1.93.0's DX12 and Win32 backends and MinHook 1.3.4, both
  pinned; it never unhooks or unloads while the game runs; no second Present hook and no second MinHook in the
  process. No patch to either library and no change of version without the owner's approval (DESIGN D28). EldenNet and
  every other consumer link no hook library or ImGui renderer backend, hook none of GraceUI's functions (the list
  GraceUI publishes, D102) and draw through GraceUI's public C ABI (panels, notices, page data); a consumer may build
  its draw lists with Dear ImGui's core of its own, with no backend. EldenNet's own later hooks, on other functions,
  follow its R12 (EldenNet O65, 2026-11-15).
- **No me3 and no fromsoftware-rs**, in any form.
- **Read-only towards the game.** GraceUI never reads or writes game memory and calls no game code; its hooks are on
  system DLLs only (Present, the window procedure, DInput and XInput), and it sees only what its spaces publish and
  its world-state providers report (EldenNet first, D78).
- **v1 is dialogue only.** Acts and gestures appear only as "later version" seams.
- **Spoiler layers apply to the UI** as they do to prompts: the spoiler rules SP1–SP9 (DESIGN §4.8) bind every page,
  count, search result, title and URL, in both views.
- **Privacy rules PV1–PV8** (DESIGN §4.9) bind every page, log and report: nothing personal, no keys, no chat or model
  text outside the pages that show it, no paths, user names, SteamIDs or local addresses.

## Invariants (each has a reason and a test; extend the list as the code grows)

1. **Projections only.** A view shows only what its space published; for EldenNet's space, no view ever receives raw
   content, flags, stages, prompts or unshown model text (spoilers; the spoiler goldens).
2. **Absence is silent.** Nothing not reached is shown, counted, greyed or hinted at; the veiled look is only for a
   perceived character whose name the game has not shown (SP5; the goldens). The one exception is the roster, by the
   owner's decision (G5, G8): every NPC type, and every named NPC's and named boss's general profile, are listed from
   the start (D67), holding only what a first meeting shows and reviewed for spoilers like shipped content; everything
   about this playthrough follows the rule in full (the roster and Playthrough goldens).
3. **One command path per space.** Every change goes through its space's one validating entry point (EldenNet's for
   EldenNet's pages), and GraceUI routes a command only to the space that owns the page; a view is untrusted input
   (security; the command tests).
4. **Loopback by default.** The server binds 127.0.0.1 or `::1` (and the PC's private network addresses only while
   `dashboard.lan` is on), checks Host, Origin, Fetch Metadata, the session cookie (granted on the loopback listener
   only) and the ticket, sends no CORS headers and a strict CSP, and serves only the files `graceui.toml` lists (DNS
   rebinding and CSRF; the server suite). It runs while any space asks for it, always for EldenNet's once its gate and
   safety steps pass (G4, D81), and never beside anti-cheat; other devices on the home network only when the player
   switches that on, and each only after pairing with a code shown in game (G9).
5. **Secrets are write-only** and never logged, echoed, stored in the browser or put in a report (privacy; the log
   and report scans).
6. **No page requests anything outside its own origin**: no CDN, no remote font, no analytics (privacy; the CSP and a
   bundle scan).
7. **One close routine for every exit path**, for every input holder (the menu, a panel, a modal), which releases
   capture and keeps changes (input leaks; the close matrix).
8. **The closing press never reaches the game**, whoever held input, and the opening press does nothing in the menu
   until released (Esc opens the game's menu, B dodges; the pad-ledger tests).
9. **Danger closes every view at once** and cancels any open confirmation: any world-state provider's danger (D78);
   without a provider there is no danger close, and that is documented (the game never pauses; in-game checks).
10. **The present thread never waits and allocates nothing per frame after warm-up**, in GraceUI and in every opted-in
    `on_present` callback, each within its budget (D76; EldenNet's T3 rule; the allocation test).
11. **No texture upload after load** in game (an upload needs a copy and a fence that the present thread must not wait
    on, invariant 10; hudhook's leak, the first reason, lapsed with it on 2026-11-16): every texture is registered
    in its mod's registration window and within its budget (D76). One exception, by the owner's G26 (a): the in-game
    map's texture is composed and uploaded again when the player gains a map piece (the upload counter, which counts
    them).
12. **The page descriptions are the one source of the information architecture**, and every setting has exactly one
    home (drift between views; the page lint and parity goldens).
13. **One token file** for both views; every readable text 4.5:1 or better (legibility; the contrast check).
14. **No looping animation** (cost and calm; a lint on the token durations and the stylesheet).
15. **One public ABI, checked at every connect.** Every consumer, EldenNet first, reaches GraceUI only through
    `GraceUI_RequestApi` and its append-only table per major (D73, D74); every struct starts with its size; `connect`
    answers `ok`, `too_old` (naming both versions), `duplicate_id`, `inactive` or `refused`, never silence; a draw list
    that fails its check is refused (D76); a failed connect leaves the consumer headless with a log line, and EldenNet's
    launcher checks GraceUI before start (D88) (the ABI, handshake and draw-list tests).
16. **GraceUI's functions have one owner.** `grace-host` in `graceui.dll` is the only code holding MinHook or hooking
    the functions GraceUI hooks: the swap chain's `Present`, `Present1`, `ResizeBuffers` and `ResizeBuffers1`, the
    command queue's `ExecuteCommandLists`, the game's window procedure, DirectInput's `GetDeviceState` and XInput's
    `XInputGetState`, a list GraceUI publishes (D102). No consumer links a hook library, hooks one of those functions or
    hands GraceUI an ImGui context; EldenNet's own hooks on other functions follow its R12 (EldenNet O65); the pure
    modules take no Win32 (G15 (a); CI checks the module graph, `graceui.dll`'s imports against a reviewed allowlist,
    and EldenNet's hook targets against the published list).
17. **Honest focus.** One input holder at a time; `request_input` is answered at once: `granted`, `refused` naming the
    holder, or `not_now` with the reason; nothing deposes the holder or queues behind it; the player's exit (a second
    menu-key press within 2 s, or Escape held 1 s) is never captured (D77; the input tests across holders).
18. **One copy.** `Game\GraceUI\` is the only copy: a second one loaded from another folder idles behind the
    per-process mutex, answers every request with null and is named in one notice; mods require GraceUI and never
    carry a private copy (D72, D83; the two-copies tests).

## Working rules

- Work on a topic branch (`design/`, `feat/`, `fix/`, `docs/`, `test/`, `chore/`), never directly on `main` unless
  the owner asks. Commit and push checked work to the topic branch after the privacy sweep passes; open a pull
  request only when asked.
- Ask the owner, with a recommendation, about anything the design leaves open. Add each question to
  [docs/DECISIONS.md](docs/DECISIONS.md) as a new entry marked Open, with the next free G-number, its options and the
  recommendation, and to the index in DESIGN §12 ("Decisions for the owner"). Record the answer in the entry with its
  date and the owner's words; a later decision amends an earlier entry by a dated note, never by rewriting it, and ids
  are never renumbered. Record what you settle yourself in DESIGN §13.
- When a GraceUI decision changes what EldenNet must do, add it to `docs/eldennet-changes.md`.
- Keep example content original: fixtures, placeholder text, icons and ornaments are ours.
- Text that players read uses British spelling, as EldenNet's documents do.
