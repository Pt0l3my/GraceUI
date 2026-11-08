# GraceUI: notes for Claude

GraceUI is EldenNet's main menu and dashboard: one set of pages shown in game on F5 and in the player's browser at the
dashboard's address, which EldenNet serves on loopback while the game runs, unless anti-cheat or an unknown build keeps
EldenNet inactive (DESIGN §7.2 item 1). GraceUI is a mod of its own, with its own releases and its own folder in the
game's `Game` folder, and each EldenNet release carries the current one (DESIGN G2). EldenNet (a separate repository)
depends on GraceUI; GraceUI never depends on EldenNet.

**Scope, widened by the owner (G14, 2026-11-08).** GraceUI is to be a UI framework that other modders can use, as
Magelight UI and PrismaUI are for Skyrim; EldenNet requires it as a dependency and always ships with it, and EldenNet's
menu and dashboard become its first user. What this changes is in [proposal 0001](docs/proposals/0001-ui-framework.md),
put to the owner as G15–G20 (Open); until they are answered, the decided design below stands.

- The design is [docs/DESIGN.md](docs/DESIGN.md); the evidence is in [docs/research/](docs/research/README.md); what
  EldenNet must change is in [docs/eldennet-changes.md](docs/eldennet-changes.md).
- EldenNet's own rules bind GraceUI too. Where this file and EldenNet's `CLAUDE.md` overlap, follow the stricter.

## Reading cheaply

- Grep with line numbers first, then read only that range. Never open a whole design or research document to find
  one thing, and never re-read a file you have just edited.
- EldenNet's documents are read in the EldenNet checkout with its `wp` tool (`wp show DESIGN§12`, `wp grep`).
- For a wide search, use one subagent and keep only its conclusion. Run long jobs in the background and print only the
  last or matching lines.

## Privacy and safety: the sweep comes first

- Nothing is pushed or uploaded before the privacy and safety sweep passes. The sweep is the owner's `privsweep`
  (kept identical in Skyrizz and EldenNet under `tools/privsweep/`), installed as git's global pre-push gate. If
  `~/.config/privsweep` is missing, run `python3 -I tools/privsweep/privsweep.py install` in the EldenNet checkout
  first. Sweep by hand anything the gate doesn't cover, such as a message to another session or a file sent to the
  owner.
- Never bypass it: no `--no-verify`, no change to `core.hooksPath`, no HOME or GIT_CONFIG override. Remove what it
  finds; a personal hit is never allowed.
- Never commit or upload anything personal about the owner: real name, account names, machine names, contact details,
  API keys, local endpoint addresses or IPs. The owner's own local commands go in chat only.
- Commit identities are noreply addresses only; use the environment's default git identity.
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

- **One overlay** (EldenNet O25): hudhook pinned at exactly 0.9.3, built with `dx12` only; `imgui` only through
  `hudhook::imgui`, MinHook only through `hudhook::mh`; never its eject path; no second Present hook, no second
  MinHook. No `imgui-tables-api` feature and no hudhook patch without the owner's approval (DESIGN D28). Under G10
  (decided by the owner, 2026-11-07: (b)), `graceui.dll` links no hudhook, `imgui` or MinHook, installs no hook and
  starts no thread; only GraceUI's host kit inside EldenNet (`grace-host`) touches `hudhook::imgui`.
- **No me3 and no fromsoftware-rs**, in any form.
- **Read-only towards the game.** GraceUI never reads or writes game memory and calls no game code; it sees only what
  EldenNet's core projects.
- **v1 is dialogue only.** Acts and gestures appear only as "later version" seams.
- **Spoiler layers apply to the UI** as they do to prompts: the spoiler rules SP1–SP9 (DESIGN §4.8) bind every page,
  count, search result, title and URL, in both views.
- **Privacy rules PV1–PV8** (DESIGN §4.9) bind every page, log and report: nothing personal, no keys, no chat or model
  text outside the pages that show it, no paths, user names, SteamIDs or local addresses.

## Invariants (each has a reason and a test; extend the list as the code grows)

1. **Projections only.** No view ever receives raw content, flags, stages, prompts or unshown model text (spoilers;
   the spoiler goldens).
2. **Absence is silent.** Nothing not reached is shown, counted, greyed or hinted at; the veiled look is only for a
   perceived character whose name the game has not shown (SP5; the goldens). The one exception is the roster, by the
   owner's decision (G5, G8): every NPC type, and every named NPC's and named boss's general profile, are listed from
   the start (D67), holding only what a first meeting shows and reviewed for spoilers like shipped content; everything
   about this playthrough follows the rule in full (the roster and Playthrough goldens).
3. **One command path.** Every change goes through EldenNet's one validating entry point; a view is untrusted input
   (security; the command tests).
4. **Loopback by default.** The server binds 127.0.0.1 or `::1` (and the PC's private network addresses only while
   `dashboard.lan` is on), checks Host, Origin, Fetch Metadata, the session cookie (granted on the loopback listener
   only) and the ticket, sends no CORS headers and a strict CSP, and serves only the files `graceui.toml` lists (DNS
   rebinding and CSRF; the server suite). It is always on (G4); other devices on the home network only when the player
   switches that on, and each only after pairing with a code shown in game (G9).
5. **Secrets are write-only** and never logged, echoed, stored in the browser or put in a report (privacy; the log
   and report scans).
6. **No page requests anything outside its own origin**: no CDN, no remote font, no analytics (privacy; the CSP and a
   bundle scan).
7. **One close routine for every exit path**, which releases capture and keeps changes (input leaks; the close matrix).
8. **The closing press never reaches the game**, and the opening press does nothing in the menu until released
   (Esc opens the game's menu, B dodges; the pad-ledger tests).
9. **Danger closes the menu at once** and cancels any open confirmation (the game never pauses; in-game checks).
10. **The present thread never waits and allocates nothing per frame after warm-up**, on either side of
    `graceui.dll`'s ABI (EldenNet's T3 rule; the allocation test).
11. **No texture upload after load** in game (hudhook 0.9.3 leaks one buffer per upload; the upload counter).
12. **The page descriptions are the one source of the information architecture**, and every setting has exactly one
    home (drift between views; the page lint and parity goldens).
13. **One token file** for both views; every readable text 4.5:1 or better (legibility; the contrast check).
14. **No looping animation** (cost and calm; a lint on the token durations and the stylesheet).
15. **One versioned ABI, checked at every start.** EldenNet drives `graceui.dll` only through `graceui_entry` and its
    append-only tables; the ABI and protocol majors must match and the folder must match `graceui.toml`; a missing,
    mixed or mismatched copy, an error code or a vertex list that fails its check turns the menu off with a notice,
    and EldenNet keeps playing (G2; the ABI and handshake tests).
16. **`graceui.dll` hooks nothing.** No hudhook, ImGui, MinHook, thread, socket, file or game access in it (one
    overlay; G10 (b), decided by the owner; a CI check of its imports and symbols against DESIGN §2.2's deny list
    and allowlist).

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
