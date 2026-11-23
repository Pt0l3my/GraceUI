# Owner decisions

The project owner's decisions on GraceUI, binding, one entry each, numbered G1 onwards. This file is the one record
of them: [DESIGN §12](DESIGN.md#12-decisions-for-the-owner) is its index, and every G-number GraceUI's documents (and
EldenNet's) cite is an entry here. What the designers settle themselves stays in DESIGN §13, the decision log (D1
onwards). EldenNet's own decisions (O1 onwards) are in EldenNet's `docs/DECISIONS.md`; what each G-answer changes in
EldenNet is in [eldennet-changes.md](eldennet-changes.md).

- **Ids.** An id is never renumbered or reused. G1–G10 are the questions DESIGN §12 put to the owner, in its order.
  G11 and G12 are owner decisions recorded outside §12's table (in DESIGN §14 and in CLAUDE.md); they take the next
  ids. G13 onwards are later questions.
- **An entry** gives the date and status, the question as it was put (with its options), what was decided, the
  owner's own words, and what it amends. The recommendation as it was written and its reasons stay inside the
  decision, under their own italic labels. A bare section sign (§2.9, §7.2) means a section of GraceUI's DESIGN.md;
  "EldenNet O25" or "EldenNet DESIGN §7.8" points into EldenNet; the research notes are cited by their short names
  (REN, ML, IA, LOOK, SND).
- **Changes.** A later decision may widen or supersede an earlier one, and says so under **Amends**. The earlier
  entry keeps its text as decided, with a dated note under **Later changes**; the question stays as it was put.
- **A new question** takes the next free id: an entry here marked **Open**, with the question, its options and a
  recommendation, and a row in DESIGN §12's index. The owner's answer is recorded in the entry with its date and
  their words, and the index row is updated. When it changes what EldenNet must do, it is added to
  eldennet-changes.md too.

## By date

- **2026-11-07.** G11, the request that started GraceUI. G1 (the in-game renderer). G2 and G4–G9 (a standalone mod,
  the dashboard always on, the roster from the start, the Steam Deck's scale, the WIP tag, the Playthrough page,
  pairing for other devices). G3 and G10 (Preact; the overlay stays in EldenNet), with the roster's widening by
  EldenNet's O43 as updated and O45. G12 (building on SkyrimNet). G1–G6 were put with the first proposal, G7 with
  the framework categories (D38), G8 after their review, and G9 and G10 with the owner's answers to G4 and G2.
- **2026-11-08.** G13 (an interactive showcase of the menu: wait for Min's blueprint; later the same day, it is
  EldenNet's). G14 (GraceUI's scope: a UI framework other modders can use; its consequences to be put as G15
  onwards).
- **2026-11-08.** G15–G20 put to the owner with proposal 0001 (GraceUI as a UI framework for any mod); G15–G17
  decided the same day, as recommended (GraceUI owns the overlay; page data and panels; the browser view for every mod
  that opts in), which supersede G10.
- **2026-11-14.** G21–G26 put to the owner with proposal 0002 (the map): what it is drawn on, glow and motion, who
  writes lore notes, when it ships, what the map key opens, and how the in-game map is kept to what was reached.
- **2026-11-14.** G27 (terrain: a local 3D relief and our own sprites), decided the same day, as recommended.
- **2026-11-15.** G21–G26 decided (G23 not as recommended). G28 (a narrow upload route, for EldenNet's plugins)
  put and decided the same day, as recommended. G29 (GraceUI's server for hosts outside the game) put with
  EldenNet's proposal 0002.
- **2026-11-16.** G30 (the server's C++ basis: our own) put and decided the same day, as recommended. G18–G20 and G29
  decided, as recommended (the framework opens with GraceUI 1.1; no SkyrimNet-derived material in GraceUI; EldenNet's
  pages live in EldenNet; one server library for hosts outside the game), and folded into both designs the same day
  (EldenNet's roadmap S7.8.d and S7.12.e).
- **Open.** None.

## G1

**The in-game renderer** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Which renderer draws the menu in game? **Options:** (a) **Dear ImGui with a native Grace theme** in EldenNet's one hudhook overlay; the full web UI in the browser. (b) **Ultralight in CPU mode** (the engine Magelight uses): the same HTML pages in game, uploaded as a texture through a reviewed patch to hudhook. (c) Ultralight with a Direct3D 12 GPU driver of our own. (d) CEF (Chromium) off-screen. (e) RmlUi, drawn as ImGui geometry. (f) Blitz or Servo.

**Decision.** (a). The browser keeps the full HTML pages. *Recommendation as written:* (a) for v1, with (b) kept as a throwaway spike after v1.0 if the owner wants the same HTML pages in game. *Why:* hudhook 0.9.3, pinned by O25, can show a changing web page only by leaking a new upload buffer on every update (about 15 MB at 1440p) and stalling the present thread, and it gives no access to the GPU, so (b), (c), (d) and (f) all need a change to the pinned library (REN §2). (b) also needs Ultralight's proprietary licence (a public free mod rests on its limited commercial grant), about 57 MB of runtime, an engine with a 60 FPS cap and no evidence under Proton; Magelight, the closest precedent, spent 17 of its 27 written invariants on its web engine (ML §2). (c) adds a GPU driver of our own for a still menu. (d) is about 250–300 MB with helper processes and a mixed Wine record. (f) are not ready (beta, monthly breaking changes). (e) fits O25 and is the fallback if ImGui proves too plain, but its pages are not the browser's. (a) adds no licence, binary or hook, uses the Proton path EldenNet must make work anyway and needs no texture updates. *What (a) gives up:* the in-game view is not the browser's HTML; it is drawn from the same page descriptions, tokens and protocol (§2.5), so it shows the same pages, but typography is simpler (no kerning or OpenType features, §3.16) and each component is written twice. The owner can judge the look before deciding: S4 produces in-game screenshots of a Grace page beside the browser mock. Choosing (b) later needs three approvals: the licence, the hudhook patch (an O25 change) and the size.

**Owner's words.** "let's use what's best compatibility with Elden Ring game engine", after weighing the lack of HTML in game.

**See also.** EldenNet O25 (the hudhook pin), which rules out (b)–(d) and (f) without a change; G10 keeps the one overlay in EldenNet.

*Later (2026-11-21):* the owner's look review of the in-game menu (EldenNet's roadmap S11.2.d; its first answers on
2026-11-20 gave D177, G31 and G32), made from screenshots of GraceUI's own pages, the demo space and EldenNet's first
pages in game and in the browser, was approved as drawn: "both approved" (with G36). (a) stands.


## G2

**GraceUI as a standalone mod** · *2026-11-07 · Decided by the owner; the recommendation was (a), a library*

**Question.** Is GraceUI a library inside EldenNet's DLL, or a standalone host DLL other Elden Ring mods can use? **Options:** (a) **A library**: GraceUI's crates and web bundle linked into `eldennet.dll`. (b) **A standalone host DLL** with an API for other mods, now. (c) (a) now, and a host later if other mods ask, grown from the bridge.

**Decision.** A standalone mod. So GraceUI has its own releases and one folder of its own in the game's `Game` folder, with a version file (§2.7, D51–D53); each EldenNet release carries the GraceUI release current at its release date; a player updates GraceUI on its own by copying a newer release's folder over the old one with the game closed; EldenNet checks what it finds at every start and keeps playing whatever it finds (§2.9, D54). The answer leaves two things to the design: which module owns the one overlay (G10, since decided: (b)), and whether other mods may use GraceUI, which v1 still leaves out (§1.3). *Recommendation as written:* (a), with (c)'s path noted, because one process holds only one hudhook overlay and one MinHook safely (ER02 §7.1) and being a host cost Magelight a versioned C ABI, a registry per mod, input-mode ownership, a sandbox, version gates and handling for mixed copies (ML §7). *What the decision costs:* a versioned C ABI with a handshake, the bridge's frames carried in process as they are to the browser (D55), the version file, and handling for a missing, older, newer or mixed copy: the ABI and version parts of Magelight's list, without its registry, sandbox or input arbitration, because GraceUI serves EldenNet alone.

**Owner's words.** "GraceUI will be a standalone mod, but EldenNet will feature the latest version with each release (at the release date) as built in, but users will be able to update GraceUI separately (by dragging files into Elden Ring/Game directory and overwriting old files)."

**Later changes.** G10 (2026-11-07) settles which module owns the one overlay: (b), EldenNet. 2026-11-08: G15 (a) supersedes G10, and the overlay moves into `graceui.dll`; so D88 replaces "EldenNet keeps playing whatever it finds": EldenNet's launcher checks GraceUI before start and installs the carried copy where none or an older one is (D83), and a failed connect leaves EldenNet headless with a log line. The folder, the carried release and updating by hand stand. Folded into the design on 2026-11-15 (EldenNet's roadmap S7.8.c).

## G3

**The browser app's framework** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Which framework for the browser app? **Options:** (a) React. (b) **Preact** with TypeScript and signals, built with Vite. (c) Svelte 5. (d) Solid. (e) Plain web components (or Lit)

**Decision.** (b), as recommended. The browser app is Preact with TypeScript and signals, built with Vite (§7.5). *Why (as recommended):* under G1 the web app runs only in desktop browsers, so the choice is about authoring and upkeep. Preact keeps React's component model and hooks, which people and AI assistants know best, at about a tenth of React's size; signals suit live topics that change four times a second without re-rendering pages. Its small parse cost and an es2022 target keep the door open to Ultralight's interpreter-only JavaScript if G1 ever changes (REN §4.2). Svelte 5 changed its syntax recently, so examples and generated code mix two dialects; Solid has a smaller community; web components need hand-written state handling. Licences (Preact MIT, Vite MIT) are read again at adoption and listed in NOTICE.

**Owner's words.** "G3: yes".

## G4

**The browser dashboard, always on** · *2026-11-07 · Decided by the owner; the recommendation was (a), opt-in*

**Question.** Is the browser dashboard on by default? **Options:** (a) **Off by default; opt-in** in Settings › Browser view (from v0.6), in First light, or with `dashboard.enabled = true` in `settings.toml` before v0.6. (b) On by default.

**Decision.** (b), always on. EldenNet serves the dashboard from the end of init's gate and safety steps to exit, unless anti-cheat is found later (§7.2 item 1); the player opens `http://127.0.0.1:8642` (D58) in a browser on the same PC and types or copies nothing else; the address is shown in game (Settings › Browser view, Home › Status, F11) and on the launcher's first screen (§7.2). Host, Origin and Fetch Metadata checks stay; the per-session token in the link gives way to a session cookie granted to any visit of the address (D60), and a changed provider address clears its key (D61). A switch can still turn the server off (D59). Other devices on the home network get in only when the player switches that on, and each only after entering a pairing code shown in game (G9). *Recommendation as written:* (a), because a listening port is attack surface even when hardened (§7.1) and Magelight moved network access to opt-in before its 1.0 (ML §3.16); §7.2's checks are what keep that surface small now.

**Owner's words.** "Browser dashboard will work the same way as SkyrimNet's, open the IP:port of the dashboard, always on."

**Later changes.** G9 (2026-11-07): other devices on the home network only behind a switch, each after pairing with a
code shown in game. G29 (2026-11-16): off game too, while EldenNet's launcher runs, served by `grace-serve` as one
library for hosts outside the game under every rule of §7.2 and invariant 4; the home-network switch and pairing stay in
the game at first (§7.2 item 26).

## G5

**NPC types listed from the start** · *2026-11-07 · Decided by the owner; the recommendation was (b)*

**Question.** NPC types: are types listed before the player has seen one? **Options:** (a) Every type listed, as EldenNet DESIGN §7.6 and O38 describe. (b) **Base kinds always listed; cards, creature families and other named slots listed once the Tarnished has seen one**, recorded per playthrough (`types_seen`), with a "show types I haven't met (may spoil)" switch.

**Decision.** (a), every type listed from the start. So NPC types lists every kind and type from the start, each with its label, its tags and a general profile: only what anyone would see on a first meeting with one, in our own words, reviewed for spoilers like shipped content; never a boss's name, and no tag names a place or a quest. This is a deliberate exception to invariant 2 and SP5 for the roster only (SP4, SP5). `ui.menu.show_unseen_types` and `types_seen` are dropped. What this playthrough has reached is on the new Characters › Playthrough page (§4.6.4, D63–D65), where invariant 2 holds in full. *Recommendation as written:* (b), because a full list names late-game creatures; the first-meeting rule and the spoiler review answer that now.

**Owner's words.** "we will feature a full roster with character biographies, so they will be part of EldenNet's database already." And, for G5 and G8 together: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown."

**Amends.** Invariant 2 and SP5, by a deliberate exception for the roster only; `ui.menu.show_unseen_types` and `types_seen` are dropped.

**Later changes.** *Widened the same day (EldenNet O43, as updated):* the roster's named slots include every named boss, by its first-meeting name (D67); type labels and tags still name no boss.

## G6

**A larger text scale on the Steam Deck** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Should the Steam Deck default to a larger text scale? **Options:** (a) `ui.scale` 1.0 everywhere, as now. (b) **On a Steam Deck, the first run writes `ui.scale = 1.35`**, detected through Steam's own `IsSteamRunningOnSteamDeck` in the already-loaded `steam_api64.dll` (the DLL O11's keyboard uses), shown and changeable in First light. (c) Ask in First light, with no detection.

**Decision.** (b), as recommended. *Why (as recommended):* at 1.0 on a 7-inch 1280 × 800 screen, menu text renders at about 11 device px and subtitles at about 22; at 1.35 menu text is about 15 px and the menu uses its drawer layout. `ui.scale` is one scale for all of EldenNet's text (NPC §10.7), so this changes the subtitles too, which makes it the owner's call.

**Owner's words.** "that's okay."

## G7

**Later-version features in the sidebar: "WIP"** · *2026-11-07 · Decided by the owner, as recommended, with the tag named "WIP"*

**Question.** Do later-version features appear in the sidebar? **Options:** (a) Hidden until they ship. (b) **Dimmed entries with a "v1.5" or "later" tag, each opening a statement page with no controls**: Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts; never for features that are not planned. (c) One "Coming later" page.

**Decision.** (b), with the tag "WIP". The four entries (Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts) are dimmed, carry a small "WIP" tag whose tooltip and accessible name say when ("Work in progress: arrives in v1.5" or "Work in progress: a later version"), and open their statement pages, which stay; AI › Models' dimmed Decisions card carries the same tag. Settings rows keep their availability badges ("Takes effect in v1.5", "Later version"), which say when a setting starts to work (§4.7, D62). *Why (as recommended):* it keeps the framework's categories recognisable to players who know SkyrimNet's (SND §1.1), tells players what is coming without controls that do nothing (§4.1 principle 3), and costs four rail entries; diaries, thoughts and scene memories stay one-line notes on their pages. A statement names a feature of EldenNet, never a part of the game, so it is no spoiler hint (SP5). (a) leaves Voice with one page and hides why there is no speech; (c) puts unrelated features in one place.

**Owner's words.** "agreed. but call it WIP."

*Later (2026-11-16):* EldenNet's decision calls came into v1 (its O84 engine; roadmap S8.1.f), on a `decisions` slot
that replaces the reserved `meta` slot, first for who speaks next at a shared place. So AI › Models' Decisions card is
no longer a later-version feature: from 0.4 it is a card like the others, with no "WIP" tag (DESIGN §4.6.2, D62).
Memories of scenes are in v1 too, one per witness (EldenNet MEM §2.3, §3.1), so only diaries and thoughts stay
one-line notes (DESIGN §4.6.5). The four rail entries and their tag are unchanged.

## G8

**Every named NPC's general profile shown from the start, and the Playthrough page** · *2026-11-07 · Decided by the owner; the recommendation was (b)*

**Question.** Are unmet published personas listed? **Options:** (a) Listed by their pre-reveal name, Allowances only, on Named NPCs and NPC types, and counted on Content packs (today's D12). (b) **Hidden on Named NPCs, NPC types, search and counts until met**, unless "Show characters and types I haven't met (may spoil)" (G5's switch, widened) is on; their allowances stay reachable through the `named` kind's own row on NPC types.

**Decision.** Every named NPC's general profile is shown from the start, met or not, and a new page tracks the playthrough. So Characters › Named NPCs lists the whole roster from the start, each by the name the game uses at a first meeting (the pre-reveal fallback, SP3), with its general profile, its Allowances and its Reactions. A general profile holds only first-meeting knowledge (who they appear to be on a first meeting, in our own words, reviewed for spoilers like shipped content): never a stage, fate, later form, reveal, quest step or relation that the story discloses later. This is the same roster exception to invariant 2 and SP5 as G5's. The new Characters › Playthrough page (§4.6.4, D63–D65) shows, for this playthrough only, the NPCs the Tarnished has met, each with their status as the game showed it, where they were last met, the line for the stage of their own story the Tarnished last saw, related stories as reached and the world's stage, with "since you last spoke" notes; invariant 2 and SP1–SP9 apply there in full. Named NPCs without a shipped general profile still appear once seen (SP4), and bosses and invaders stay unnamed until the game names them, because a boss's name is itself a reveal. Putting the right profile into each request is EldenNet's prompt assembly as designed (EldenNet DESIGN §7.7); a page never shows the prompt's private fields. D12 is superseded. *Recommendation as written:* (b), hiding unmet personas until met, because a greyed list of strangers is a hint even when the cast is published; the first-meeting rule answers that now.

**Owner's words.** "Same as G5, we will have them all ready, and the framework will inject correct NPC profile into the request payload prompt." And the follow-up quoted in G5: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown."

**Amends.** D12 is superseded; the same roster exception to invariant 2 and SP5 as G5's.

**Later changes.** *Widened the same day by the owner's answers to EldenNet's roster questions (EldenNet O43 as updated, O45):* every named boss and every named invader with a shipped bio is in the roster from the start, by its first-meeting name, with a first-meeting general profile (D67); bosses and invaders without a bio still stay unnamed until the game names them.

## G9

**Other devices on the home network: a switch and pairing** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** May other devices on the home network open the dashboard? **Options:** (a) **No**: this PC only (loopback), as G4's answer is built now. (b) **A switch, off by default** (`dashboard.lan`): the server also listens on the PC's home-network address, and each device pairs once with a code shown in game, never without one. (c) **A host field**, as SkyrimNet has: the server listens on whatever address the player types, with no pairing. **As put to the owner:** "the same as SkyrimNet by default, always on and opened at its address on your PC. Opening it from other devices would be a setting you switch on, and then a device must enter a short pairing code shown in game before it gets in."

**Decision.** (b). So the dashboard serves this PC only until the player switches `dashboard.lan` on, a device on the home network gets in only after entering a short pairing code shown in game, and use on this PC never needs a code. *How it works* (§7.2 item 16): `dashboard.lan` (off); when on, the server also listens on the PC's private network addresses only (never a public address, never every interface), and Windows asks once whether the game may accept connections on private networks; Host must be one of those addresses with the port, so DNS rebinding still fails; "Pair a device" in game shows a six-digit code for two minutes, one use, void after five wrong tries; a paired device keeps a random device cookie until the player forgets it, stored only as a hash in a file of its own in EldenNet's config folder (`devices.toml`, never `settings.toml`) and listed with its name and the date it paired; Forget revokes it at once and closes its connections; a paired device cannot set or clear keys or change providers, so a key is never typed over the network; Docs say that the home network carries the page in plain HTTP. *SkyrimNet, for comparison*, in answer to the owner's "G4: how does SN handle that issue?" (from the owner's screenshots and the sandboxed run of its dashboard, SND): its Settings › Dashboard › Web Server has an on switch, a host field that defaults to 127.0.0.1 and a port (8080, with 8081 for a legacy browser UI). Its page opens at that address with no link, sign-in or code. Other devices reach it only if the player changes the host to a network address, and we saw no login or pairing setting. We did not run its game-side server, so whether it checks Host or Origin is not known. *Recommendation as written:* (b), because the owner's "open the IP:port" fits a phone or a second screen beside the game, while on a network address Host and Origin checks no longer tell the player from anyone else on the network, so (c) without a pairing step would let any device on it read chat text and change settings; (a) needs nothing more; (c) is the least work and the least safe.

**Owner's words.** "Yes, approved."

**See also.** G4, whose answer raised it.

**Later changes.** 2026-11-23: built in GraceUI 0.2.25, as decided (DESIGN D221–D223): Other devices is a switch on
GraceUI › Browser view, off by default; Pair a device shows the code on a sheet in game, valid while the sheet is open
(D113); the device types its own name on its pairing page; each paired device is listed there with Forget, and a paired
device is refused every command a space marks sensitive.

## G10

**Which module owns the one overlay** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Now that GraceUI is a mod of its own (G2), which module owns the one overlay? **Options:** (a) **GraceUI is the UI host**: hudhook, its MinHook and the Present hook move into `graceui.dll`; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 through GraceUI's API, and its input detours go through GraceUI's MinHook. (b) **EldenNet keeps the overlay and loads `graceui.dll`**, which links no hudhook, ImGui or MinHook and hooks nothing: it lays out and tessellates the menu, and EldenNet checks its vertex lists and copies them into ImGui's draw list (§2.9). (c) **GraceUI's code stays linked into `eldennet.dll`**, and only its files (the web app, fonts, Docs) can be replaced by hand.

**Decision.** (b), as recommended. EldenNet keeps hudhook, MinHook, the Present hook and its own surfaces; `graceui.dll` links no hudhook, ImGui or MinHook, installs no hook, starts no thread and draws through EldenNet's overlay as vertex lists that `grace-host` checks (§2.9, D56). *Why (as recommended):* exactly one module may own hudhook, the Present hook and MinHook (EldenNet O25; ER02 §7.1). *The owner's "standalone mod":* (a) and (b) both give GraceUI a DLL and a folder of its own that a player can replace; (c) cannot update the in-game menu without an EldenNet release. *ImGui's one context and allocator:* ImGui keeps its current context and its allocator per copy of the library, so two DLLs drawing into one context need the same version, build and allocator handed across, and a hudhook build inside `graceui.dll` would bring a second MinHook; (b) keeps ImGui out of `graceui.dll`, while (a) makes every EldenNet surface call ImGui through GraceUI. *Rust and the C ABI:* Rust types, allocators and panics do not cross a DLL boundary, so either way the contract is `repr(C)` tables, borrowed buffers and caught panics; (b)'s is frames in and vertex lists out, one call a frame, while (a)'s covers every surface and hook of EldenNet's. *Crash isolation:* one process either way, so a hard fault in either DLL still ends the game; under (b) a caught panic or a malformed vertex list turns the menu off and subtitles, chat and call-outs keep drawing, while under (a) a fault in the host takes them too. *The hudhook pin:* (b) keeps it, R12's detours and every O25 rule in EldenNet; (a) moves them into GraceUI's releases, which players replace by hand. *Updates by file:* (b)'s update can change the menu's layout, drawing and pages but never a hook; (a)'s can change the hooks. *Other mods later:* (a) is the natural host for them; (b) can grow a host API later without moving the overlay; none is in v1 (§1.3). *What (b) costs:* a vertex-list contract that `grace-host` checks, GraceUI's own text field in place of ImGui's (D57), glyph tables handed over from EldenNet's atlas, and topics encoded in process (≤ 0.5 ms of T6 per revision, §8). The design follows (b) throughout.

**Owner's words.** "G10: approved".

**See also.** G2, whose answer raised it; EldenNet O25, every rule of which stays in EldenNet.

**Later changes.** 2026-11-08: superseded by G15 (a), decided by the owner: GraceUI owns the one overlay, so that any mod can use it without EldenNet; hudhook, its MinHook, the Present hook and the input hooks move into `graceui.dll` with every rule of EldenNet's O25, and EldenNet draws its surfaces through GraceUI. The design is folded in S7.8.c of EldenNet's roadmap (proposal 0001).

## G11

**The owner's request that started GraceUI** · *2026-11-07 · The owner's request; outside §12's table*

**Question.** None was put: the owner's request on 2026-11-07, which started this repository, recorded in DESIGN §14 (Sources).

**Decision.** A separate repository named GraceUI; a layout like SkyrimNet's dashboard; the appearance of SeverActions' menu (built on Magelight UI); Elden Ring tabs and settings; earlier the same day, a main EldenNet UI on a hotkey such as F5. Later the same day, on the first proposal: the layout was not yet like SkyrimNet's, and EldenNet is a framework, not a game plugin, so the menu should hold categories of an AI framework, taken from how SkyrimNet groups them, with what Elden Ring's version needs.

**Recorded in.** DESIGN §14 ("The owner's request"); the design answers it in D5, D38 and D39 (D4 is superseded by D39). The look is our own, in SeverActions' spirit (CLAUDE.md, Licences). For the research, the owner also asked that SkyrimNet's dashboard be rendered in a sandbox (EldenNet O52, superseded by G12).

## G12

**SkyrimNet: building on Min's work** · *2026-11-07 · Decided by the owner; outside §12's table*

**Question.** None was put: the owner's decision of 2026-11-07, recorded in EldenNet's CLAUDE.md that day as EldenNet O53, and in this repository's CLAUDE.md as it binds GraceUI.

**Decision.** SkyrimNet's author, Min, gives us the engine blueprint to work with and approves building on SkyrimNet, so its code, dashboard and styling may be used, copied and adapted, credited as Min wishes and with every licence and notice file that comes with it. Never present GraceUI as SkyrimNet itself or claim more endorsement than Min has given.

**Recorded in.** CLAUDE.md, Licences ("SkyrimNet (owner's decision, 2026-11-07, as in EldenNet's CLAUDE.md)"); EldenNet's `docs/DECISIONS.md`, O53, holds the full entry.

**Supersedes.** For GraceUI, the sandbox-only study of SkyrimNet's dashboard (EldenNet O52): structure only, the bundle's source never read.

**Later changes.** G19 (2026-11-16): for GraceUI, as a framework other mods use, no SkyrimNet-derived material enters
GraceUI, and `NOTICE` stays free of SkyrimNet entries; whatever EldenNet takes from SkyrimNet lives in EldenNet's own
pages, which G20 put in EldenNet's repository (DESIGN §3.17, CLAUDE.md).

## G13

**An interactive showcase of the menu** · *2026-11-08 · Decided by the owner*

**Question.** The owner asked on 2026-11-08 for an interactive showcase of the menu, a page to click through in the browser like Skyrizz's, beside EldenNet's static showcase. How is it built? **Options:** (a) **From the page descriptions**: write GraceUI's page descriptions (§2.5) now as data, with a settings description in the shape EldenNet will export (D8) and invented sample data, and draw them with a small renderer of the shared components (§2.5) in plain script, into one self-contained page with GraceUI's fonts bundled (§3.4); tested in a headless browser (no request outside the page, every page at two widths, no script error, no name of the game's characters or places, the spoiler and privacy rules, contrast); published privately on claude.ai and opened from file, with no server. The page descriptions and the sample data carry over into GraceUI 0.1 whatever its code becomes; the renderer is replaced by the web app. (b) **The real web app now**: start GraceUI 0.1's browser app (Preact, TypeScript and signals with Vite, G3) against `grace-dev`'s fixtures, and publish its build as the showcase. Nothing to keep in step, but the code starts before Min's blueprint shows how much of SkyrimNet's dashboard GraceUI adopts (G12). (c) **The static mock made clickable by hand**, as Skyrizz's showcase is drawn. The quickest, but it drifts from the design wherever no test pins it.

**Recommendation.** (a). It follows the Skyrizz session's advice of 2026-11-08 (one source for the pages, fonts bundled from the start, tests as plain pass and fail lines, one file per page), keeps §2.5's rule that the page descriptions are the one source of the pages, and commits no code base before the blueprint arrives. It would live in this repository (`showcase/`), linked from EldenNet's showcase; the static mock stays until the showcase covers its pages.

**As put to the owner.** The three options above, as "From page data", "Real web app now" and "Clickable mock", and a fourth: (d) **Wait for the blueprint**: "Keep the static showcase for now and decide after Min's engine blueprint arrives."

**Decision.** (d). The owner's answer: "Wait for blueprint". EldenNet's static showcase stays as it is; how the interactive showcase is built is asked again, as a new question, once Min's blueprint has arrived and what GraceUI adopts of SkyrimNet's dashboard is known (G12).

**Follows from.** G3, G12; §2.5; DESIGN §10's GraceUI 0.1 (page descriptions, fixtures and the mock).

**Later changes.** 2026-11-08, the same day: the owner decided that the interactive showcase is part of EldenNet, not GraceUI. The owner's words: "also, I intent to make GraceUI as a standalone just a UI that is capable to present EldenNet in-game, so the interactive UI should be part of EldenNet and not GraceUI." The recommendation's "in this repository (`showcase/`)" no longer applies; when the question is asked again after the blueprint, it is asked in EldenNet. What the same words mean for GraceUI's browser view is G14.

## G14

**GraceUI's scope: in game, and the browser view?** · *2026-11-08 · Decided by the owner (the scope); its consequences are open*

**Question.** The owner said on 2026-11-08 that GraceUI is to be "a standalone just a UI that is capable to present EldenNet in-game" (G13's later change). GraceUI's design today has two views of one set of pages: the menu in game on F5, and the same pages in the player's browser at the dashboard's address while the game runs, served by EldenNet (G3, G4, G9; §2.1). Does the browser view stay part of GraceUI? **Options:** (a) **Both views stay in GraceUI**: GraceUI is the one UI that presents EldenNet, in game and in the browser while the game runs; showcases and demos with sample data are EldenNet's. (b) **In game only**: GraceUI draws only the in-game menu; the browser dashboard becomes EldenNet's own, with its own web app, and G3, G4 and G9 move to EldenNet. (c) **In game only, and no browser dashboard in v1.**

**Recommendation.** (a). One set of page descriptions drives both views (§2.5, invariant 12), so splitting the views across two repositories would write every component twice in different places and let them drift; the server is already EldenNet's (`graceui.dll` opens no socket, G10), and GraceUI already depends on nothing of EldenNet.

**As put to the owner.** "Does the browser view stay part of GraceUI?", with (a) "Both stay in GraceUI (Recommended)", (b) "In game only" and (c) "No browser view in v1".

**Decision.** None of the options as put: the owner widened the question. GraceUI is to be a UI framework that other modders can use, as Magelight UI and PrismaUI are for Skyrim; EldenNet requires it as a dependency and always ships with it. The owner's words: "I want GraceUI to be something other modders can use, like Magelight or prismaUI for SKyrim. EldenNet will require it as dependency, but it will always ship with GraceUI anyway." What this changes (whose overlay draws in game, G10; what draws another mod's pages, G1; the browser view; the interface other mods use; which pages are EldenNet's own) is worked out in a proposal and put to the owner as new questions, G15 onwards. Until they are answered, the decided design stands.

**Follows from.** G3, G4, G9, G10, G13.

**Amends.** G11 (GraceUI as EldenNet's menu and dashboard): GraceUI becomes a framework for any mod, and EldenNet's menu and dashboard become its first user.

## G15

**Who owns the one overlay** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** Now that GraceUI is a framework other mods can use (G14), which module owns the one hudhook overlay, its MinHook, the Present hook and the input hooks? **(a) GraceUI** (recommended): they move from EldenNet into `graceui.dll`, with every rule of EldenNet's O25; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 view through GraceUI like any mod, and links no hook library. **(b) EldenNet, as G10 decided**: GraceUI keeps drawing through EldenNet's overlay, and other mods can use GraceUI only while EldenNet is loaded and active, through a host API EldenNet grows later. **In plain words:** Only one copy of the overlay library GraceUI and EldenNet use can run safely in the game. Should that copy be GraceUI's, so any mod can use it on its own, or stay EldenNet's, so GraceUI only works for other mods while EldenNet runs?

**Recommendation.** (a). One process holds one hudhook and one MinHook safely, and every shared host that works in this game is one module that others call (FW F1, F2); a framework other modders use cannot depend on an AI mod that starts offline through its own launcher; your words "EldenNet will require it" then hold literally; nothing is built, so moving now costs a design pass, while moving later would rewrite EldenNet's surfaces twice. *What (a) gives up:* G10's isolation (a fault in GraceUI's host stops EldenNet's subtitles and chat as well as the menu); EldenNet cannot show anything, not even its banner, without GraceUI, so its launcher checks GraceUI first; the hook code ships in the releases players replace by hand; GraceUI's first increment must be ready for EldenNet v0.1.

**What each answer changes.** (a) amends G10 and EldenNet O25 (the pin and its rules move to GraceUI; both CLAUDE.md files change), rewrites invariant 16 as "one host, one hook set", replaces invariant 15's "EldenNet keeps playing" and amends G2's "keeps playing whatever it finds" (D88); DESIGN §2, §3.16, §5 and §8 are rewritten around the host; D69 to D72, D77, D86 and D88 apply; §5 of this proposal's items 1 to 7 and 12 go to eldennet-changes; GraceUI gains a 0.1 Host increment with EldenNet v0.1. (b) keeps G10, O25 and invariants 15 and 16 as they are; the framework is limited to EldenNet's presence; G17's recommendation becomes (b).

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: GraceUI owns the one hudhook overlay, its MinHook, the Present hook and the input hooks, with every rule of EldenNet's O25; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 view through GraceUI like any mod, and links no hook library.

**Owner's words.** "GraceUI (Recommended)".

**Amends.** G10 (superseded), EldenNet O25 (the pin and its rules move with the overlay), G2 ("keeps playing whatever it finds", D88) and invariants 15 and 16, all rewritten when the design is folded (S7.8.c).

**Later changes.** 2026-11-08: by EldenNet's O60 (the owner's direction), EldenNet and GraceUI's in-game parts are expected to be written in C++, settled when Min's blueprint is adopted; hudhook (Rust) would then give way to a C++ overlay that GraceUI owns, with every rule of O25 restated for it and the owner's approval. The same day Min confirmed the blueprint is C++, so C++ is settled (O60's later change); the overlay itself is chosen at adoption. 2026-11-16: the owner chose it (EldenNet's O25 as changed that day): Dear ImGui 1.93.0 with its DX12 and Win32 backends and MinHook 1.3.4, pinned, in `grace-host` in place of hudhook 0.9.3; every rule of G15 stands with them (folded in EldenNet's roadmap S8.1.e). Owner's words: "Dear ImGui + MinHook (Recommended)".

## G16

**What other mods write, and what draws it in game** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** What does a mod that uses GraceUI provide? **(a) Page data and panels** (recommended): its pages and settings described as data in GraceUI's components (drawn in game with the Grace theme and, if it opts in, in the browser), topics and commands in the bridge protocol, and, for anything else, in-game panels it draws itself as checked vertex lists, with GraceUI's drawing kit or its own headless Dear ImGui with the default vertex layout and 16-bit indices (checked in 1.89.2 and 1.92.6; other builds convert before submitting, and the checker refuses anything else); no HTML in game. **(b) Page data only**: no panels for other mods (EldenNet's own surfaces still use them). **(c) HTML pages in game**, as Magelight UI and PrismaUI offer: this reopens G1 and needs a hudhook patch (an O25 change), Ultralight's licence and about 57 MB, or another engine. **In plain words:** Magelight and PrismaUI let modders write web pages. GraceUI would instead let them describe their pages as data, which GraceUI draws in the game's style and in the browser, plus panels they draw themselves. Is that the right offer, or should mods get web pages in game?

**Recommendation.** (a). G1's reasons apply to every mod: the pinned hudhook cannot update an image without leaking memory and stalling the frame (REN §2.1). Page data shows in both views from one source and keeps other mods' code out of the browser; panels let a mod draw anything without sharing ImGui's internals, which differ between versions (FW §4); EldenNet needs panels anyway. *What (a) gives up:* modders cannot bring HTML, CSS or React; pages are limited to GraceUI's components; panels show only in game; pictures are static, so no video or live images.

**What each answer changes.** (a): D75 and D76 apply; G1 extends to every mod unchanged. (b): D76 serves EldenNet only and the guide drops panels; mods with needs beyond the vocabulary cannot use GraceUI. (c): G1 is reopened with its three approvals (licence, hudhook patch, size), invariant 11 and REN §2's findings are revisited, and S4 gains an HTML spike.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: a mod provides its pages and settings as data in GraceUI's components (drawn in game in the Grace theme and, if it opts in, in the browser), topics and commands in the bridge protocol, and in-game panels it draws itself as checked vertex lists (GraceUI's drawing kit, or its own headless Dear ImGui in the default vertex layout); no HTML in game.

**Owner's words.** "Page data + panels (Recommended)".

**Amends.** G1 extends to every mod unchanged; D75 and D76 apply.

## G17

**The browser view: a GraceUI feature for every mod?** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** The browser dashboard (G4, G9) shows the same pages as the menu. Who serves it? **(a) GraceUI, for every mod that opts in** (recommended if G15 is (a)): the local server and the web app move into `graceui.dll` with every rule of §7.2 and pairing; EldenNet's dashboard is its space there, always on as G4 decided. **(b) EldenNet, for its own pages only** (recommended if G15 is (b)): GraceUI keeps the web app and the server as a kit that EldenNet runs, as designed now; other mods have no browser view. **(c) EldenNet's own web app**: GraceUI is in game only, and the browser dashboard, G3, G4 and G9 move to EldenNet. **In plain words:** Should the "open it in your browser" dashboard be something GraceUI offers any mod, or stay EldenNet's alone?

**Recommendation.** (a), given G15 (a). One hub: each mod publishes once and both views show it, from one page source; no other Elden Ring host offers a browser view (FW F7). Your words ("a UI that is capable to present EldenNet in-game", "like Magelight or prismaUI") put the game first, and (a) keeps that: the browser shows a mod's pages only when the mod asks. (c) would write every component twice, in two repositories (G14's recommendation). The study behind this proposal leaned the other way, towards (b), for your in-game words and no framework-wide port; this recommendation weighs the one hub higher. *What (a) gives up:* `graceui.dll` opens a network port on the PC and runs threads, and a mod's command bug becomes reachable from the browser as it is from the game. (b) keeps, even with G15 (a), a `graceui.dll` that opens no socket, and G4's port as EldenNet's alone behind its gate; other mods' authors still get a browser test bench under either answer (`grace-dev`, D85).

**What each answer changes.** (a): G4, G9, D58 to D60 and D66 move with the server to GraceUI, and D61 stays EldenNet's, with its pages (D82); invariant 4 is GraceUI's server; D81 applies, with §7.2 item 1's gate clause kept as §4.9 of this proposal's four rules; §5 of this proposal's item 7 goes to eldennet-changes. (b): the server stays in EldenNet as designed (eldennet-changes §3 unchanged), and other mods' pages are in game only. (c): the web app, G3, G4 and G9 move to EldenNet, invariants 4 to 6 and 12 become EldenNet's, and the components are written in both repositories.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: GraceUI serves the browser view for every mod that opts in, with every rule of the local server (§7.2) and pairing (G9); EldenNet's dashboard is its space there, always on as G4 decided.

**Owner's words.** "Yes, opt-in (Recommended)".

**Amends.** G4, G9, D58–D60 and D66 move with the server into GraceUI; D61 stays EldenNet's; invariant 4 becomes GraceUI's server's; D81 applies, with §7.2 item 1's gate clause kept as proposal 0001 §4.9's four rules.

## G18

**When the framework opens to other modders** · *Open since 2026-11-08 · Decided by the owner 2026-11-16, as recommended*

**Question.** **(a) After EldenNet v1.0, as GraceUI 1.1** (recommended): the ABI is designed now, EldenNet uses it from GraceUI 0.1, an example mod of ours exercises it in CI, and the guide, the examples and a frozen ABI 1 are published with 1.1. **(b) With GraceUI 1.0**, at EldenNet v1.0. **(c) Early**, as a preview marked unstable from GraceUI 0.2. **In plain words:** Should other modders get GraceUI only once EldenNet 1.0 is out, at the same time, or as early as possible with a warning that it may change?

**Recommendation.** (a). EldenNet v1 is to be "deliberately minimal and stable"; a public ABI is a promise kept for years, and it is better frozen after a real user has driven it through a full release; Magelight broke its API twice before its first public release, in 0.29.0 and 0.30.0 (Magelight `CHANGELOG.md:1135-1210`, `:1290`, via the Skyrizz session, 2026-11-08). *What (a) gives up:* other modders wait about one release, and early feedback from them is lost.

**What each answer changes.** (a): §6's 1.1 Open row; the guide and examples after v1.0. (b): those move into 1.0's exit checks. (c): a preview label, a changelog of breaking changes and support questions from 0.2; the ABI's freeze waits for 1.0 either way.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**Decision (2026-11-16).** (a): the framework opens to other modders after EldenNet v1.0, as GraceUI 1.1. Its ABI is
designed now and EldenNet uses it from GraceUI 0.1, an example mod of ours exercises it in CI, and the guide, the
examples and a frozen ABI 1 are published with 1.1; until then ABI 1 is a preview.

**Owner's words.** "After EldenNet 1.0 (Recommended)", asked when other modders should get GraceUI.

## G19

**SkyrimNet-derived material and other mods** · *Open since 2026-11-08 · Decided by the owner 2026-11-16, as recommended*

**Question.** G12 lets GraceUI use SkyrimNet's code, dashboard and styling, as Min approved for building on SkyrimNet. If GraceUI is a framework, anything SkyrimNet-derived in it reaches every mod and player that uses it. **(a) Not until confirmed** (recommended): GraceUI keeps to its own code and the Grace theme; whatever is adopted from Min's blueprint for EldenNet's pages lives in EldenNet; once the blueprint arrives, you ask Min, if you wish, whether what GraceUI would adopt may be passed on through a public framework. **(b) Yes**: G12 covers GraceUI as a framework, credited as Min wishes. **In plain words:** Min agreed that EldenNet and GraceUI may build on SkyrimNet. Does that agreement also cover other people's mods getting SkyrimNet-derived parts through GraceUI?

**Recommendation.** (a). G12 and EldenNet O53 record Min's approval for EldenNet's foundation; neither says whether it reaches third parties, and claiming more endorsement than Min has given is ruled out; GraceUI's look is our own anyway (D3). *What (a) gives up:* the question G13 will ask after the blueprint can place adopted parts only in EldenNet's pages, not in the shared components, so the web app may take longer.

**What each answer changes.** (a): §3.17's last line is aligned with G12 by saying SkyrimNet-derived material stays out of GraceUI until confirmed; NOTICE stays free of SkyrimNet entries. (b): §3.17's line is replaced by G12's rule for GraceUI as a framework, and NOTICE carries SkyrimNet's credits and licences as Min wishes.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**Decision (2026-11-16).** (a): GraceUI keeps to its own code and the Grace theme. No SkyrimNet-derived material enters
GraceUI, and NOTICE stays free of SkyrimNet entries; whatever EldenNet takes from SkyrimNet lives in EldenNet's own
pages. Asking Min whether a public framework may pass such parts on stays possible, as a new question. Since EldenNet's
O84 there is no blueprint to adopt from, and since G30 the server is our own too.

**Amends.** G12, for GraceUI (its later changes); §3.17's last line.

**Owner's words.** "No, keep it our own (Recommended)", asked whether GraceUI, as a framework other mods use, should
also carry SkyrimNet-derived parts.

## G20

**Where EldenNet's pages live** · *Open since 2026-11-08 · Decided by the owner 2026-11-16, as recommended*

**Question.** G14 leaves open which pages are EldenNet's own once GraceUI is a framework. EldenNet's menu and dashboard content (its categories, pages, settings description, help texts and Home cards), its fixtures, the spoiler, roster and Playthrough goldens, the spoiler rules SP1–SP9, EldenNet's halves of the privacy rules PV1–PV8 and DESIGN §4: **(a) In EldenNet's repository** (recommended), as content EldenNet ships and registers with GraceUI at start; GraceUI keeps the frame, the components, the theme, PV3, PV6 and PV7 as framework rules, and a demo space with invented content (D82). **(b) In GraceUI's repository**, as its first user's bundled pages, as designed now. **In plain words:** EldenNet's own pages, and the rules that keep them free of spoilers, could move to EldenNet, so that GraceUI holds only the parts every mod shares, or stay in GraceUI as now. Which should it be?

**Recommendation.** (a). Your words "the interactive UI should be part of EldenNet and not GraceUI" (G13's later change); the spoiler rules are EldenNet's domain; a framework other modders use should not carry one user's content and its spoiler review. *What (a) gives up:* one repository for the whole information architecture; GraceUI's page lint and parity tests then run on EldenNet's pages in EldenNet's CI against a pinned GraceUI, and a change to a component and to EldenNet's pages can land in two repositories.

**What each answer changes.** (a): D82 applies; DESIGN §4, SP1–SP9, EldenNet's halves of PV1–PV8, D61, its fixtures and goldens move to EldenNet in S7.8.c; invariants 1 and 2 take their framework form in GraceUI and their EldenNet form in EldenNet; §5 of this proposal's item 9 goes to eldennet-changes. (b): DESIGN §4, SP1–SP9, the goldens and invariants 1 and 2 stay in GraceUI as they are, beside the framework rules; GraceUI's release carries EldenNet's page data as a bundled space that EldenNet's `connect` activates, and the framework carries one user's content.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**Decision (2026-11-16).** (a): EldenNet's pages live in EldenNet's repository, as content EldenNet ships and registers
with GraceUI at start: its menu and dashboard content (its categories, pages, settings description, help texts and Home
cards), its fixtures, the spoiler, roster and Playthrough goldens, the spoiler rules SP1–SP9, EldenNet's halves of
PV1–PV8 and DESIGN §4. GraceUI keeps the frame, the components, the theme, PV3, PV6 and PV7 as framework rules, and a
demo space with invented content (D82). The move is a fold of its own (EldenNet's roadmap S7.8.d).

**Amends.** DESIGN §4, §9 and §13, folded on 2026-11-16: EldenNet's content, SP1–SP9, PV1, PV2, PV4, PV5 and PV8, D61
and the goldens moved to EldenNet's PAGES under the same numbers; D82 came into force; invariants 1 and 2 took their
framework form (CLAUDE.md).

**Owner's words.** "In EldenNet (Recommended)", asked where EldenNet's own pages and the spoiler rules that guard them
should live.

## G21

**What is the map drawn on?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, as recommended*

**Question.** **(a) The player's own game map, read from their game at run time, masked and never shipped, with GraceUI's drawn base as the fallback when it cannot be read or does not fit** (recommended), and a setting to choose the drawn base or none instead. **(b) GraceUI's drawn base only.** **(c) An image the player exports from their own install with a tool on their PC**, loaded from a folder they choose.

**Recommendation.** (a). It is what the owner asked for ("render the map image from the game process", with "a toggle option" for custom maps), it ships nothing of the game, and the fallback keeps the map working whatever the spike finds. *What (a) gives up:* it depends on a route the spike has yet to find; until then the drawn base is the map.

**What each answer changes.** (a): §3 as written; S7.9.c's image route; D90, D93, D94. (b): no image reader, no cache, no fit check; G26 falls away. (c): a player-side export step and its instructions; the fit check stays.

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**The owner's requirement (2026-11-14), on every answer.** "we will have to add the exact shape of the map, it's parts
(so the areas match the map shape and fit into it), accurate placements. That is a must even if we ship our own
texture." Every base therefore has the exact shape, areas and placements; GraceUI's drawn base is built on the
player's PC from their own game and no shape ships (proposal 0002 §3, D97). Under (b) the drawn base still needs the
player's game as its source, so (b) no longer avoids the image reader.

**Later (2026-11-15): what the research found.** The recommendation stands, now concrete: the base is the game's own
map tiles, composed as the game composes them (each tile in the variant for the map pieces the player holds, at
each zoom level), read from the player's install at run time and never shipped. The image's formats, the game's own
masks, the zoom levels' sizes and every variant the game draws are found and checked on the owner's PC; the risk left
is EldenNet's own run-time read of the game's archives. (c) is proven too, but asks each player to unpack the game
with a tool, so it stays a developer's route for testing. Placements and areas now have exact sources (proposal 0002
§14; EldenNet WHY§61 to WHY§82).

**Decision (2026-11-15).** (a): the player's own game map, its tiles composed as the game composes them for the map pieces the player holds, at each zoom, read from their install at run time and never shipped, with GraceUI's drawn base as the fallback and a setting to choose the drawn base or none (proposal 0002 §3, §14).

**Owner's words.** "Your game's own map (Recommended)".

## G22

**May anything on the map loop or pulse?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, as recommended*

**Question.** **(a) No: still glow, hover brightening and one-shot flares; invariant 14 unchanged** (recommended). **(b) A slow ambient shimmer in the browser only**, behind a setting. **(c) Loops in both views**, behind a setting.

**Recommendation.** (a). A still glow reads as high quality without the cost and distraction of motion that never stops; the in-game frame budget (invariant 10) has no room for per-frame animation; one rule for both views keeps them alike (invariant 12). *What (a) gives up:* the living shimmer some map mods use.

**What each answer changes.** (a): §7 as written. (b): invariant 14 gains a browser-only exception and a setting. (c): invariant 14 is replaced by a setting, with a per-frame cost budget in game.

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**Decision (2026-11-15).** (a): no loops; still glow, brightening on hover and a single flare for new things; invariant 14 unchanged.

**Owner's words.** "No: still glow (Recommended)".

## G23

**Who writes the lore notes?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, not as recommended*

**Question.** **(a) The model proposes notes from the player's memories, each citing them, and a note joins the map only if the player keeps it** (recommended). **(b) The player writes every note**; no model call. **(c) The model writes notes on its own**, and the player may remove them.

**Recommendation.** (a). It is the owner's idea ("memories that are relevant and could be added as lore") while keeping the player in charge of their own map; notes stay grounded, because each cites memories the player actually has. *What (a) gives up:* a little effort: nothing appears until kept.

**What each answer changes.** (a): §8; one batched proposal job. (b): no model call; an editor only. (c): the job writes directly; a "remove" action and a setting to turn it off.

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**Decision (2026-11-15).** (c), as the owner put it: the model adds lore notes to the map on its own, each from two or more of the player's memories tied to one place or person and citing them, through the lock filter (SP2), batched on a light slot under EldenNet's pacing; the player may edit or remove any, and a removed note is not proposed again from the same memories. A setting, **Lore notes by AI**, is on by default and turns it off (Settings › Map, D95).

**Owner's words.** "Let AI add notes automatically, a setting on by default."

*Later (2026-11-16):* EldenNet's O86 keeps spoilers by grounding alone, so the lock filter (SP2) that this decision
cites is gone: a note is written from the player's memories, which hold only what each NPC could know, and SP2 as
re-cut (DESIGN §4.8) shows only what the game showed or EldenNet wrote from the same grounded inputs. The rest of the
decision stands.

## G24

**When does the map ship?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, as recommended*

**Question.** **(a) After EldenNet v1.0, as part of 1.1**, with the spike done early as research (recommended). **(b) In v1.0, the browser view on the drawn base only**; the game's map and the in-game view in 1.1. **(c) All of it in v1.0.**

**Recommendation.** (a). EldenNet v1 is to be "deliberately minimal and stable"; the map depends on the spike's answer, on the framework's panel kinds (G16) and on the design fold (S7.8.c). The pieces v1 needs anyway (the sightings record behind Playthrough, the world stages of O51) land in v1 and make 1.1's map cheaper. *What (a) gives up:* players wait one release for the map.

**What each answer changes.** (a): the build is planned for 1.1; S7.9.c early. (b): the panel kind and the browser view move into v1.0's scope; the image reader waits. (c): all of §10 moves into v1.0's scope.

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**Later (2026-11-15).** The recommendation stands. The research found the image route and the placements (proposal 0002
§14), so the riskiest part of a 1.1 map is mostly solved; the panel kind, the design fold and EldenNet's own read of
the game's archives remain.

**Decision (2026-11-15).** (a): after EldenNet v1.0, as part of 1.1, its groundwork (the sightings record, the world stages) built for v1.0 anyway. Asked first: "is there a drawback to building in v1.0?"; told the drawbacks (v1.0 later by the largest single feature, the run-time read of the game's archives a v1.0 risk, more in-game checks), the owner chose (a).

**Owner's words.** "In 1.1, after v1.0 (Recommended)".

## G25

**What does the map key open?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, as recommended*

**Question.** **(a) A setting, off by default: the map key opens the game's map, or GraceUI's map when the player switches it on**; with it on, the key pressed again or held opens the game's map (recommended). **(b) Always the game's map**; GraceUI's map only from the menu and the browser. **(c) GraceUI's map by default.**

**Recommendation.** (a). The owner's words ("could be a toggle setting"); fast travel and markers stay in the game's map in v1, so the default should keep them one press away as they are now. *What (a) gives up:* players who would prefer our map must find the setting.

**What each answer changes.** (a): §6; S7.9.c's input checks. (b): no key capture for the map. (c): as (a), with the default flipped and a first-run notice.

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**Decision (2026-11-15).** (a): a setting, off by default; with it on, the map key opens GraceUI's map, and pressed again or held opens the game's map, where fast travel and markers stay in v1.

**Owner's words.** "A setting, off by default (Recommended)".

## G26

**In game, how is the game's map kept to what the player has reached?** · *Open since 2026-11-14 · Decided by the owner 2026-11-15, as recommended when put again*

**Question.** hudhook 0.9.3 leaks one buffer per texture upload, so the in-game map can be uploaded only once (invariant 11), while new areas are reached during play. **(a) Upload the whole image once at start, at a reduced size, and mask it at draw time by the reached areas' shapes** (recommended): invariant 1 gains one narrow, tested exception, this texture, which no view shows unmasked and no projection, log or report carries. **(b) Upload it once, masked as at start**; areas reached during the session show on the drawn base until the next start. **(c) The game's map in the browser only**; in game the drawn base.

**Recommendation.** (a). One upload per session, new areas show the moment they are reached, and the guarantee that matters (nothing unreached is drawn, counted or hinted at) holds and is tested by rendering goldens at fixture states. *What (a) gives up:* invariant 1's "never receives" for one texture; a debugger reading video memory could see the whole map.

**What each answer changes.** (a): invariant 1 gains its exception and a pixel golden; D90. (b): a second style in game until restart. (c): the in-game map is the drawn base only.

**Later (2026-11-15): put again, with new options.** The game reveals its map by map piece (32 in all), not by places
visited, and each tile has a variant for each set of pieces, so nothing unrevealed need ever be held, drawn or sent.
**(a) As the game shows it: the in-game texture is composed at start from the tiles for the pieces held, and composed
and uploaded once more when the player gains a piece**, each such upload leaking one buffer of hudhook 0.9.3 until
the game restarts, a narrow, measured exception to invariant 11 (recommended). **(b) Composed once at start**; a
piece gained shows from the next start. **(c) The game's map in the browser only.** The recommendation is (a): the map
matches the game's at once, and invariant 1 stands whole, which the old (a) bent. The old options are withdrawn
(proposal 0002 §14.4).

**Follows from.** The owner's request for an interactive map (2026-11-14); [proposal 0002](proposals/0002-map.md) §13, with its reasons in §3–§8.

**Decision (2026-11-15).** (a) as put again on 2026-11-15: as the game shows it. The in-game texture is composed at start from the tiles for the map pieces the player holds, and composed and uploaded once more when a piece is gained, each such upload leaking one buffer of hudhook 0.9.3 until the game restarts: invariant 11 gains that narrow, measured exception, with a test counting the uploads; invariant 1 stands whole (proposal 0002 §14.4).

**Owner's words.** "Update on a piece gained (Recommended)".

## G27

**How does the map show terrain?** · *2026-11-14 · Decided by the owner, as recommended*

**Question.** The exact land cannot ship (proposal 0002 §3, D97), so terrain is built on each player's PC from their
own game on first use (a few minutes, then cached), drawn in GraceUI's style. **(a) A local 3D relief and our own
sprites** (recommended): the exact terrain's heights built locally, shown as a 3D relief in the browser (tilt, rotate,
zoom; marks and threads on the terrain) and as a relief image in game, uploaded once at start; our own terrain sprites
and the tile-grid schematic ship as the fallback. **(b) A local 2D relief and our sprites**: top-down only, in both
views. **(c) Our sprites only**: no terrain built from the game.

**Recommendation.** (a): the owner's idea (a 3D map read from the game, textures rendered on top), with nothing of the
game shipped; the browser has room for 3D, the in-game view keeps to one upload.

**Decision.** (a).

**Owner's words.** "Local 3D relief + our sprites (Recommended)".

**Follows from.** The owner's messages of 2026-11-14 on topography and a 3D map; [proposal 0002](proposals/0002-map.md)
§3; D97.


## G28

**May a space take a file from the browser view?** · *Open since 2026-11-15 · Decided by the owner 2026-11-15, as recommended*

**Question.** EldenNet's O67 (a) lets its plugins also be added on the dashboard: a drop zone with an Add file button on
the Content packs page, in the browser only, this PC only, never for code (D103). GraceUI's frames are capped at 16 KiB
(§6.8), so a file needs a route of its own. **(a) A narrow upload route** (recommended): one route on the loopback
listener only, for a space's commands that take a file and are marked sensitive (EldenNet's `plugins.install` first);
one file at a time, at most 32 MiB (EST); every check of §7.2 (Host, Origin, Fetch Metadata, the session cookie) and a
one-use ticket for that upload; refused from any paired device; the bytes handed to the space's command queue and kept
by GraceUI no longer than the call; the server suite covering it. **(b) No uploads**: EldenNet's plugins come through
its Plugins folder alone, and EldenNet's O67 works as its (b). **In plain words:** Should the dashboard in your browser
be able to hand a plugin file to EldenNet, from this PC only, or should plugins only ever go in through the folder?

**Recommendation.** (a). It is what O67 chose, and a route this narrow keeps invariant 4 whole: loopback only, the same
checks as every command, one ticket per upload, nothing kept. *What (a) gives up:* a second kind of request on the
server, which the security suite must cover.

**What each answer changes.** (a): §7.2 gains an item for the upload route, §6 a note on how a file travels, and the
server suite its cases; D103's drop zone works. (b): D103's drop zone falls away, and the page names EldenNet's Plugins
folder instead, by its relative name (PV5).

**Follows from.** EldenNet's O67 (2026-11-15); EldenNet's proposal 0001 §6.2.

**Decision (2026-11-15).** (a): one narrow upload route on the loopback listener alone, for a space's file commands
marked sensitive (EldenNet's `plugins.install` first); one file at a time, at most 32 MiB (EST); every check of §7.2 and
a one-use ticket; refused from any paired device; GraceUI keeps nothing after handing the bytes to the space (§7.2 item
18).

**Owner's words.** "Narrow upload route (Recommended)".

**Later changes.** 2026-11-23: built in GraceUI 0.2.26 (protocol 1.9, ABI 1.6), as decided (DESIGN D226–D228): a space
names its file commands, each sensitive; an upload block on its page is a drop zone with a button in the browser on this
PC, and a line saying where to add the file in game and on a paired device; the page asks for a one-use ticket over its
connection, then sends the bytes to `POST /upload` on the loopback listener, where GraceUI checks their size and SHA-256
before handing them to the space, which gets them through ABI 1.6 and answers as for any command.

## G29

**GraceUI's server for hosts outside the game?** · *2026-11-15 · Decided by the owner 2026-11-16, as recommended*

**Question.** EldenNet's owner asked for its dashboard in the browser with the game closed, and for the EldenNet
devkit's tools in the browser (EldenNet's proposal 0002, O76 and O78). Both hosts run outside the game, where
`graceui.dll` is not loaded. **Options:** (a) **one server library GraceUI publishes** for hosts outside the game (EldenNet's
launcher, the devkit), with every rule of §7.2 and invariant 4: loopback only; Host, Origin, Fetch Metadata, the session
cookie and the ticket checked; a strict CSP; only the files listed served; the home-network switch and pairing (G9)
staying in the game at first; (b) each host writes its own server.

**Recommendation.** (a): one implementation of the server's protections, tested once.

**What each answer changes.** (a): invariant 4 and §7.2 cover a server outside the game; G4 widens to off game while
EldenNet's launcher runs, if EldenNet's O76 is (a) or (b). (b): nothing in GraceUI; each host carries the rules itself.

**Follows from.** G4, G9, G17; EldenNet's O76 and O78.

*Later (2026-11-15):* EldenNet's O76 decided (b): its launcher runs EldenNet's engine off game, once the engine is
adopted, and serves the dashboard from the saved files; its O78 (b) and O79 put the devkit's tools, viewers included,
in EldenNet's dashboard behind Developer mode. So a host outside the game will serve GraceUI's pages either way; G29
asks only how.

**Decision (2026-11-16).** (a): GraceUI publishes its server (`grace-serve`, our own since G30) as one library for hosts
outside the game, EldenNet's launcher first, with every rule of §7.2 and invariant 4: loopback only; Host, Origin, Fetch
Metadata, the session cookie and the ticket checked; a strict CSP; only the files listed served. The home-network switch
and pairing (G9) stay in the game at first, and G4 widens to off game while EldenNet's launcher runs. The fold is
EldenNet's roadmap S7.12.e.

**Amends.** G4 (its later changes), invariant 4 and §7.2 (item 26), folded on 2026-11-16.

**Owner's words.** "Reuse GraceUI's server (Recommended)", asked how EldenNet's launcher and the devkit should serve the
pages outside the game.

## G30

**The C++ basis of the loopback server** · *Open since 2026-11-16 · Decided by the owner 2026-11-16, as recommended*

**Question.** With GraceUI's in-game parts in C++ (EldenNet's O60), `grace-serve`, the loopback HTTP and WebSocket
server of §7.2, needs a C++ basis. **Options:** (a) **our own small server** on Windows sockets, serving only the narrow
subset that §7.2 and §6.8 allow (HTTP/1.1 GET and POST with a Content-Length and caps, no chunked request bodies, one
WebSocket per tab per RFC 6455), fuzzed in CI; (b) civetweb (MIT, C), with its file serving off and every handler
wrapped by §7.2's checks; (c) Boost.Beast (BSL-1.0), which brings Boost and Asio with it.

**Recommendation.** (a): every rule of §7.2 stays in our own code, the surface is only what the rules allow, and no
server library needs patching; the risk, bugs in our own parsing, is met by fuzzing and §6.8's limits.

**What each answer changes.** (a): `grace-serve` is our own code, with a fuzzer for its parser in CI. (b) or (c): the
library is pinned, listed in `NOTICE` and tracked for security fixes, and `grace-serve` wraps it. §7.2's rules and
invariant 4 hold whichever is chosen.

**Follows from.** G4, G9, G17, G28; EldenNet's O60.

*Later (2026-11-16):* the owner asked for a second agent's view before deciding; a request with §7.2's rules, the
options and the questions was written for it.

**Decision (2026-11-16).** (a): our own small server, on the narrow subset that §7.2 and §6.8 allow, built to the plan
of the independent review (research note SRV, §5): the HTTP head parser and the WebSocket frame decoder are pure
functions over bounded buffers, fuzzed in CI; one non-blocking I/O thread and one worker; `Connection: close` on every
HTTP response; static paths matched to the manifest byte for byte; RFC 9112's and RFC 6455's strict rules; WebSockets
capped at two, with plain HTTP connections capped on their own (§6.8, corrected the same day).

**Owner's words.** "Our own server (Recommended)".

**Evidence.** [research/server-basis.md](research/server-basis.md) (SRV): the review recommends the same, with
Boost.Beast as the runner-up, and found that every general-purpose library it checked shipped parser or framing fixes
in 2025-26.

## G31

**The menu's pointer** · *Open since 2026-11-20*

**Question.** In game, the menu and a mod's panel that holds the mouse show Dear ImGui's own arrow, drawn by the host
(§5.5): a white arrow with a black edge, which looks nothing like the rest of Grace. Asked at the look review whether it
could be the same as Elden Ring's own pointer. **Options:** (a) **our own pointer in the Grace look**: a slim arrow drawn
as vector shapes from the tokens (`gold.hi` edge on a dark fill, Grace Clear's thicker), at the effective scale, with
a text beam over fields; drawn where Dear ImGui's is today, so it never lags; (b) keep Dear ImGui's arrow; (c) the
game's own pointer, taken from the player's install at run time.

**Recommendation.** (a). The game's pointer image can never be in our files (no game textures, icons or frames in a
repository or a release). (c) would take it from the player's install at run time instead, which GraceUI cannot do
itself (it reads nothing of the game; what comes from the install comes through EldenNet): EldenNet's devkit would have
to read the game's archives with the player's own keys (EldenNet's O73 to O75) and hand the image to GraceUI, a large
piece of work for one pointer, and one that other mods would not have. (a) can share the game's spirit (a fine, warm,
unobtrusive pointer) without copying it, as the rest of Grace does, and costs one shape in grace-draw and a golden.
*What (a) gives up:* an exact match with the game's pointer.

**What each answer changes.** (a): §5.5's first line names the Grace pointer; grace-draw gains its shape and the host
draws it in Dear ImGui's place; a golden and a harness check. (b): nothing. (c): a request to EldenNet for the devkit's
run-time read, a texture EldenNet registers for GraceUI at load, and (a) as the fallback when EldenNet is absent.

**Follows from.** The owner's look review of the in-game menu (EldenNet's roadmap S11.2.d), 2026-11-20; §3, §5.5.

*Later (2026-11-20):* the owner sent a cursor of another game's theme as a reference ("how about cursor like this one,
but with elden ring colors and more rounded edges?"): a hollow arrowhead with a notched back and a looping glint. It
was read only, never copied, and kept out of the repository; a mock-up of our own was shown, in three forms: A still,
B with a lit edge, C with one glint played once (a loop being against invariant 14).

**Decision (2026-11-20).** (a), as recommended, in form A: our own pointer, a notched arrowhead with rounded corners,
`gold.hi` edge on a dark fill over a soft dark halo, still, with no glint; a text beam over fields. Built as D178.

**Owner's words.** "the white animation is not required" (of the mock-up's glint, form C), after asking for a pointer
"like this one, but with elden ring colors and more rounded edges".

## G32

**Quieting a mod's notices** · *Open since 2026-11-20*

**Question.** Each mod sets its notices' durations (D134), and the player has no say over a chatty mod. SkyrimNet's
dashboard has a switch for its in-game notices (research note SND), which led to the question whether GraceUI, as the
framework every mod's notices go through, should give the player one. **Options:** (a) **a switch per mod on the Mods
page** (Notices: shown, or quiet: a quiet mod's notices are kept in a list the menu shows, not drawn over the game),
GraceUI's own notices always shown (the player's exit, why the menu cannot open, danger); (b) one switch for every mod's
notices; (c) nothing for now.

**Recommendation.** (a): the player quiets the one mod that bothers them and keeps the rest; GraceUI's own notices stay,
since they say how to take the controls back (invariant 17). It is a framework rule of GraceUI's own, not a part of
SkyrimNet's, so G19 is not touched. *What (a) gives up:* a mod cannot rely on a notice being seen; it learns nothing of
the switch (a quiet notice is still accepted, invariant 2's spirit: absence is silent).

**What each answer changes.** (a): a setting per connected mod on the Mods page, its home there (invariant 12), a
notice list in the menu, the harness's notice checks with a quiet mod. (b): one setting on the Menu page. (c): nothing.

**Follows from.** The owner's question at the look review, 2026-11-20, comparing SkyrimNet's UI settings; D134, D141;
G19.

**Decision (2026-11-20).** (a), as recommended: a switch per mod on the Mods page (Installed mods), Shown or Quiet; a
quiet mod's notices are accepted as before and kept in a list on the same page, not drawn over the game; GraceUI's own
notices always show; the switch is kept between sessions. Built as D181 in GraceUI 0.2.3. Since the mods are known only
at run time, the switch is not a setting of GraceUI's settings store but a command of GraceUI's own space
(`mods.notices`), its state in the page's table; its one home is still the Mods page (invariant 12), and the Mods page
joins in 0.2 rather than 0.6 (D173 amended).

**Owner's words.** "go ahead with G32, per-mod switch".

## G33

**Five additions for mods' pages** · *Open since 2026-11-20*

**Question.** EldenNet's session, building its first pages, asked for six things; D184 built the first (a mod's
key-value lists, check lists, stat tiles and sentences drawn in game). The other five change the page data's format or
add a framework feature every mod may use, and are put together in [proposal 0006](proposals/0006-page-data-additions.md):
(1) `props.field`, one topic feeding several blocks; (2) `props.empty`, a block's own empty-state words, shown only
once its topic has published an empty body; (3) `props.newest_first` on a log tail; (4) a greyed state a space sets,
with options (a) a framework topic `space.state` that greys every page of the space with its reason, (b) the same per
page, (c) nothing; (5) a shortcut row's control in game, as the browser draws it.

**Recommendation.** All five, and (4) (a). None blocks EldenNet; its order of need is the order above.

**What each answer changes.** See proposal 0006 §6: 1 to 3 the component props in `protocol.toml`, the page lint and
both views; 4 a framework topic name and both views' grey; 5 grace-view alone. No change of the ABI.

**Follows from.** EldenNet's request of 2026-11-20 (its WHY§266 to §270); D166, D181, D184; G16, G18.

*Later (2026-11-20):* EldenNet's session answered that 1, 2, 3 and 5 fit its PAGES as written, and that (4) must be
per page from the start (its PAGES §4.4 greys some pages at L1 and others at the title screen, keeping the rest
usable). The recommendation for (4) is now (b), a map from page id to its words, with (a) as a shorthand beside it
(proposal 0006 §4, §8); EldenNet's session recommends the proposal with that change.

**Decision (2026-11-20).** All five, as recommended, with (4) as revised: a framework topic `space.state` mapping each
greyed page to its words, and a whole-space shorthand beside it. Built as D185 to D189 in GraceUI 0.2.7.

**Owner's words.** "Agreed to proposal 0006".

## G34

**Row actions, live pickers, row secrets and small forms** · *Decided 2026-11-21*

**Question.** EldenNet's session, building its AI pages on GraceUI 0.2.7, asked for four additions, put together in
[proposal 0007](proposals/0007-rows-pickers-forms.md): (1) an action on each row of a table (a Remove a provider,
destructive ones with their confirm text and second press); (2) a picker whose choices come from a topic at run time,
with options (a) `choices_from` on a string setting, the space checking membership, (b) a free text field with
suggestions, (c) presets only; (3) a secret for each row (a key for each provider the player adds), held to invariant
5 as a secret setting is; (4) a small form (a name and a base URL) whose fields go to the space's command.

**Recommendation.** All four, and (2) (a). None blocks EldenNet; each has a stand-in today. Telling a space which view
sent a command, so that keys, Add and Remove are refused from a paired device, stays with pairing (D183's open item).

**What each answer changes.** Proposal 0007 §6: the page data's types, the page lint and registration checks, both
views, fixtures and goldens; for (3) the server's and registry's handling of a value they never log; for (2) the
settings description and the store's check. No change of the ABI.

**Follows from.** EldenNet's request of 2026-11-21 (its WHY§274 to §276); D173, D181, D183, D185 to D189; G16, G18.

*Later (2026-11-21):* EldenNet's session answered that the proposal fits its AI pages as written, with (2) (a), and
that marking (1), (3) and (4)'s commands as candidates for the sensitive list is right (proposal 0007 §8).

**Decision (2026-11-21).** All four, as recommended, with (2) (a): `choices_from` on a string setting, its choices
supplied by the mod, which checks the value at its own entry point. The owner decided after a picture explainer of the
decisions waiting (the AI pages as built, and the four parts as mock-ups), and passed the answer through EldenNet's
session, which recorded its side as its O95. Built as D192 to D196 in GraceUI 0.2.8 (protocol 1.2), the row action and
the row secret declared in the page data rather than in the topic's body (D192).

**Owner's words.** "I am fine with everything you and grace done so far, so that's approved." (relayed by EldenNet's session, 2026-11-21); and to GraceUI's
session, of the explainer: "That looks fine."

## G35

**Pad chords for hotkeys** · *Put 2026-11-20 with proposal 0005; decided 2026-11-21*

**Question.** EldenNet's chat box opens on a pad by a chord (its NPC §5.6, O11), which only GraceUI may resolve (it alone
hooks XInput, invariant 16). [Proposal 0005](proposals/0005-pad-chords.md) says what a chord is (an anchor pressed
first, then a key: EldenNet's D-pad Up, then View; the key held back only when pressed 150 ms or more after the anchor,
the chord complete at 0.4 s, a broken chord replaying the key as one tap; figures EST, for S4 to confirm), when chords
stand aside, and how a mod asks for one (ABI 1.3, `pad_chord` as the key's bit in the low 16 bits and the anchor's in
the high 16, `has_cap("pad-chords")`). It asked (1) whether chords stand aside whenever a request for input would be
answered `not_now`; (2) the encoding; (3) EldenNet's session, whether §2 matches its NPC §5.6. The proposal was put on
2026-11-20 and not entered here as a question then; it is entered now, with its answer.

**Recommendation.** (1) Stand aside; (2) the anchor in the high half, the key in the low; build it for S4's second round.

**What it changes.** `grace-input`'s pad ledger, `register_hotkey`'s `pad_chord`, ABI 1.3 (an addition), the harness and
unit tests (proposal 0005 §5). Nothing in ABI 1.2's layout.

**Follows from.** EldenNet's NPC §5.6 and O11; D33, D77, D155, D176; invariants 8, 16 and 17.

*Later (2026-11-20):* EldenNet's session agreed with §2 as written, with one hotkey event enough, and with standing
aside (proposal 0005 §7).

**Decision (2026-11-21).** As proposed: D-pad Up held first, then View; 150 ms and 0.4 s as the estimates S4's second
round confirms or changes; chords stand aside whenever input would be refused; the encoding as in §4. Built when S4's
second round needs it, after EldenNet's R1, unless there is room before.

**Owner's words.** "I am fine with everything you and grace done so far, so that's approved." (relayed by EldenNet's session, 2026-11-21).

*Later (2026-11-22):* built before S4's second round, there being room, as D204 in GraceUI 0.2.14 (ABI 1.3). The owner,
asked whether to start it, answered: "alright, but keep in mind I don't have a pad, controller, VR set, or handheld to
test with, only a PC with keyboard and mouse." So every pad behaviour is checked without one (D205): by unit
timelines, and in the harness under Wine on both toolchains; the three estimates, the chord's freedom in the game and
the Deck's checks wait for testers' machines, never for the owner.

## G36

**Sections as cards: a chip, a live status and their own actions** · *Decided 2026-11-21*

**Question.** EldenNet's AI › Models wants one card per job slot (its PAGES §4.6): a title with the slot id as a chip, a
health dot and word, its pickers and budgets, and on Prepared lines a button of its own. Sections are already drawn
as cards; [proposal 0008](proposals/0008-section-cards.md) adds to a section (1) a static chip, (2) a live status from a
state topic (`{word, tone}`, tones as the lamp's), drawn in its head, nothing while unpublished, and (3) one or two
actions of its own, checked as header actions are. Alternatives: (b) a cards component built at run time from a
topic, whose settings would need keys made at run time against one home per setting; (c) nothing, keeping EldenNet's
settings block under the Jobs table.

**Recommendation.** (a) All three. Home's grid of cards (its Customise drawer) is left to a later proposal.

**What each answer changes.** Proposal 0008 §5: three optional fields on a section (protocol 1.3), the lint and
registration checks, the server's destructive commands, both views' section head, the demo space and goldens. No
change of the ABI.

**Follows from.** EldenNet's request of 2026-11-21 (its third pages slice); D173, D183, D184, D185, D188, D193; G16, G18.

*Later (2026-11-21):* EldenNet's session answered that the proposal fits its PAGES §4.6 as written and that its view is
(a), all three (proposal 0008 §7).

**Decision (2026-11-21).** (a) All three, as recommended: a section's chip, its live status and its own actions. To be
built in GraceUI 0.2.12 (protocol 1.3); built as D202. Home's grid of cards stays for a later proposal.

**Owner's words.** "both approved" (of G36 and the look review of the in-game menu, 2026-11-21).

## G37

**Where and how a hotkey is rebound** · *Put 2026-11-22 with proposal 0009; decided 2026-11-22*

**Question.** Since GraceUI 0.2.15 (D206) the Hotkeys page lists every mod's hotkeys, and nothing can change them: a
hotkey whose default another took stays unbound, and the menu's key reads F5 alone. The design says two things: §5.2,
that the player binds every mod's keys on GraceUI's Hotkeys page; D115 (and EldenNet's PAGES after it), that each
hotkey is changed on its own space's page. ABI 1 has no slot to change a hotkey. [Proposal 0009](proposals/0009-rebinding-hotkeys.md)
asks (1) where: (a) GraceUI's Hotkeys page for every mod, the binding kept in GraceUI's settings under the mod's and
the action's ids, or (b) each mod's own page, through a new `rebind_hotkey`; (2) a key already taken: (a) ask, then
move it, or (b) refuse; (3) pad chords chosen with two pickers in both views, a chord held on a pad captured later;
(4) whether a mod may read its hotkey's current key (ABI 1.4, `describe_hotkey` and a changed event); (5) for
EldenNet's session, whether retiring `hotkeys.talk`, `hotkeys.skip_page` and `hotkeys.debug` as EldenNet settings, in
favour of the defaults it registers, suits PAGES and O94.

**Recommendation.** (1) (a), one home for each binding (invariant 12), every conflict in one place, and mods with no
page served; (2) (a); (3) yes; (4) (a). The menu's key becomes live with it. When to build it is the owner's: GraceUI's
plan put it in 0.6, and with unbound hotkeys listed now it could come sooner.

**What each answer changes.** Proposal 0009 §6: `grace-input`'s bindings over the defaults, the capture modal in both
views, GraceUI's settings file, the menu's key, the page's row actions, the harness; with (4) (a), ABI 1.4. Under (1)
(a), EldenNet's three key settings and its Settings › Hotkeys; under (1) (b), a `rebind_hotkey` slot and each mod's own
capture.

**Follows from.** §5.2, §5.9, D33, D77, D115, D155, D204, D205, D206; invariants 8, 12 and 17; EldenNet's PAGES (its
Settings › Hotkeys) and O94.

**Decision (2026-11-22).** As recommended: (1) (a) every binding on GraceUI's Hotkeys page, kept in GraceUI's settings
under the mod's and the action's ids; (2) (a) a taken key asked, then moved; (3) pad chords chosen with two pickers in
both views, capture on a pad later; (4) (a) ABI 1.4's `describe_hotkey` and a changed event. The menu's key becomes live
with it. Built now, in 0.2, rather than in 0.6. Item 5, EldenNet's view of retiring its three key settings, is its
session's to answer in the proposal.

**Owner's words.** "approved as recommended, build it now" (2026-11-22).

*Later (2026-11-22):* built the same day as D208 to D210 in GraceUI 0.2.16 (protocol 1.4, ABI 1.4). Item 5 still waits
for EldenNet's session.

*Later (2026-11-22):* the owner gave the same answers in EldenNet's session's question dialog, relayed by it: "GraceUI's
Hotkeys page (Recommended)", "Ask, then move it (Recommended)", "Pad chords by two pickers (Recommended)", "Mods read
their key (Recommended)", "Now, in 0.2.x (Recommended)". EldenNet's session answered item 5: yes, its three key settings
become the defaults it registers, its Settings › Hotkeys a read-only list from GraceUI's topic with a link to GraceUI ›
Hotkeys, O94's T kept as the default (proposal 0009 §9). Its question on changing bindings off game is answered by
D211: in game only for now.


---

## G38

**Editing a document: what the profile editor needs from GraceUI** · *Put 2026-11-22 with proposal 0010; decided 2026-11-22*

**Question.** EldenNet's O43 puts a profile editor in v1, in both views, and since G20 (a) its pages are EldenNet's
(its S7.2.b), built from GraceUI's components. None of today's fits: a profile is a document of many fields, checked as
a whole, saved with a revision and drafted by a model; GraceUI has no text field in game (D57 is designed, not built),
neither view draws master–detail, a topic is declared one at a time, and a command is answered within 10 s.
[Proposal 0010](proposals/0010-editing-documents.md) adds, as protocol 1.5: topic families (`profiles/<key>`, also
EldenNet's `named/<id>`, `journal/<id>` and `conversation/<id>`); master–detail in both views; an editor block (eight
field kinds, the limits from the space's topic, check, save with `rev` and a new `conflict` code, reset, restore, and
a model's draft shown beside the fields, unsaved); slow commands, answered within 90 s; and GraceUI's own text field in
game. It asks (1) what to build and when: (a) all of it now, in 0.2.x, the text field first; (b) all but the text field
now, in the browser only, the text field with the menu's increment (0.6); (c) wait for EldenNet's pages pass; (2) what
becomes of unsaved edits when the menu closes in game: (a) kept in memory until the game closes, marked unsaved, or
(b) dropped with a notice; (3) prose fields as one paragraph each, with no new lines: (a) yes, or (b) new lines
allowed. Its §6 asks EldenNet's session whether the field kinds, the document's shape, the messages and the draft's
answer suit its editor (NPC §4.5, §4.7).

**Recommendation.** (1) (a): Named NPCs comes at EldenNet 0.3 and needs families and master–detail first, and the text
field also makes D194's row secrets, D195's forms and search work in game. (2) (a): invariant 7's close keeps changes,
danger never waits for an answer (invariant 9), and a save is a write the player asks for. (3) (a).

**What each answer changes.** Proposal 0010 §4: the protocol's minor, the page lint and registration, the registry,
`grace-view` and `grace-draw`, the web app, the demo space, the tests and a harness scenario; no change of the ABI.
Under (1) (b), the editor reads "Edited in the browser view" in game until 0.6. EldenNet: its Characters pages and the
editor in this shape (S7.2.b).

**Follows from.** EldenNet's O43, O46 and O86; G20 (a); D2 (amended by D213), D36, D57, D169, D193, D194, D195; §4.3,
§5.3, §5.6; invariants 1, 2, 3, 7 and 9; PV3; [profile-editor-needs.md](profile-editor-needs.md).

**Decision (2026-11-22).** As recommended: (1) (a) all of it now, in GraceUI 0.2.x, in the order the proposal gives
(the text field in game, topic families with master–detail, slow commands, the editor), each tried on the demo space
with invented content; (2) (a) unsaved edits kept in the menu's memory when it closes in game, never on disk, the page
reopening with them marked unsaved; (3) (a) prose fields as one paragraph each, with no new lines. Proposal 0010 §6
waits for EldenNet's session.

**Owner's words.** The owner's choices in the question dialog, 2026-11-22: "All now, in 0.2.x (Recommended)", "Keep in
memory (Recommended)", "One paragraph (Recommended)".

*Later (2026-11-22):* EldenNet's session answered proposal 0010 §6, yes to all four. It asked for five additions to
the editor block, recorded in the proposal's §7 and to be built with it:
- an opaque `key` on a list item;
- a `locked` list item, with its words;
- dotted field ids;
- `null` as "not set here";
- a message with no field shown at the top of the block.

It needs no cancel command for a draft: it cancels when the member is no longer watched. Its view on the three
questions was the owner's answers.

## G39

**One Escape for a mod after its box has closed** · *Put 2026-11-23 with proposal 0011; decided 2026-11-23*

**Question.** EldenNet's rule for ending a conversation (its NPC §5.7) has two steps while a reply still paces: the
first Esc closes the chat box and lets the reply finish; a second Esc ends it at once. The second press cannot reach
EldenNet: with the box closed nothing holds input, and Escape cannot be a hotkey (D155), so it opens the game's menu
over the reply (EldenNet's S11.4.d, its WHY§437). [Proposal 0011](proposals/0011-escape-after-close.md) lets a mod
arm Escape while nothing holds input: the next press goes to that mod and never to the game, then it is disarmed. It
asks (1) whether a mod may take one Escape press while nothing holds input: (a) yes, armed by the mod, one press, for
at most 60 s, Escape only, answered at once like `request_input` and disarmed by any holder, danger, loading, focus
loss, no render, a fault or time, the mod told why; (b) only within a fixed 5 s after the mod's own box closes, with no
arming; (c) no, and EldenNet's rule changes so that a reply stops only while its box is open; and (2) the pad: (a) the
keyboard only for now, or (b) the pad's B too while armed.

**Recommendation.** (1) (a): it is EldenNet's rule as designed, it costs the player at most one Esc while a reply
paces, and it is honest in D77's way. (2) (a): B dodges in the game, a dodge must never be eaten, and the owner tests
with keyboard and mouse only (D205).

**What each answer changes.** Under (1) (a) or (b): `grace-input` and ABI 1.6 (`arm_escape`, or under (b) an automatic
window; two event kinds; `has_cap("escape")`), unit tests and an "escape" harness scenario; EldenNet arms it while its
reply paces, ends the conversation on the press and shows the hint in its subtitles. Under (c): nothing in GraceUI;
EldenNet changes NPC §5.7. Under (2) (b): B masked while armed, checked on S4's second round by testers.

**Follows from.** D77, D155, D176, D204, D205, D212; invariants 8, 9 and 17; EldenNet's NPC §5.7, WHY§437 and S11.4.d.

**Decision (2026-11-23).** (1) (c): no armed Escape and no change in GraceUI. Esc and the pad's buttons act on a chat
box only while it is open: Enter sends the line, Esc cancels and closes the box. Once the box has closed, or the reply
is out, Esc and the pad are the game's again at once. No press stops a reply that is still pacing after its box has
closed: the reply finishes. (2) Falls with (1): nothing is taken from the pad while the box is closed. While the box is
open the pad may work as the keys do, as a holder's pad already does (D176). EldenNet's NPC §5.7 drops its second Esc,
and its S11.4.d needs nothing of GraceUI's.

**Owner's words.** In the question dialog, 2026-11-23, to (1): "what is this about? is it when the player enters
something in the chatbox to speak with an NPC? esc should only work whilst the chatbox is open, press Enter to enter
the written text. Press Esc to cancel and close down the chatbox. Once the reply is out or chatbox is closed, Esc
function is immediately back to normal (game's function)"; to (2): "no need to stop reply, user either closes chatbox
with Esc or enter reply with Enter. You can implement gamepad buttons to work as such too, but we only care about while
the chatbox is open, if the chatbox is not open those keys are back to normal function."

