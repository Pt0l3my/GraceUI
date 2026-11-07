# Study: GraceUI's information architecture (sections, pages and where every setting lives)

Written 2026-11-07 for the GraceUI design and cited as **IA**. [GraceUI DESIGN](../DESIGN.md) §4 adopts it; §11's candidate
Open questions are resolved there as the table in §11 shows. It maps every EldenNet
setting and view to a sidebar of sections and pages with Elden Ring names. For each page it gives the purpose,
contents and controls, the EldenNet data the page reads or writes, its spoiler and privacy rules, and which parts
wait for a later version.

**Superseded in part (later on 2026-11-07).** The sidebar of flavour-named sections (§3) and the page set (§5) are
replaced by DESIGN §4's framework categories (D38, D39), on the evidence of [SND](skyrimnet-dashboard.md); DESIGN §4.12
maps each old page to its new home. This note's mapping of EldenNet's settings and views (§4), its spoiler rules (§6),
privacy rules (§7) and settings-row badges (§8) still stand, as DESIGN §4.5 and §4.7–§4.9 carry them.

## 0. Sources, method and limits

- **SkyrimNet, for ideas only (when this note was written; its author has since approved building on SkyrimNet).** Its dashboard's page list as EldenNet research records it (SN01 §9, SN00 row 38).
  Nothing of its code, text or compiled files was opened, and its name is not used to promote anything.
- **EldenNet's design**, on `design/v1-proposal`, read through `wp` and grep with line numbers. EldenNet's
  speaking-up design was being written at the same time; DESIGN §7.6, §8.9 and §9.4, NPC §1.4, §9.1 and §9.8–§9.11,
  and ARCH §11.5 at settings schema 2 already carried it. Where those documents were not yet written, this note cites
  that work's specification (a working document of 2026-11-07, cited as "spec Pn" or "spec §1.n") and its event list.
  Their content is being folded into the sections above, so check those first. Other
  codes: DESIGN, ARCH (`architecture.md`), NPC (`npcs-and-dialogue.md`) and MEM (`memory-and-knowledge.md`).
- **The look.** [severactions-look.md](severactions-look.md), cited as LOOK (the Grace theme, the rail, the header strip and the components). This note uses
  its component names and decides only what goes where.
- **Untrusted copies.** No Magelight, SeverActions or SkyrimNet file was opened for this note. The only one looked
  at is the owner's screenshot of SeverActions' menu, and only for layout: a rail with eyebrow-labelled groups,
  sub-items under the active item, settings at the foot, a header strip, a table and a detail pane.
- **Names.** Section and page names are ordinary English words in an Elden Ring spirit. No FromSoftware label,
  menu entry, banner or item text is reused. Names of characters, places and bosses appear only at runtime, from
  the player's own install (NPC §2.7).
- **Limits.** The settings design is still moving. Every key below was checked against ARCH §11.5 (schema 2) and
  DESIGN §7.6 as they stand today; §12 lists where those two disagree.

---

## 1. What SkyrimNet's dashboard suggests, and what GraceUI takes

| SkyrimNet idea (SN01 §9) | GraceUI | Why |
|---|---|---|
| A home page with status, nearby NPCs, live events and recent model requests | **Take**: Grace (Home) | It is the page players and testers open first. Live views are filtered by perception (SP7) |
| One app, shown in the browser and in game | **Take**: the same pages in both hosts | Settled in GraceUI DESIGN (D5). In game, safe moments only |
| Searchable settings with hotkey capture | **Take**: global search, All settings, Bindings | One home per setting, with search over labels, plain names and keys |
| Content editors (characters, memories, world knowledge, triggers, prompts, actions, diary) | **Leave in v1**, except read-only memories with Forget | Persona private fields, roads and forbid lists are spoilers. EldenNet's content is files with overrides (ARCH §11.6). Editors become a later Workshop seam (§5.18) |
| Agents over MCP, a content hub | **Leave** | Not in EldenNet's scope |
| A game-data explorer | **Leave** | Settled in GraceUI DESIGN (D2): a spoiler surface with no v1 use |
| Logs, traces, performance, event monitor | **Take**: Vigil (diagnostics) and Augury (event monitor) | Augury adds "why did nobody speak", the main tuning tool for pacing |
| A model picker with presets | **Take**: Oracles | EldenNet already has presets, slots, health and a self-test (ARCH §11.1–§11.7) |

---

## 2. Principles

1. **One home per setting.** Every setting has exactly one home page, where search leads. Other pages may show it
   as a *shortcut* row: the same control, marked with a small "lives in Pacing" link.
2. **Changes apply as they are made.** There is no Save button. Changes are written to `settings.toml` (debounced
   300 ms) and hot-reload (ARCH §11.6). Danger in game closes the menu and keeps every change. A confirmation still
   open when danger closes is cancelled, never carried out.
3. **Each row says whether it works now.** Availability is shown in words: live, waiting on a game check on this
   build, takes effect in v1.5, later version, or no lines in this version. A setting that does nothing yet is
   never presented as if it works (§8).
4. **Player-safe projections only.** The UI shows what EldenNet's core projects for the player, never raw content
   or prompts (SP1). Spoiler layers apply to the UI as they do to prompts.
5. **Live views show what the Tarnished perceived** (SP7), so neither host becomes a radar or a spoiler feed.
6. **Same pages in both hosts.** The browser adds no pages, except the developer Workshop, which is behind
   `debug.panel`. In game the menu opens only when `safe_context` holds or at the title screen (NPC §10.6).
7. **Plain words.** A page carries a flavour name and a plain subtitle ("Beckoning · Speaking up and autonomy").
   Search matches both, and a display option shows plain names only (§5.15).
8. **Nothing personal leaves the page** (PV1–PV8). Keys are write-only, and chat text never reaches a log or a
   bug report.

---

## 3. The map

### 3.1 The frame (per LOOK §3.7)

- **Rail:** the wordmark and the build line ("v0.6 · Threshold"), the search field, then the sections below. The
  active page's tabs show as sub-items under it, as in the owner's screenshot. "All settings" sits at the foot,
  then the key hints ("Esc closes · ⇆ sections · / search"; pad glyphs when a pad is in use).
- **Header strip:**
  - left: the character's name (PV2), the current region by its runtime name (SP3) and the journey ("Journey 2"
    from NG+ on);
  - middle: one state word with a dot: Full, Reduced (which feature), Observing (no model or AI paused), Paused
    (multiplayer, loading), or Inactive (the build is not supported). These are DESIGN §4.4's levels in words;
  - right: help, the key-hint toggle and close.
- **Page**, with an optional **detail pane** on the right for master–detail pages (The Named, Kinds, Remembrance,
  Augury).

### 3.2 The sidebar

| Section (eyebrow) | Page: flavour · plain | One line | First shown (EldenNet increment) |
|---|---|---|---|
| — | **Grace** · Home | Status, nearby NPCs, live activity, recent model calls, pause | v0.2 (browser) |
| FOLK | **The Named** · Named NPCs | Each named NPC's page: who they are to you, memories, conversations, allowances | v0.3 |
| FOLK | **Kinds** · NPC types and allowances | Every NPC type with its tags, and what each may do | v0.4 (view), v0.6 (in game) |
| VOICES | **Pacing** · Pacing and reach | Time between lines, lines per window, ranges | v0.4 |
| VOICES | **Reactions** · Reactions and cooldowns | Each event type: whether NPCs react, and its cooldown | v0.4 |
| VOICES | **Beckoning** · Speaking up and autonomy | Who speaks unasked, greetings, quest business, news, autonomy, NPC-to-NPC talk, self-awareness | v0.3 (named), v0.4 (all) |
| VOICES | **War cries** · Combat dialogue | Combat lines for every combatant, boss fights, scripted-line holds, line pools | v0.4 |
| VOICES | **Afflictions** · Status awareness | Statuses NPCs notice in themselves and others, and what they say | v0.4, each status as S8 passes |
| VOICES | **Parley** · Conversation | Chat, call-out, content safety, typing | v0.2 (chat), v0.6 (call-out) |
| WORLD | **Tidings** · World and stories | The state of the land as you have made it, and who has heard (spoiler-safe) | v0.5 |
| WORLD | **Remembrance** · Memories and journals | Journals, keepsakes, facts and conversations; forgetting | v0.3 |
| WORLD | **Augury** · Event monitor | What EldenNet noticed, and why each line did or did not play | v0.4 |
| CRAFT | **Oracles** · Models and providers | Presets, providers, keys, model per slot, health, self-test, background work | v0.2 |
| CRAFT | **Voice** · Speech (later) | Text to speech and speech input: the v1.5 seam | Shown greyed with a "v1.5" badge |
| CRAFT | **Lettering** · Display and subtitles | Scale, HDR, subtitles, labels or feed, menu options, preview | v0.6 |
| CRAFT | **Bindings** · Hotkeys | Talk, skip, menu, debug; capture and conflicts | v0.6 |
| UPKEEP | **Vigil** · Diagnostics | Health, read groups, checks, performance, latency, storage, content, logs, debug options | v0.2 |
| UPKEEP | **Missive** · Bug report | A previewed bundle with no secrets and no chat text | v0.2 (preview), v0.6 (copy) |
| UPKEEP | **Workshop** · Developer (browser, `debug.panel` only) | Prompt inspector, unfiltered events, world view, unknown characters | v0.2 (browser) |
| (foot) | **All settings** | Every setting in one searchable list, with "changed" and "availability" filters | v0.6 |

Outside the sidebar: **First light** (the setup wizard, §5.20) and the confirmation modals.

**Staying ImGui in v1** (DESIGN §12): the lower third, the chat box, the call-out box, labels and the feed, toasts,
the banner and pause badge, the rollback modal (it must work at load), and the F11 debug view. GraceUI is the menu
and the dashboard.

### 3.3 Global search

- **Opened with** `/` or Ctrl+F (a pad button, to be fixed by the input design), from any page. **Indexes:**
  - page names (flavour and plain);
  - every setting's label, plain synonyms, help text and key (`pacing.gap_s`);
  - reaction labels and ids;
  - type labels and tags (seen only, SP4);
  - named NPCs (on-screen names, SP3);
  - Vigil's check names.
- **A result** shows its breadcrumb ("Voices › Pacing › Time between lines") and opens the setting's home page with
  the row focused and tinted; other rows dim (LOOK's settings search).
- **Never indexed:** journal text, conversation text, Tidings text and Augury rows. Search is for settings and
  places in the menu, so it cannot surface a spoiler or a private line by accident.

### 3.4 States of the whole menu

| State | Pages available | What the menu shows |
|---|---|---|
| Title screen (no playthrough; ARCH §1.5 closes the playthrough file) | Grace (status only), Oracles, Parley, Lettering, Bindings, Vigil, Missive, every settings page, First light | The Named, Remembrance and Tidings say "Load a character to see this" |
| L1 banner (DESIGN §4.4) | Vigil, Missive, Lettering, Bindings | Everything else is greyed: "EldenNet is inactive: <reason>" |
| L2 observing (no healthy model, or AI paused) | All pages | Grace and Oracles lead with the cause |
| Dormant (multiplayer, loading) | All pages, live views paused | "Paused (multiplayer)". Settings stay editable |
| Lost contact with EldenNet (browser) | Last snapshot, read-only | LOOK's stale state: dimmed, "Reconnecting", Retry |

---

## 4. Every setting and view, and where it lives

Effect timing comes from ARCH §11.6: **Now** (hotkeys, UI), **Next moment** (`features`, `autonomy`, `range`,
`pacing`, `reactions` and `allow` take effect at the next core step), **Next request** (providers, keys), and
**Next launch** (`enabled = false` is full Off at the next launch, AI paused now; ARCH §1.5).

### 4.1 `settings.toml` (ARCH §11.5, schema 2)

| Key | Player label | Default (bounds) | Home | Shortcuts on | Effect |
|---|---|---|---|---|---|
| `enabled` | EldenNet on | true | Grace | All settings | Pause now, Off next launch |
| `features.chat` | Conversation with named NPCs | true | Parley | — | Next moment |
| `features.callout` | Call out to soldiers at range | true (v0.6) | Parley | — | Next moment |
| `features.speak_up` | Named NPCs speak unasked | true | Beckoning | War cries | Next moment |
| `features.barks` | Other NPCs speak unasked | true | Beckoning | War cries | Next moment |
| `features.npc_to_npc` | NPCs answer and greet each other | true | Beckoning | — | Next moment |
| `features.self_awareness` | NPCs know what they said and how they are | true | Beckoning | — | Next moment |
| `features.status_awareness` | NPCs notice statuses | true | Afflictions | — | Next moment |
| `features.memory` | Named NPCs remember | true | Remembrance | — | Next moment |
| `features.labels` | Floating labels (else the feed) | true | Lettering | War cries | Now |
| `autonomy.enabled` | NPCs speak up on their own | true (false on free presets) | Beckoning | — | Next moment |
| `autonomy.period_s` | How often they may | 120 s (30–1,800) | Beckoning | — | Next moment |
| `range.talk_m` | Speaking-to-you range | 8 m (3–16) | Pacing › Reach | Beckoning | Next moment |
| `range.hear_m` | Overheard-lines range | 20 m (8–30) | Pacing › Reach | — | Next moment |
| `range.fight_m` | Combat-lines range | 40 m (10–40) | Pacing › Reach | War cries | Next moment |
| `pacing.gap_s` | Time between lines | 2.5 s (0.5–10) | Pacing | — | Next moment |
| `pacing.group_gap_s` | Time between lines from one fighting group | 8 s (0–30) | Pacing | War cries | Next moment |
| `pacing.window_s` | Window length | 60 s (30–300) | Pacing | — | Next moment |
| `pacing.fight_max` | Lines per window in a fight | 6 (0–20) | Pacing | War cries | Next moment |
| `pacing.idle_max` | Idle remarks per window (off 0, sparse 2, normal 4) | 2 (0–8) | Pacing | — | Next moment |
| `pacing.talk_max` | Talk per window | 4 (0–12) | Pacing | Beckoning | Next moment |
| `reactions.<id>.on`, `.cooldown_s` (20 ids, NPC §9.1) | Per reaction (§5.5) | per id (0–21,600 s of play) | Reactions | War cries (combat family), Afflictions (status family), Beckoning (calm family) | Next moment |
| `allow."<kind\|type\|slot>".<key>` (DESIGN §7.6) | Per allowance (§5.3) | shipped per kind | Kinds | The Named (the slot's row), War cries (`combat`, `boss_fight`), Afflictions | Next moment |
| `ui.scale` | Text and menu scale | 1.0 (0.75–1.5) | Lettering | — | Now; atlases rebuilt when the menu closes (NPC §10.7) |
| `ui.hdr`, `ui.paper_white_nits` | HDR; paper white | auto; 203 (80–400) | Lettering | — | Now |
| `ui.follow_game_subtitles` | Follow the game's subtitle setting | true | Lettering | Parley | Now |
| `ui.latency_toasts` | Latency toasts | false | Vigil › Developer | — | Now |
| `pools.max_jobs_per_session` | Prepared-line batches per session (0 = off) | 60 | Oracles › Background work | War cries | Next request |
| `safety.level` | Content safety: faithful or gentle (O17) | faithful | Parley | — | Next request |
| `hotkeys.talk`, `.skip_page`, `.settings`, `.debug` | Talk, skip page, menu, debug | T (from S4), Tab, F10, F11 | Bindings | Parley | Now |
| `augur.allow_custom_rules` | Custom event rules | false (seam) | Workshop, locked "later" | — | — |
| `debug.panel`, `.record_sessions`, `.record_redact_text`, `.log_level` | Developer options | false, false, false, info | Vigil › Developer | — | Now |

### 4.2 Other configuration files

| Source | Contents | Home | Access |
|---|---|---|---|
| `providers.toml` (ARCH §11.1) | `preset`; `[[provider]]` with `id`, `flavor`, `kind`, `base_url`, `key_ref`, `headers`, `extra_body`, `max_concurrency`, `json_mode`; `[slots.<slot>]` with `model` and `fallback` | Oracles | Read and write |
| `secrets.toml` (ARCH §11.5) | API keys | Oracles | **Write only.** The UI gets a key's status (set, missing, rejected), never its value (PV2) |
| `models.toml` (shipped; ARCH §11.4) | Presets, slot budgets, the free-tier warning | Oracles | Read |
| `overrides/` and `disable.toml` (ARCH §11.6) | Whole-file overrides; disabled personas, cards, KB, triggers and rules | Vigil › Content | Read (counts and ids; editing stays in files) |
| `identity/types.toml` (NPC §1.4) | Types, labels, tags, kind rules, shipped allowances | Kinds | Read |
| `personas/*.toml` (DESIGN §7.3) | `display_name_fallback`, `public.summary`, `awareness.news` temperament; later `voice_key` | The Named | Read, **those fields only** (SP1) |
| `world/stages.toml` (MEM §12.2) | A world stage's `line` (impersonal, our words) | Tidings | Read, **only for stages that hold** (SP5, SP6) |

### 4.3 Actions (commands)

| Action | Home | Guard |
|---|---|---|
| Pause AI for this session (proposed, §9) | Grace | Confirm-free; shown as a state |
| Run self-test (ARCH §11.7, steps 4a–4e) | Oracles (also Vigil) | Only while the gate allows network (DESIGN §3.4) |
| Find local servers (ARCH §11.4 probes) | Oracles | As above |
| Pre-bake line pools (DESIGN §11.5 step 5) | Oracles › Background work | At rest or at the title only (ARCH §11.3 pause rules) |
| Forget this NPC, Forget this journey (MEM §9.5) | Remembrance (also The Named) | Confirmation; `pre-forget` backup; no undo but the backup |
| Reset a row, a page, or all settings | Every row; page menu; All settings | Confirmation for a page or all |
| Preview a subtitle and a label | Lettering | In game draws through the ImGui surfaces for 3 s |
| Capture a key | Bindings | Conflict check before saving |
| Build, preview, copy or save a bug report | Missive | The deterministic scan passes first (DESIGN §14) |

### 4.4 Views (projections the core builds for the UI)

| View | Built from | Pages |
|---|---|---|
| Ladder level, read groups, capability matrix, gate | DESIGN §4.4, §3.4; ARCH §3.2 | Header, Grace, Vigil |
| Provider and model health | ARCH §11.2 (per provider and model: failures, cooling, disabled) | Grace, Oracles |
| Model call log | The LLM client's events and `diag.sqlite` traces (DESIGN §10.5) | Grace (last 20), Vigil › Latency, Workshop (with text) |
| Nearby characters | Snapshot, identity, allowances, filtered by SP7 | Grace |
| Shown lines and notices | `NpcSaid` and `PlayerSaid` that were displayed; toasts | Grace › Activity, The Named › Conversations |
| Journals, facts, conversations, memory jobs | MEM §2 and §3 tables | Remembrance, The Named |
| World stages that hold; which met personas know them | `knowledge::world`, `known(P)` restricted to world stages (DESIGN §10.6) | Tidings, The Named › Overview |
| Event stream | A tap on Augur's bus (ARCH §8.6), bounded, filtered by SP7 | Augury › Events |
| Scheduler decisions | Admission results with the refusing rule (spec P2; proposed ring, §9) | Augury › Decisions, Reactions (per-row stats) |
| Verification states | Spike results per feature, kind, status and persona (S3, S6, S7, S8; spec §1.11) | Availability badges everywhere; Vigil › Checks |
| Reload and validation results | ARCH §11.6 (file and line) | Vigil › Content |
| Performance | Task p99, overlay and menu frame cost, memory (DESIGN §1.3 C3) | Vigil › Performance |
| Logs | `tracing` files, scrubbed (PV5) | Vigil › Logs |
| Unknown characters, last prompt and reply, raw world view | F11's lists (NPC §2.5; DESIGN §14) | Workshop only |

---

## 5. Pages

Every page entry gives: **Purpose · Contents · Controls · Reads/writes · Spoilers and privacy · Seams · Ships**.
SP and PV refer to §6 and §7.

### 5.1 Grace · Home

- **Purpose.** See at a glance whether EldenNet is working, who is around, what was just said, and what the model
  is doing.
- **Contents.**
  - **Status tiles:**
    - the state word and its reason ("Reduced: floating labels off: the camera group failed its self-test");
    - one dot per model slot (`dialogue`, `bark_live`, `bark_pool`, `memory`): healthy, cooling, disabled or not
      set;
    - memory (on, or off for this session);
    - line pools (ready, generating, paused);
    - in game only, "You can talk now" or the reason not (`safe_context`, NPC §5.4).
  - **Nearby** (SP7), one row each:
    - the on-screen name (SP3), or the type label for generics;
    - the type label;
    - a state word: calm, fighting you, fighting at your side, fighting others, or asleep (after S8);
    - the wound band in words for combatants (DESIGN §5.5: unhurt, bloodied, badly hurt, barely standing);
    - status words (after S8);
    - "speaking" while a line of theirs is visible;
    - for a met persona, their disposition word;
    - a link to the slot's or type's allowances.

    Updates at most twice a second.
  - **Live activity:**
    - lines that were displayed (speaker, text, motive label, time of play);
    - conversations opened and closed (and who opened them);
    - notices (the toasts' texts, DESIGN §2.7);
    - reloads.

    Filters: Lines, Notices.
  - **Recent model calls** (last 20):
    - time, slot and purpose (reply, greeting, quest business, autonomy, call-out, pool batch, memory job,
      self-test);
    - the speaker's on-screen name, or "—";
    - provider and model;
    - first token, total time, and tokens in (cached / uncached) and out;
    - the outcome class (ARCH §11.2);
    - the displayed text, only if it was shown.

    A summary line gives own-initiative calls this hour against the bound (`talk_max` × 3,600 / `window_s`, 240 at
    the defaults; spec P12) and the session's tokens.
- **Controls.** "Pause AI for this session" (proposed, §9) and the master switch `enabled` (with the explanation
  "pauses now; fully off from the next launch"). "Run self-test" (shortcut to Oracles). Activity: pause the stream,
  clear the view.
- **Reads:** the status, health, nearby, shown-lines, notices and call-log views. **Writes:** `enabled`, and the
  pause command.
- **Spoilers and privacy.**
  - Nearby and activity follow SP7: only perceived characters and displayed lines. Boss-arena characters appear
    only once the bar shows, and invaders once the game announces them.
  - No HP numbers and no distances for hostiles.
  - Unshown model text is never shown here: lint-blocked replies, prepared lines that never played, and silent
    autonomy answers (SP1). Blocked replies are exactly where spoilers leak.
  - Chat text appears only in the activity list and is never logged (PV4).
- **Seams.** The `Directed` class (DESIGN §13) would add "director" activity later.
- **Ships.** Status, model calls and activity with v0.2; nearby with v0.3; statuses as S8 passes.

### 5.2 The Named · Named NPCs

- **Purpose.** One page per named NPC: who they are to you, what they remember, what was said, and what they may
  do.
- **Contents.**
  - **List** (left): the six personas (the published cast, DESIGN §7.6), by their on-screen name (SP3; before
    the first meeting, `display_name_fallback`, which is the pre-reveal name, NPC §2.7). After them come other
    named NPCs once seen (`named:<param>`, runtime name), marked "no lines in this version". Columns:
    - met (count) or "not yet met";
    - last met, as region and time of play ago;
    - disposition word;
    - here now;
    - overrides (how many allowances differ from the type).

    The Lantern-Keeper test persona shows only in Workshop.
  - **Persona page tabs:**
    1. **Overview:**
       - the on-screen name and the public summary (DESIGN §7.3, the Stranger Test: impersonal, passes SP2);
       - the state now, if nearby (as on Grace);
       - facts as data, not prompt sentences: met and seen counts; first and last met (place, time of play); times
         you struck them, in words; fought beside you; whether they know your name (O18); disposition in words
         (MEM §2.2, §2.4);
       - their temperament for news, in words ("raises news that touches their own affairs" and so on, from
         `awareness.news`; DESIGN §10.6);
       - "Has heard of": the world stages from Tidings that this persona knows, with a certainty word (SP5).
    2. **Memories:** the journal (§5.11), embedded.
    3. **Conversations:** each meeting by place and time of play, with its lines (player lines in italics, as in
       the chat box), the end reason, and lines it said unasked (`NpcSaid` with no conversation; spec §1.5).
    4. **Allowances:** the slot's row from Kinds, embedded, with "inherited from type" shown faintly (§5.3).
    5. **Reactions:** the slot's per-reaction overrides (`allow."persona:x".reactions`).
  - **Unmet personas** open on Allowances only. The other tabs are absent, not veiled (SP5).
- **Controls.** Allowance and reaction tri-states (inherit, on, off). "Forget this NPC" (shortcut to Remembrance).
- **Reads:** persona display fields, facts, journal, conversations, `known(P)` restricted to world stages, and the
  allowance resolution. **Writes:** `allow."persona:<id>"` and `allow."named:<param>"`.
- **Spoilers and privacy.**
  - The UI never shows a persona's stage, `road`, `gist`, private fields, ties, `forbid`, `beckon`, KB entries or
    tracks (SP1).
  - Names follow the reveal rule exactly (SP3), so a veiled persona stays veiled in the menu.
  - Conversation text is the player's own story: shown locally, never logged and never in a report (PV4). The
    page title carries the name, never chat text.
- **Seams.** `npc_thought` (thoughts) and native line text (`NativeLineHeard.talk_id`, S10) would add tabs later;
  voices (v1.5) add a voice row to Overview.
- **Ships.** v0.3 (Kalé); the other five with v0.5; other named slots with v0.4.

### 5.3 Kinds · NPC types and allowances

- **Purpose.** The owner's page (O38): every type of NPC, labelled and tagged, and what each may do. Named NPCs
  have their own slots.
- **Contents.** A master–detail page (it suits the menu's width better than a 15-column grid; a grid view is
  offered in the browser on wide screens).
  - **Rows**, in DESIGN §7.6's order:
    - named slots first;
    - then each kind (named, merchant, humanoid, creature, boss, invader, summon, mount) with its types beneath
      it;
    - then one locked "unknown" row ("Characters EldenNet hasn't identified stay silent").

    Each row shows our label (≤ 32 characters), tag chips, how many are nearby now, and summary icons for Speech,
    Unasked and Battle.
  - **Tag chips:** kind, race or family, faction or allegiance, live side (a state, shown only on Grace),
    voice. A tag still resting on an unverified lead is drawn hollow, with the tooltip "not yet confirmed on
    your build" (NPC §1.4 lists each tag's signal and spike).
  - **Detail pane:** the allowances in groups, each a tri-state (inherit, on, off) that shows the inherited value
    and its source ("on, from kind: humanoid"). Precedence: slot > type > live kind > base kind > shipped default
    (DESIGN §7.6).

    | Group | Keys (DESIGN §7.6) | Notes |
    |---|---|---|
    | Speech | `dialogue`, `tts` | `dialogue` off greys every other row: "off also turns off voice and every reason to speak". `tts`: "takes effect in v1.5" |
    | Unasked | `greet_player`, `npc_to_npc`, `autonomy`, `news`, `stage_talk` | `news` and `stage_talk` show for persona slots only |
    | Reactions | `reactions` (opens the 20-row override list), `status_reactions` | Overrides as tri-states over the reaction ids |
    | Battle | `combat`, `boss_fight` | `boss_fight` shows "held for this kind until its scripted-line check passes" where S3 has not passed |
    | Awareness | `self_awareness`, `status_awareness` | `status_awareness`: "waits on the status check" until S8 |
    | Acts | `actions`, `gestures` | Locked, greyed: "later version" (O41) |

  - **Inert rows** (no voice source in v1) carry the badge "no lines in this version". Their toggles still work,
    ahead of later versions (O43).
- **Controls.** The tri-states; reset a row; filters (kind, changed, seen, nearby).
- **Reads:** `types.toml`, the allowance resolution, live counts, verification states, `types_seen` (proposed,
  §9). **Writes:** `allow."<kind|type|slot>".<key>` and `allow."…".reactions.<id>`.
- **Spoilers.**
  - Labels are ours, and boss rows never name a boss (DESIGN §7.6).
  - Other named slots appear only once seen.
  - Recommended (Open 3): cards and creature families too appear only once the Tarnished has seen one, because a
    list of every creature family reveals late-game creatures. A "show types I haven't met (may spoil)" switch
    is offered. Base kinds are always listed, so a kind-level allowance still covers unseen types.
  - Allowances only narrow, and cannot override the non-disableable rules (ARCH §11.6, DESIGN §7.6). The page
    states this once, as "Safety and spoiler rules always apply", never as dead toggles.
- **Seams.** `tts` (v1.5); `actions`, `gestures` (later); boss, invader and creature voices (O43).
- **Ships.** Read-only view with v0.4, when `types.toml` and the `allow` keys exist; editable in the browser from
  v0.4 (Open 11); in game with v0.6 (spec P17).

### 5.4 Pacing · Pacing and reach

- **Purpose.** The only limits on how often NPCs speak (O31): the gap, the window and per-type cooldowns. The
  cooldowns live on Reactions. Also the ranges (O32).
- **Contents.**
  - **Gap:** `gap_s`, `group_gap_s`.
  - **Window:** `window_s`, then `fight_max`, `idle_max` and `talk_max`. `idle_max` has a segmented control (off
    0, sparse 2, normal 4, custom), so O14's density survives as a preset.
  - **Reach:** `talk_m`, `hear_m`, `fight_m`, each with one sentence on what it governs (spec P10).
  - **"What this means"**, computed live:
    - "in a fight: at most 6 lines a minute, never closer than 2.5 s, one group at most every 8 s";
    - "NPCs prompted by the model: at most 240 calls an hour".
  - **Notes, not settings:** replies to you are never limited; at most 2 labels show at once and the feed holds 3
    lines (presentation, spec §1.1).
- **Controls.** Sliders with tabular values and numeric entry within bounds; "Reset page". Optionally presets
  (Quiet, Default, Lively) that write the six keys (Open 16; values to be tuned with X2).
- **Reads:** `pacing.*`, `range.*`, the schema's bounds. **Writes:** the same.
- **Spoilers and privacy.** None.
- **Seams.** A replay preview (run `busy_fight.scn` at the current values and draw the timeline) could come later
  through `en-tools replay`. In v1 the page uses computed text only.
- **Ships.** v0.4 (the keys exist); in game with v0.6.

### 5.5 Reactions · Reactions and cooldowns

- **Purpose.** O33: for each event type, whether NPCs react and how long before the same reaction can recur.
- **Contents.** The 20 rows of the reaction catalogue (NPC §9.1, spec P7), grouped by family. Autonomy appears as
  a shortcut row, because it lives in Beckoning.

  | Family | Reaction ids |
  |---|---|
  | In battle | `engaged`, `struck_hard`, `low_hp`, `ally_fell`, `foe_fell`, `player_fell`, `foe_fled`, `foe_healed` |
  | Afflictions | `status_self`, `status_plea`, `status_warn`, `status_foe` |
  | Calm remarks | `idle`, `greet_npc`, `answer`, `saw_kill`, `saw_victory` |
  | Speaking to you | `greet_player`, `stage_talk`, `news` |

  Each row shows:
  - the player label ("Takes a heavy blow");
  - an on/off switch;
  - the cooldown as a duration field with units (s, min, h of play; 0–6 h; `player_fell` "none");
  - the scope in words (per NPC, per fighting group, per pair, per NPC and status, per named NPC and story step,
    per named NPC and piece of news);
  - where its lines come from (written lines, prepared lines, or live from the model, with a cost mark);
  - an availability badge (for example "waits on the status check" for the status family until S8);
  - this session's stats from Augury: last played, and refusals by rule ("cooldown 3, window 1");
  - an "Overrides: 2 types" link to Kinds, filtered.
- **Controls.** Switch, duration, reset row, reset family.
- **Reads:** `[reactions]`, catalogue metadata, decision stats. **Writes:** `reactions.<id>.on` and
  `.cooldown_s`.
- **Spoilers and privacy.** None. The labels are generic, and "story step" says nothing about any story.
- **Seams.** The reactions the speaking-up design recommends for later (an arrival remark, a cheer on a critical hit,
  warnings as a status builds) appear when they ship, never as placeholders.
- **Ships.** v0.4 (combat and calm families); status rows as S8 passes; in game with v0.6.

### 5.6 Beckoning · Speaking up and autonomy

- **Purpose.** Everything about NPCs starting to speak: greetings, quest business, news, autonomy, NPC-to-NPC
  talk and self-awareness (O32, O34, O35, O37, O39).
- **Contents.**
  - **Who speaks unasked:** `features.speak_up` (named NPCs) and `features.barks` (others).
  - **Greetings:** shortcut rows for `greet_player` and `greet_npc`; a note that in v1 only named NPCs greet you,
    because the six cards are hostile (spec §1.8).
  - **Quest business:** the `stage_talk` row, and per persona one readiness word: ready; "waits for a game check
    on your build" (S3 or S6 not passed: fail closed, spec §1.2); or "not in this version". It never names or
    hints at a stage.
  - **News:** the `news` row, and one sentence on when a named NPC raises news (O30's recommended default).
  - **Autonomy:** `autonomy.enabled` and `period_s`; the bound in words ("prompted only when pacing would let a
    line through: at most 4 calls a minute at your settings"); a free-tier note.
  - **NPC to NPC:** `features.npc_to_npc`, and the `answer` and `greet_npc` rows; one sentence on how exchanges
    end by themselves (spec §1.4).
  - **Self-awareness:** `features.self_awareness`, with one sentence on what it adds (their own recent lines,
    and how they are now).
  - **Reach:** `range.talk_m` (shortcut).
- **Controls.** As on the home pages of each key.
- **Reads:** `features.*`, `autonomy.*`, the shortcut rows, readiness from the verification states. **Writes:**
  `features.speak_up`, `features.barks`, `features.npc_to_npc`, `features.self_awareness` and `autonomy.*`.
- **Spoilers and privacy.** Readiness is per persona and in words only. A persona's `beckon` text never shows
  (SP1), and no "has something for you" indicator shows anywhere in the menu: it would give away a stage change
  the game has not yet shown.
- **Seams.** The `Directed` class (DESIGN §13).
- **Ships.** v0.3 (named NPCs: Kalé, when S3 passes), v0.4 (the rest); in game with v0.6.

### 5.7 War cries · Combat dialogue

- **Purpose.** O42: combat lines for any NPC engaged in combat, with the Tarnished or with each other, boss fights
  included, on by default, and never while that NPC speaks scripted dialogue.
- **Contents.**
  - A **kinds × {`combat`, `boss_fight`}** quick matrix (shortcuts into Kinds).
  - **Scripted-line holds:** per kind, "plays in boss fights", or "held until its scripted-line check passes on
    your build" (S3; spec §1.8). Kinds with no scripted combat lines show "no check needed".
  - The **In battle** reaction rows (shortcuts).
  - `range.fight_m`, `pacing.fight_max` and `pacing.group_gap_s` (shortcuts).
  - **Lines from:** written lines and prepared pools per card (keys ready, generating, paused in combat; NPC
    §9.5); `pools.max_jobs_per_session` (shortcut to Oracles).
  - Labels or feed (shortcut to Lettering).
- **Controls.** As on the home pages.
- **Reads:** allowances, verification states, pool status. **Writes:** only through shortcuts.
- **Spoilers and privacy.** Cards appear once seen (Open 3). No boss names. A pool's lines are not listed:
  generated text is not shown before it plays.
- **Seams.** Boss, invader and creature voices (O43).
- **Ships.** v0.4; in game with v0.6.

### 5.8 Afflictions · Status awareness

- **Purpose.** O40: NPCs know their own statuses, see others', and react in words.
- **Contents.**
  - `features.status_awareness`.
  - The statuses in our words (frost, poison, rot, bleeding, madness, sleep, blight; spec P18), each with
    "live" or "waits on the status check" (S8 verifies each one separately, spec §1.10).
  - The Afflictions reaction rows (shortcuts).
  - The `status_reactions` and `status_awareness` allowances per kind (shortcuts).
- **Controls.** The master switch; shortcuts.
- **Reads:** `features.status_awareness`, S8 states. **Writes:** `features.status_awareness`.
- **Spoilers and privacy.** None. The words are ours and generic.
- **Seams.** Shown greyed as "later version" (O41): fleeing, asking-and-being-healed, gestures, and warnings as a
  status builds (`StatusBuilding`).
- **Ships.** v0.4, with each status live as S8 passes; in game with v0.6.

### 5.9 Parley · Conversation

- **Purpose.** Talking with named NPCs and calling out to soldiers.
- **Contents.**
  - `features.chat`; `features.callout` (from v0.6); `safety.level` (faithful or gentle, O17, with one sentence
    each).
  - Shortcuts: the talk and skip keys; following the game's subtitles.
  - Information:
    - typing is allowed only at safe moments (O9; the freeze is a later opt-in);
    - pad typing through the Steam keyboard if S4 passes (O11);
    - NPCs use your name only after you have told them (O18).
- **Controls.** Switches and a two-way choice.
- **Reads/writes:** `features.chat`, `features.callout`, `safety.level`.
- **Spoilers and privacy.** None.
- **Seams.** Speech input (v1.5, `InputSource::Stt`), an intent list for pads (v1.1, O11), an opt-in freeze
  (v1.x, O9).
- **Ships.** v0.2 (chat, as a file setting), v0.6 (in the menu, with the call-out).

### 5.10 Tidings · World and stories (spoiler-safe)

- **Purpose.** The owner's quest and world awareness, made visible without spoilers: the state of the land as the
  Tarnished has shaped it, and who among the NPCs you have met has heard.
- **Contents.**
  - **The land now:** each world stage that holds, by verified flags only (strict mode; DESIGN §10.6, MEM §12.3).
    Each is shown by its `line` (impersonal, our words; MEM §12.2), in `rank` order.
  - **Who has heard:** for each stage, the met personas who know it, with a certainty word (knows, has heard,
    believes) (`known(P)`).
- **Controls.** None. A Lettering option hides the page (`ui.menu.show_world`, proposed).
- **Reads:** the world view and `known(P)`, restricted to world stages. **Writes:** nothing.
- **Spoilers** (Open 4 recommends this scope):
  - World stages only. Quest tracks and persona stages are never shown, even in safe words: they are other
    people's affairs, discovered in play, and a track step can hold before the Tarnished has seen its effect.
  - No counts, no locked rows, no "x of 9" and no "not yet" (SP5).
  - Each `line` passes SP2 at projection time.
  - An ambiguous step (dead or concluded) keeps MEM §12.8's wording rules.
- **Privacy.** None.
- **Seams.** None.
- **Ships.** v0.5 (every world stage verified or dropped); `castle_fallen` alone from v0.2 if wanted.

### 5.11 Remembrance · Memories and journals

- **Purpose.** What named NPCs remember of you, and the means to forget.
- **Contents.**
  - **List:** personas met in this journey.
  - **Journal:**
    - the summary (≤ 120 words, first person; MEM §2.3);
    - up to 8 keepsakes, each with its basis (said or seen, as an icon), its weight as five marks, what it is
      about, and a "kept" mark on the protected one;
    - disposition in words, and the told name.
  - **Memory work:** the job's state (queued, deferred, parked, done), the last run, the next window ("at rest" on
    a local model), and pending notes as a count.
  - **Conversations:** the same timeline as The Named.
  - **Earlier journeys** (NG+): collapsed and read-only (Open 13; they are never injected, O20).
  - **Storage:** the backup count and the time of the last backup, with no paths (PV5).
- **Controls.**
  - `features.memory`.
  - **Forget this NPC** and **Forget this journey**: a modal names exactly what goes and what stays (MEM §9.5),
    says a backup is taken and that the backup is the only undo, and needs a deliberate second press. In game it
    is cancelled if danger closes the menu.
- **Reads:** journals, facts, memory jobs, conversations, backups. **Writes:** `features.memory`, and the forget
  commands.
- **Spoilers and privacy.**
  - Text is shown as it renders for that persona (Locked(P), MEM §12.8), since it is that persona's memory (SP2).
  - Read-only in v1 (Open 5): editing would bypass the memory job's validation, its claims and the protected
    keepsake.
  - Journal and chat text are never logged and never in a report (PV4).
- **Seams.** `Recall` (scene or vector memory; DESIGN §13); journal editing (Open 5).
- **Ships.** v0.3; in game with v0.6.

### 5.12 Augury · Event monitor

- **Purpose.** What EldenNet noticed and why each line did or did not play. It is the tool for tuning pacing,
  reactions and allowances.
- **Contents.** Three tabs (shown as sub-items in the rail):
  1. **Events:** a live, bounded stream (500 rows in the browser, 100 in game).
     - Columns: time of play; event, in our label (ARCH §8.6 kinds); family (world, named NPCs, the Tarnished,
       fights, speech, start-up and safety); subject and other (SP3; generics by type label); confidence; who
       perceived it (the Tarnished; personas by name; generics as a count); and the reactions it requested.
     - Filters: family, kind, "reactions only". Pause, clear.
  2. **Decisions:** each own-initiative request, with speaker, motive, class (Combat, Ambient, Autonomous) and its
     result: played, or refused by the gap, the group gap, the window (full), the cooldown (with the time left),
     the allowance, the feature switch, a named gate (spec P10), or expiry. It also says where the line came from
     (written, prepared, live).
  3. **Session:** counts per family and per reaction, and lines per minute against the pacing values.
- **Controls.** Filters, pause, clear, and "copy as text" (developer mode only, scrubbed).
- **Reads:** the bus tap and the decision ring (proposed, §9). **Writes:** nothing.
- **Spoilers and privacy.**
  - In player mode, only events the Tarnished perceived (SP7).
  - Plumbing events are hidden: `FlagChanged`, `StageChanged`, readiness, read-group and lifecycle internals.
    `StageChanged` alone would give away that someone's story moved on.
  - Start-up and safety events show in words ("Multiplayer: EldenNet paused").
  - Speech rows show only displayed text.
  - Developer mode (SP8) shows everything, with the spoiler ribbon.
- **Seams.** Hook-based event sources (DESIGN §13) appear as new sources later; `NpcThought` and `DecisionMade`
  stay reserved.
- **Ships.** v0.4 (when the scheduler and the reactions exist); events alone could ship with v0.2.

### 5.13 Oracles · Models and providers

- **Purpose.** Choose and check the models: provider presets, keys, a model per slot, health and costs.
- **Contents.**
  - **Preset:** cloud budget, local 12 GB, local 16 GB+, or custom (ARCH §11.4), with DESIGN §11.3's advice
    (cloud under 12 GB of VRAM).
  - **Providers:** for each provider:
    - id, flavour and kind;
    - base URL (local addresses are shown here only: PV5);
    - key status (set, missing, rejected) with a masked, write-only "Set key" field (PV2);
    - max concurrency, JSON mode, and advanced `headers` and `extra_body`.
  - **Slots:** `dialogue`, `bark_live`, `bark_pool`, `memory` and `selftest`, each with model and fallback (a
    picker filled from `/v1/models`) and its fixed budget, read-only (ARCH §11.4). `meta` is greyed (reserved).
  - **Health** per provider and model: consecutive failures, cooling until, disabled (and why: "key rejected"),
    and the last error class (ARCH §11.2).
  - **Self-test:** reachability, first token, tokens per second, the voice check, the JSON check, and the verdict
    in words (quick, acceptable or slow, with the figures; ARCH §11.7).
  - **Background work:**
    - `pools.max_jobs_per_session`;
    - the pool pre-bake (progress);
    - the local priority order (replies, then own-initiative replies, then `bark_live`, `memory` and `bark_pool`;
      spec P12);
    - the free-tier warning for pools.
  - **Cost:** calls and tokens per hour (from the call log), and the own-initiative bound. Money is shown only
    where the provider reports it in its response (Open 14).
- **Controls.** Preset choice, provider fields, Set key, model pickers, "Find local servers", "Run self-test",
  "Pre-bake", "Run setup again" (First light).
- **Reads:** `providers.toml`, `models.toml`, key status, health, the call log, self-test results. **Writes:**
  `providers.toml`; `secrets.toml` (write-only); `pools.max_jobs_per_session`.
- **Spoilers and privacy.** Keys are never shown or returned (PV2). The base URL is never copied into reports or
  logs (PV5). Network actions run only while the gate allows (DESIGN §3.4; C4: zero requests while gated).
- **Seams.** The `meta` slot for `DecisionRequest` (DESIGN §13); TTS providers (v1.5).
- **Ships.** v0.2 (read-only, plus self-test; the config file is the source); writable with v0.6.

### 5.14 Voice · Speech (later)

- **Purpose.** Holds the v1.5 seam (O8), so players see that voices are planned, and where the `tts` allowances
  already live.
- **Contents in v1:**
  - a short statement: text first in v1, voices in v1.5;
  - the `tts` allowance summary by kind (shortcut to Kinds);
  - each persona's and card's voice key, once content carries `voice_key` (NPC §10.8).

  No disabled controls pretending to work.
- **Reads:** the allowance summary. **Writes:** nothing.
- **Spoilers and privacy.** Later: a microphone (STT) will need a privacy note and push-to-talk.
- **Seams.** `SpeechSink`, `TtsSink`, `PageTiming`, `Line.voice_key` and `InputSource::Stt` (DESIGN §12, §13).
- **Ships.** A greyed rail entry with a "v1.5" badge from v0.6; live in v1.5.

### 5.15 Lettering · Display and subtitles

- **Purpose.** How EldenNet's text and the menu look.
- **Contents.**
  - **Subtitles:** `ui.scale` (75–150%; one scale for the subtitles, labels and menu), `ui.follow_game_subtitles`,
    `features.labels` (floating labels or the feed).
  - **HDR:** `ui.hdr`, `ui.paper_white_nits`.
  - **Menu** (proposed keys under `ui.menu`, §9): page names (flavour with plain subtitles, or plain only); reduce
    motion; key hints on or off; pad glyphs (auto, or a fixed family, drawn by us); backdrop (dim the game or
    opaque; LOOK §4 point 6, settled in GraceUI DESIGN D26); show Tidings.
  - **Preview:** a sample lower-third page and a label in our own words, at the current scale. In game it is drawn
    for 3 s by the real ImGui surfaces.
- **Controls.** Sliders, choices, the preview button.
- **Reads/writes:** `ui.*`, `features.labels`, `ui.menu.*` (proposed).
- **Spoilers and privacy.** None. The preview text is ours.
- **Seams.** Native subtitles (`PresentationSink`, v2 after S9; DESIGN §13).
- **Ships.** v0.6 (both hosts); the keys exist earlier as file settings.

### 5.16 Bindings · Hotkeys

- **Purpose.** Rebind EldenNet's keys.
- **Contents.**
  - Rows: talk (default from S4), skip page, open the menu (default F5, proposed `hotkeys.menu`, Open 12), debug
    view (F11).
  - Each row: the key cap, "Change" (capture: press a key, Esc cancels), reset.
  - A conflict line for our own keys and for the game's default bindings (from our own list of defaults, checked
    by S4).
  - Pad combinations if S4 allows (O11).
- **Controls.** Capture, reset.
- **Reads/writes:** `hotkeys.*`.
- **Spoilers and privacy.** None.
- **Seams.** Push-to-talk (v1.5).
- **Ships.** v0.6. In the browser, capture records the key's name the same way.

### 5.17 Vigil · Diagnostics

- **Purpose.** Is EldenNet healthy on this machine and build, and if not, why?
- **Contents** (tabs as rail sub-items):
  1. **Health:**
     - the level and its reason;
     - the game build and its layout profile, supported or not;
     - the 11 read groups (`world`, `player`, `session`, `flags`, `ui`, `place`, `params`, `text`, `combat`,
       `camera`, `status`): self-test result and breaker trips;
     - the capability matrix in words ("No camera group: the feed replaces floating labels"; DESIGN §4.4);
     - the gate (host, own world, lobby, set count) in words.
  2. **Checks:** for each feature that waits on verification, its state on this build, in player words with the
     spike named for testers (the talk observer S3, flags S6, combat S7, status S8; spec §1.11).
  3. **Performance:** task p99, overlay and menu frame cost, memory; C3's limits beside them.
  4. **Latency:** first token and first page p50 and p95 per slot, and the C5 verdict.
  5. **Storage:** the playthrough database size, backups (count, last), the store's state. No paths and no
     SteamID (PV5).
  6. **Content:** the last reload results (file and line, relative to `overrides/`); active overrides by folder;
     `disable.toml` entries.
  7. **Logs:** a scrubbed tail with a level filter.
  8. **Developer:** `debug.panel`, `record_sessions`, `record_redact_text`, `log_level`, `ui.latency_toasts`.
- **Controls.** Run self-test, re-check, the developer switches, "Open the F11 view" (in game).
- **Reads:** the diagnostics views. **Writes:** `debug.*`, `ui.latency_toasts`.
- **Spoilers and privacy.**
  - Checks name features, never flags, stages or characters.
  - Logs are scrubbed the same way as reports (PV5).
  - Unknown characters are developer-only (Workshop), because "seen this session" includes hidden characters.
- **Seams.** Probation for unknown builds (O12, deferred).
- **Ships.** v0.2 (Health, Latency, Logs); the rest as their sources exist; in game with v0.6, and also at L1.

### 5.18 Missive · Bug report

- **Purpose.** A report a player can share without leaking anything.
- **Contents.**
  - **Step 1:** what happened, as free text with a warning ("don't include personal details").
  - **Step 2:** the bundle preview, every field visible:
    - build, level, groups and matrix;
    - settings (no secrets exist in `settings.toml`);
    - provider kinds and model ids;
    - error classes and counts;
    - latency figures;
    - the recent event kinds (no text);
    - the scrubbed log tail.

    Optional parts can be switched off.
  - **Step 3:** copy, or save a file. The deterministic scan runs before either (DESIGN §14), and a failure blocks
    the copy and names the field.
- **Reads:** the diagnostics views, settings, the scrubbed logs. **Writes:** a file in EldenNet's reports folder,
  on request.
- **Privacy** (PV5, PV7):
  - Never in a report: chat or model text, keys, SteamID, paths, the Windows user name, the character's name, or
    local or LAN endpoint addresses.
  - Never sent anywhere by EldenNet: the player shares it.
- **Ships.** The preview with v0.2; copy and save with v0.6, when the scan test gates (DESIGN §14).

### 5.19 Workshop · Developer (browser only, `debug.panel = true`)

- **Purpose.** Testers' and authors' tools that are spoilers by nature.
- **Contents:**
  - the last prompt and reply (keys redacted);
  - every event, unfiltered;
  - the world view with raw verification states;
  - each persona's known set and the reaction cue that would fire now, or the first condition that fails (as
    F11, DESIGN §14);
  - unknown characters with "copy as TOML" (NPC §2.5);
  - the Lantern-Keeper;
  - recording controls.
- **Guard** (SP8): a "contains spoilers" confirmation once per session, and a red Spoilers ribbon on every page.
  Never on by default.
- **Seams.** Content editors for personas, cards and KB; custom Augur rules (`augur.allow_custom_rules`, shown
  locked).
- **Ships.** v0.2, as the browser mirror of F11's text views. F11 stays the in-game developer surface (GraceUI
  DESIGN D6).

### 5.20 All settings, and First light

- **All settings.** Every setting from §4.1 and §4.2, grouped by home page, each row as on its home page.
  - Filters: changed from default; live, waiting on a check, or later; search.
  - "Reset all" needs confirmation. "Copy settings" puts `settings.toml` on the clipboard (it holds no secrets).
  - Ships with v0.6.
- **First light** (the setup wizard of DESIGN §11.5 and ARCH §11.7), as a full-screen flow:
  - its steps: notices (offline only, save backups, data sent to the provider, AI disclosure); status; provider
    (masked key or local probe); checks 4a–4e; optional pre-bake;
  - it opens at the title screen on first run, and in the browser;
  - it is reachable again from Oracles;
  - recommended: the ImGui wizard stays as the fallback when the menu cannot render (Open 10).

---

## 6. Spoiler rules for every page

- **SP1. Projections, not raw content.** The core sends the UI structured, player-safe data. It never sends flag
  ids, catalogue aliases or `note`s; stage ids, `road`, `gist`, `private`, `forbid`, `knows`, `beckon`, ties or
  `heard` lines; KB text; prompts; or model text that was not displayed. The only exception is Workshop (SP8).
  The only authored texts the UI shows are written to be impersonal: `public.summary`, a world stage's `line`, our
  labels and help text.
- **SP2. The lock filter.** Every content or model string passes a lock check per sentence at projection time:
  - global strings (labels, `line`, `public.summary`): the Tarnished's set, `(G_locked ∪ S_locked) ∪ R_locked`,
    minus runtime names the game has shown (MEM §7.2);
  - a persona's own strings (journal, its lines): `Locked(P)`, as they render for it (MEM §12.8).

  A failing item is dropped whole (no "…" placeholder) and counted in Vigil.
- **SP3. Names as the game shows them.** The on-screen name is the runtime NpcName of the current row, or
  `display_name_fallback` when text is down (NPC §2.7). A veiled persona stays veiled. Region names are the
  runtime place names of regions entered. GraceUI never stores a name.
- **SP4. Seen before listed.** Other named slots appear once seen (DESIGN §7.6). Recommended: cards and creature
  families too (Open 3). *(The owner chose otherwise on 2026-11-07, GraceUI DESIGN G5 and G8: every NPC type, and every
  named NPC with a shipped general profile, is listed from the start in first-meeting words; other named slots still
  appear once seen. Every named boss with a shipped bio joins the roster the same way, GraceUI DESIGN D67.)* Regions
  appear once entered, and bosses never by name outside a live bar.
- **SP5. Absence is silent.** No counts, progress, placeholders, greyed rows or "not yet" for world stages, tracks,
  stages, bosses, regions or reactions not reached. This mirrors "no path means silence, never not yet" (DESIGN
  §10.6). The look study's veiled treatment is used only for a perceived character whose name the game has not
  shown ("Someone you have not met"). It is never used for a story item (see §12).
- **SP6. Strict mode in the UI.** An unverified flag never unlocks anything the UI shows (MEM §6.4, §7.7).
- **SP7. Perception.** Live views show only what the Tarnished perceived: characters on screen within
  `range.hear_m` (with the `camera` group), engaged with or near the Tarnished, or met personas within range;
  events whose audience includes the Tarnished (MEM §1.3); lines that were displayed. Never hidden characters,
  ambushes, characters behind walls, a boss before its bar, an invader before the game announces it, HP numbers,
  or distances to hostiles.
- **SP8. Developer mode only behind a gate.** Unfiltered views exist only in Workshop and F11, with `debug.panel`
  on, a confirmation each session, and a visible ribbon.
- **SP9. Search and titles.** Search indexes no story or chat text (§3.3). Page titles and URLs carry no chat
  text.

## 7. Privacy rules

- **PV1. Loopback only.** The browser host binds 127.0.0.1 with a per-session token, and Host and Origin checks
  (GraceUI DESIGN §7). There is no remote access. *(GraceUI DESIGN PV1 now uses a session cookie instead of the token,
  after G4, and admits devices on the home network once the player switches that on and pairs each with a code shown
  in game, after G9.)*
- **PV2. Write-only secrets.** Keys go in and never come out: the UI sees set, missing or rejected. The
  character's name shows only in the header and on pages about the player's own story.
- **PV3. GraceUI keeps nothing.** No page content in GraceUI logs, local storage or caches. The release build has
  no console output and no analytics.
- **PV4. Chat and model text stay on screen.** They appear only on Grace's activity list, The Named and
  Remembrance. They are never in a log, a report, a URL, a toast log or search.
- **PV5. Scrubbing.** Vigil's logs, Augury's copy and Missive scrub: SteamID64 (it is in the save path, DESIGN
  §10.5), absolute paths and the Windows user name, the character's name, and base-URL hosts that are not a known
  public provider (local and LAN addresses).
- **PV6. No outside requests.** Fonts and assets are bundled, with no CDN and `default-src 'self'`.
- **PV7. Clipboard only on a click**, and after a preview.
- **PV8. In game, nothing about the menu is recorded.** Session recordings (`record_sessions`) never capture menu
  content.

---

## 8. The settings row and its badges

- **Row:**
  - the label and the control;
  - a changed-from-default lozenge with a reset button (LOOK §3.9);
  - a help line that opens on focus;
  - the effect timing (Now, Next moment, Next request, Next launch) as a faint word;
  - an availability badge;
  - a "lives in …" link on shortcut rows.
- **Availability badges** (player words; the tester detail is in Vigil › Checks):

  | Badge | Meaning |
  |---|---|
  | (none) | Live |
  | Waits on a game check | Its spike has not passed on this build. The setting is kept, and does nothing until the check passes |
  | Takes effect in v1.5 | `tts` |
  | Later version | Locked: `actions`, `gestures`, custom rules |
  | No lines in this version | Inert type: no voice source in v1 |
  | Off because … | A dependency ("Conversation is off"; "`dialogue` off for this type") |

- **"Always on"** rules (the not-disableable list of ARCH §11.6 and spec §1.3) are stated once per page where they
  matter. They are never drawn as disabled toggles.
- **Schema.** Every row is generated from a settings description that EldenNet exports (Open 6): key, type,
  default, bounds, unit, label, help, plain synonyms, home page, effect timing, availability source, and whether
  it is locked. GraceUI owns only the page layout (groups and order).

---

## 9. What GraceUI needs from EldenNet (new or changed; each to be raised in EldenNet)

| Need | Why | Cost |
|---|---|---|
| A settings description export (JSON from the Rust types, plus labels and help) | One source for labels, bounds and availability; rows generated in both hosts | Small; versioned with the bridge |
| A player-safe projection layer (SP1, SP2, SP7) in the core | The UI never sees raw content | Moderate; the lock sets exist already |
| A bounded decision ring (about 256 entries): every admission result with its refusing rule | Augury › Decisions, and the stats on Reactions | Small; the scheduler already computes it (spec P2) |
| `types_seen` per playthrough (type ids the Tarnished has seen) | SP4 for cards and creature families (Open 3; not needed since G5, 2026-11-07) | Tiny; one fact table |
| A session-only "pause AI" command that does not write `enabled` | Pause without changing the next launch | Small |
| `hotkeys.menu` (default F5), with `hotkeys.settings` retired (schema 3, or an amendment to schema 2 before v0.6) | The F5 menu (GraceUI DESIGN D6); `deny_unknown_fields` refuses unknown keys | Small |
| `ui.menu.*` (names, reduce motion, key hints, pad glyphs, backdrop, show Tidings), and the browser dashboard switch (`dashboard.enabled`, GraceUI DESIGN G4) | Menu preferences shared by both hosts in one file (Open 15) | Small |
| A bounded event tap on the bus (500 entries) | Augury › Events | Small; ARCH §6.6's order is kept |
| The model call log as a view (slot, purpose, timings, tokens, class) | Grace and Vigil | Exists in traces; needs a projection |
| A subtitle preview command | Lettering | Small |

---

## 10. Increments (GraceUI pages by EldenNet increment; DESIGN §16.2, spec P17)

| EldenNet | Pages | Hosts |
|---|---|---|
| Before v0.2 | Shell, rail, search, the desktop harness against a fake EldenNet, the mock | Harness only |
| v0.1 Eyes | None: F11 covers testing | — |
| v0.2 First words | Grace (status, model calls, activity), Oracles (read-only, self-test), Vigil (Health, Latency, Logs, Developer), Missive (preview), Workshop | Browser, if the dashboard is on (GraceUI DESIGN G4) |
| v0.3 Remembrance | The Named (Kalé), Remembrance with Forget, Grace › Nearby, Beckoning (named NPCs) | Browser |
| v0.4 War cries | Kinds, Pacing, Reactions, Beckoning (all), War cries, Afflictions (as S8 passes), Augury; writable in the browser (Open 11) | Browser |
| v0.5 Veil | Tidings; The Named for all six personas | Browser |
| v0.6 Threshold | The in-game host on F5 (safe moments; F10 folds in); every page writable in both hosts; All settings; First light; Lettering; Bindings; Parley with the call-out; Missive's copy behind the scan test | Both |
| v1.0 | Accessibility pass, pad mode, Deck scale, the full matrix | Both |
| v1.5 | Voice goes live (TTS, push-to-talk) | Both |
| Later | Acts columns live; Workshop editors; custom rules | — |

---

## 11. Candidate Open rows (with recommendations), and where GraceUI DESIGN resolved them

| # | Question | Options | Recommendation | Resolved in GraceUI DESIGN |
|---|---|---|---|---|
| 1 | Page names | (a) flavour with a plain subtitle, both searchable, plus a "plain names" option; (b) plain only; (c) flavour only | **(a)** | D4 |
| 2 | Unmet named NPCs | (a) listed by fallback name, allowances only (DESIGN §7.6 as written); (b) hidden until seen | **(a)**: the cast is published, and the fallback is the pre-reveal name | D12, superseded by G8 (the owner, 2026-11-07) |
| 3 | Types not yet met (cards, creature families) | (a) all listed (DESIGN §7.6 as written); (b) listed once seen, with a `types_seen` record and a "show all (may spoil)" switch | **(b)**: a full list of creature families reveals late-game creatures | G5: the owner chose (a), 2026-11-07 |
| 4 | What Tidings shows | (a) world stages that hold, verified; (b) also tracks and persona stages in safe words; (c) no world page in v1 | **(a)** | D11 |
| 5 | Editing memories | (a) read-only plus Forget; (b) edit keepsakes through the validator; (c) full editor | **(a)** in v1; (b) later | D13 |
| 6 | Where labels and help live | (a) EldenNet exports a settings description; (b) GraceUI owns labels | **(a)**: one source for the README and both hosts | D8 |
| 7 | Live views | (a) perception-filtered for players, full in developer mode; (b) always full | **(a)** | D10 |
| 8 | Developer surfaces | (a) F11 in game, plus Workshop in the browser behind `debug.panel`; (b) Workshop in game too; (c) none in v1 | **(a)** | D6, D10 |
| 9 | Pausing | (a) a session-only pause command; (b) reuse `enabled` | **(a)** | D15 |
| 10 | The wizard | (a) First light in GraceUI, ImGui as fallback; (b) ImGui only | **(a)** | D16 (First light in both views; no separate fallback, because the in-game view is ImGui under G1) |
| 11 | When browser writes begin | (a) v0.4, when the keys exist; (b) v0.6 with the in-game host | **(a)**: testers tune pacing in v0.4 (X2) | D17 |
| 12 | Hotkey | (a) `hotkeys.menu = "F5"`, F10 unbound by migration; (b) F10 also opens the menu at All settings | **(a)**: fewer keys to collide (S4) | D6 |
| 13 | NG+ journeys in Remembrance | (a) read-only, collapsed; (b) hidden (strictest reading of O20) | **(a)**: O20 governs prompts, not the player's own view | D13 |
| 14 | Money in the cost display | (a) tokens and calls; money only when the provider reports it; (b) a price table in `models.toml` | **(a)**: prices are volatile (ER06) | D18 |
| 15 | Where menu preferences live | (a) `settings.toml` `ui.menu`; (b) browser storage | **(a)**: one file for both hosts | D19 |
| 16 | Pacing presets | (a) Quiet, Default and Lively, writing the six keys; (b) none | **(a)**, with values tuned by X2 | D20 |

---

## 12. Conflicts and notes for EldenNet (carried into [eldennet-changes.md](../eldennet-changes.md))

1. **ARCH §11.5's example uses an allowance key that does not exist.** `[allow."card:vulgar_militia"]` sets
   `greet_npc = false`, but `greet_npc` is a reaction id. The allowance keys (DESIGN §7.6) are `npc_to_npc` and
   `reactions = { greet_npc = false }`. With strict parsing, the example is an error. The Kinds page needs the
   exact key set, so the example should be fixed in EldenNet.
2. **NPC §10.6 (the F10 panel) and DESIGN §12's "Settings" bullet are out of date.** They still list "ambient
   lines" and bark density, which schema 2 replaced with `pacing.idle_max`. GraceUI's IA supersedes both, and they
   should point to GraceUI's design.
3. **The look study's veiled state** gives "A quest not yet begun" as an example. That conflicts with SP5 (and
   with the study's own "unreached pages are absent"). Keep veiling for unnamed perceived characters only.
4. **DESIGN §7.6 lists every type** whether or not the Tarnished has seen it. See Open 3. *(No conflict since G5,
   2026-11-07: GraceUI lists every type from the start too.)*
5. **`hotkeys.settings = "F10"`** is in schema 2. The F5 menu key needs a schema change before v0.6, because of
   `deny_unknown_fields` (Open 12).
6. **The speaking-up design is still being written.** Re-check §4.1 against ARCH §11.5 and NPC §9.1 when that work
   lands.
7. **Browser live views pause while EldenNet is dormant.** C4's "zero requests while gated" concerns model
   requests. The loopback server is not network traffic in that sense, but say so in EldenNet's design to avoid a
   misreading.

---

## 13. Provenance

Read for this note:
- EldenNet's design documents, through `wp show` and grep with line numbers: SN01 §9; SN00 row 38; DESIGN §2.7,
  §3.4, §4.4, §7.3, §7.6, §10.2, §10.4–§10.6, §11.2–§11.5, §12, §13, §14 and §16.2; ARCH §1.2, §1.5, §8.6 and
  §11.1–§11.7; NPC §2.5, §2.7 and §10.1–§10.8; MEM §2.1, §2.3, §7.1, §7.2, §9.5, §12.2 and §12.8.
- The speaking-up work's two briefs, its specification (§1.0–§1.5, §1.8–§1.11) and its event list (working documents,
  2026-11-07).
- severactions-look.md (§3.7–§3.9 and §4).
- The owner's screenshot.

Not opened: any SkyrimNet, SeverActions or Magelight file. No helper script was written, and nothing was copied
from a third party.
