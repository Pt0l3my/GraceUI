# Proposal 0006: Five additions for mods' pages

**Status: proposal, 2026-11-20, put to the owner and EldenNet's session (G33); EldenNet's session answered the same
day (§8), and §4's recommendation follows its answer; not built.** EldenNet's session met
these while building its first pages (Home, System, Settings > Developer; its WHY§266 to §270) and listed them with
D184's in-game components, which GraceUI 0.2.6 built. None blocks EldenNet: each has a stand-in today. They change the
page data's format, or add a framework feature every mod may use (G16, G18), so they are put here rather than settled
alone. A bare § is a section of [DESIGN.md](../DESIGN.md).

## 1. One topic feeding several blocks (`props.field`)

**Today** each component block draws its topic's whole body, so a page with two key-value lists needs two topics
(EldenNet's `system.status` and `system.compatibility`). **Proposed:** a component block's optional `props.field`
names a member of its topic's body, which the block draws as if it were the body: `{"status": {"items": [...]},
"compatibility": {"items": [...]}}` feeds two blocks from one topic. The page lint checks that the field is a name
(1 to 32 of `a-z`, `0-9`, `_`), and both views read it alike; a body without the field is drawn as empty.
*Recommended.* *Alternative:* keep one topic a block (no change; more topics, each watched on its own).

## 2. A block's own empty-state words (`props.empty`)

**Today** an empty block says "Nothing to show yet." in both views. **Proposed:** `props.empty`, 1 to 80 characters
of the space's own words ("No problems"), shown only once the topic has published a body with nothing in it; before
its first state the block still says "Nothing to show yet.", so that a page never claims "No problems" while it is
still loading (invariant 2: nothing is implied before it is published). *Recommended.*

## 3. A log tail newest first (`props.newest_first`)

**Today** a log tail lists its rows oldest first, and a live feed newest first. **Proposed:** `props.newest_first`
(a boolean) on `log_tail`, for EldenNet's Logs (its PAGES). *Recommended.* It is drawn in the browser view; in game the
streams stay the browser's (D184) until a later change draws them.

## 4. A greyed state a space sets

**Today** a page cannot say that its space is inactive: EldenNet's PAGES §4.4 greys Home at L1 with "EldenNet is
inactive: reason", and has only a statement block and the banner notice for it. **Options:** (a) **a framework topic
`space.state`** a space may declare and publish, `{"inactive": "reason in the space's words"}` or `{}`, which greys
every page of the space in both views (its controls inert, as a locked setting's) under one line with the reason, and
lifts when the topic says so; (b) the same per page (`{"pages": {"eldennet.home": "reason"}}`); (c) nothing: the space
uses a statement block. *Recommended, after EldenNet's answer (§8):* (b) from the start, in the shape
`{"greyed": {"<page id>": "<the space's words>"}}`, with (a) beside it as a shorthand (`{"inactive": "..."}` greys
every page not named): a page in the map is greyed with its own words in both views, a page not in it is usable. An
inactive space is rarely inactive everywhere: EldenNet keeps its System, Settings and Docs pages usable while the rest
are greyed, and greys some pages only at the title screen. Commands to an inactive space still go to it, which
answers `not_now` as it does today: the grey is what the player sees, the space's answer is the rule (invariant 3).

## 5. A shortcut row's control in game

**Today** a shortcut row in game is a link to the setting's home ("Lives in Behaviour"), while §4.7 and the browser
draw the label, the control and the link. **Proposed:** in game too, the control and the link: ← and → change the
value as on the setting's own row, Enter follows the link, and a switch turns with ← and → only (Enter stays the link).
The change goes to the space as the home's would, through its one entry point. *Recommended.* *Alternative:* keep the
link alone in game (one place to change each setting there).

## 6. What each changes, and its cost

1 to 3: `protocol.toml`'s component props, the page lint, both views, and the fixtures and goldens; no change of the
ABI. 4: one more framework topic name the registry and the server know (as `pages`, D183), both views' grey, and the
harness. 5: grace-view alone, and its goldens. Each in its own commit and D-entry, in the order the owner chooses;
EldenNet's order of need is the order above.

## 7. For the owner and EldenNet's session

Which to build, and for 4, which option. EldenNet's session asked for all five; it said none blocks it.

## 8. EldenNet's answer (2026-11-20)

Items 1, 2, 3 and 5 fit its PAGES as written. Item 4 must be per page from the start: at L1 its PAGES §4.4 keeps
System, Tools > Bug report, Settings, Docs and Voice > Text and subtitles usable and greys the rest with "EldenNet is
inactive: reason"; at the title screen, Playthrough, History and Event monitor read "Load a character to see this"
while the rest stay usable. So the greyed state is a map from page id to its words (§4's recommendation now), with a
whole-space shorthand beside it if wanted. With that change EldenNet's session recommends the proposal to the owner as
written.
