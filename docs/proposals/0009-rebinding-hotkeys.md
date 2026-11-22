# Proposal 0009: Rebinding hotkeys

**Status: proposal, 2026-11-22, put to the owner and EldenNet's session as G37. The owner approved it as recommended
the same day, to be built now (G37); item 5 waits for EldenNet's session.** A bare § is a section of
[DESIGN.md](../DESIGN.md); PAGES is EldenNet's `docs/design/pages.md`.

## 1. Why

Since GraceUI 0.2.15 (D206) the Hotkeys page lists every mod's hotkeys with their keys, pad chords and states. Nothing
can change them yet:

- A hotkey whose default another took first registers unbound (D77, D204), and fires nothing until the player binds it.
  With chords built, two mods asking for the same chord is likely: there are few free pad gestures.
- The menu's key is `hotkeys.menu`, a GraceUI setting (D115), but GraceUI 0.2 reads F5 alone. Since 0.2.15 its row is
  shown as "Later version" so that the browser does not change a setting nothing reads.
- The design says two things. §5.2: "the player binds every mod's keys there" (on GraceUI's Hotkeys page). D115:
  "every registered hotkey listed with its space for its conflicts, each changed on its own space's page". PAGES
  follows D115: EldenNet's Settings › Hotkeys holds `hotkeys.talk`, `hotkeys.skip_page` and `hotkeys.debug`, with
  capture, conflicts and reset, and links to GraceUI › Hotkeys for the menu's key and every other mod's.
- ABI 1 has no slot to change or remove a hotkey: a mod that changed its own key setting could not apply it.

## 2. Where a binding is changed

- **(a) On GraceUI's Hotkeys page, for every mod.** *Recommended.* Each row of the list gets a Change action (G34's
  row actions, built) and a Reset to its default. The player's choice is kept in GraceUI's settings file (D84) under
  the mod's id and the action's id, and applied each time the mod registers the hotkey, so a mod's update that changes
  its default does not undo it, and a binding for a mod that is not installed stays for when it is. One home for each
  binding (invariant 12), every conflict seen in one place, and it works for a mod with no pages, which is most mods
  that only want a key. A mod's own page may carry a link to GraceUI › Hotkeys where a player would look (D115 allows
  navigation links). For EldenNet: `hotkeys.talk`, `hotkeys.skip_page` and `hotkeys.debug` stop being EldenNet
  settings and become the defaults it registers (T, its skip key, F11); its Settings › Hotkeys becomes a link, or a
  read-only list of its own hotkeys from GraceUI's topic.
- **(b) On each mod's own page (D115 and PAGES as written).** A mod keeps its keys as `key_name` settings of its own
  space, and applies a change through a new ABI slot, `rebind_hotkey`; GraceUI's page lists all of them and changes
  only the menu's key and the hotkeys of mods with no page. Each mod writes its own capture and conflict handling
  against keys it cannot see (other mods'), learning of a clash only from `GRACEUI_UNBOUND`.
- **(c) Both.** Not offered: a binding with two homes breaks invariant 12 (drift between the views and the pages).

## 3. How a key is chosen (under (a))

- **In game.** Change opens a small modal: "Press the key for ‹action›". The next fresh press, with its Ctrl, Shift
  and Alt, is taken; the press never reaches the game (the menu holds input, invariant 8). Escape cancels (it is the
  player's exit, never bindable); Escape, the modifiers alone, the Windows keys and Alt+F4 are refused in words, as
  `register_hotkey` refuses them. A "No key" button leaves the hotkey with its chord alone, or unbound.
- **In the browser.** The same modal records `KeyboardEvent.code`, mapped to GraceUI's key names, which D206 made after
  it (A, 7, F5, PageUp, NumpadEnter); keys the browser keeps for itself (F5, F11) are taken only while the field has
  focus, with a note saying so (§5.9).
- **A pad chord** is chosen with two pickers, anchor and key, in both views, from XInput's fourteen buttons. The browser
  has no pad in v1 (§5.9), and the owner tests with keyboard and mouse only (D205), so a chord captured by holding it
  on a pad waits for testers.
- **The menu's key** becomes live: `hotkeys.menu` takes effect at once in the window procedure and DirectInput's view;
  a mod's hotkey on the new key becomes unbound, as at registration. The menu key keeps no pad chord (D33).

## 4. When the chosen key is taken

- **(a) Ask, then move it.** *Recommended.* "F9 is Tide Charts's Show the tide table. Use it here and leave that one
  unbound?" with Cancel first; on yes the other hotkey becomes unbound, its row says so, and nothing else changes.
- **(b) Refuse**, naming the holder; the player frees it first.

GraceUI cannot see the game's own keys (it reads nothing of the game): the modal's help says that a key the game uses
is hidden from the game while a hotkey has it bound with no modifier, as today (D155).

## 5. What a mod learns

A hotkey's presses come as before (`GRACEUI_EVENT_HOTKEY`), whatever its key. A mod that names its key in its own
words ("Press T to talk", EldenNet's call-out) needs the key as the player bound it:

- **(a) An ABI 1.4 call, `describe_hotkey(consumer, id, out)`**, filling the key's and the chord's text as the Hotkeys
  page writes them ("Ctrl+K", "D-pad Up, then View", empty for none), and a `GRACEUI_EVENT_HOTKEY_CHANGED` when the
  player changes it. *Recommended.*
- **(b) Nothing**: a mod words its text without the key ("your talk key").

## 6. Cost

`grace-input`: rebind, unbind and the player's bindings over the defaults. `grace-host`: the modal's capture, the
settings file's `hotkey_bindings` list (ids and key names only), the menu's key made live. The page: Change and Reset
as row actions, a Reset every hotkey page action. Both views: the capture modal and the two chord pickers. The lint,
the unit tests, a harness scenario (capture in game, a conflict, the menu's key moved, a binding kept across
sessions). With 5 (a): ABI 1.4, an addition. EldenNet: its three key settings retired, its Settings › Hotkeys
re-pointed, its call-out's words from `describe_hotkey`.

## 7. Not covered

Bindings per context (in a menu, on a horse), hold and tap kinds, mouse buttons as hotkeys, and Steam Input's own
mapping (the documentation's advice of D33 stands).

## 8. For the owner and EldenNet's session

1. Where: (a) GraceUI's Hotkeys page for every mod, or (b) each mod's own page?
2. A key already taken: (a) ask, then move it, or (b) refuse?
3. Pad chords chosen with two pickers in both views, capture on a pad later: yes or no?
4. A mod reads its hotkey's current key (ABI 1.4): (a) yes, or (b) no?
5. For EldenNet's session: under 1 (a), does retiring `hotkeys.talk`, `hotkeys.skip_page` and `hotkeys.debug` as
   settings, in favour of registered defaults, suit PAGES and O94?

When it is built is the owner's: GraceUI's plan put the Hotkeys page's changes in 0.6 (§10); with unbound hotkeys
listed now, it could come in 0.2.x, after EldenNet's R1 or before it.
