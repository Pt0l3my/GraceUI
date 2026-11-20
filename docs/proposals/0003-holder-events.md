# Proposal 0003: A holder's events through ABI 1

**Status: proposal, 2026-11-20, put to EldenNet's session to check against its chat box (S11.4.c). Agreed by it the
same day, with a fifth kind for a pad's buttons (§6), and folded into DESIGN §13 as D176; where this text and the
design differ, the design holds. [graceui.h](../../src/grace-abi/include/graceui.h) gains it as ABI 1.1 when it is
built.** A bare § is a section of [DESIGN.md](../DESIGN.md).

**Built, 2026-11-20, in GraceUI 0.2.1** (D176's as-built note): `flags` is appended after `reserved` rather than
replacing it, as ABI 1's append-only rule requires; a pointer event carries `button_event`; a set_caret from a mod
that does not hold input is ignored; §2's "a pad's buttons: a later kind" was overtaken by §6's answer.

## 1. Why

A mod's panel that takes the controls (`request_input`, D142) gets nothing of what the player types: the window
procedure hands the holder's keys and characters to GraceUI's own Dear ImGui context, and ABI 1's `poll` carries
hotkeys and the bridge protocol's frames only (WHY§241, found by the spike observer's typing test, D175). D142 had
promised more: "What a holder learns of its own end, and its keys, characters and pointer, reach it through `poll`".
EldenNet's chat box (its DESIGN §8.1) is the first mod that needs it: v0.2 First words types into it.

## 2. What it settles (D176, proposed)

**Opt in, per request.** `GraceUiInputRequest`'s `reserved` word becomes `flags`; with `GRACEUI_INPUT_EVENTS` (1) the
holder's input comes to its mod through `poll`. Without it (every mod built for ABI 1.0 sends 0), nothing changes:
the input is taken from the game, as now. An event struct smaller than ABI 1.1's never gets the new kinds, as the
protocol's kinds were gated in 0.2 (D169).

**Four kinds**, each only for the mod that holds input, only while it holds it (tied to the holder's generation), and
never for any other mod:

| Kind | Fields | When |
|---|---|---|
| `GRACEUI_EVENT_KEY` (5) | `key` (the virtual key), `modifiers` (`GRACEUI_MOD_*`), `pressed` (1 press, 0 release), `repeat` (1 for an auto-repeat) | each press, repeat and release of a key the holder takes |
| `GRACEUI_EVENT_CHARACTER` (6) | `code_point` (one Unicode scalar value) | each character typed: a surrogate pair joined, an ANSI window's bytes decoded, the IME's committed text, the Steam floating keyboard's |
| `GRACEUI_EVENT_POINTER` (7) | `x`, `y` (in the panel's own pixels, D137; outside it when the pointer is), `button` and `pressed` for a button, `wheel_x`, `wheel_y` | while the holder takes the mouse: moves (only the newest position since the last poll), buttons and the wheel |
| `GRACEUI_EVENT_INPUT_ENDED` (8) | `why` (`GRACEUI_CLOSED_*`: released, menu key, danger, loading, focus lost, no render, fault, player) | once, when the mod's holder ends, by whatever path (invariant 7): D77's "tells the holder why" |

Every new kind carries `dropped`: how many of this holder's events were lost before it because the mod polled too
slowly (a ring of 256 a holder; the newest are dropped, never reordered).

**What GraceUI keeps.** The menu key never reaches a mod (it is GraceUI's, invariant 17); a single Esc does, as a key
(a chat box's "cancel"), but Esc held 1 s, or the menu key's second press, is the player's exit, never captured: the
holder ends and its mod gets `INPUT_ENDED` with `player`. Alt+F4 still reaches the game. Hotkeys stand aside while a
holder has input (D77). When a holder ends, its events not yet polled are discarded, and `INPUT_ENDED` comes after
them, last.

**The caret, for the IME.** A new slot at the table's end, `set_caret(consumer, const GraceUiCaret*)`: the mod's panel
and the caret's rectangle in that panel's pixels (height 0: none). GraceUI turns it into the game window's pixels and
places the OS composition and candidate windows there (§5.6, D57's rule for GraceUI's own fields). Only the holder's
caret counts. Drawing the composition inside the field stays later work, as it is for GraceUI's own fields.

**Not GraceUI's.** The clipboard: Ctrl+C, Ctrl+V and the like come as key events, and a mod that wants them reads or
writes the clipboard itself; GraceUI never reads it. The Steam floating keyboard: a mod opens it itself
(EldenNet does, its O11), and what it types arrives as characters. A pad's buttons: a later kind, if a mod needs one
(the menu reads the pad itself).

**For mods on Dear ImGui.** `graceui_imgui.hpp` gains `graceui::imgui::feed(io, event)`, which turns these events into
the mod's own context's input (keys, characters, the pointer), so a headless Dear ImGui's `InputText` works in a mod's
panel with no backend (invariant 16 holds: the mod's context is its own, and GraceUI hands it nothing).

**Privacy.** What is typed goes from the window procedure to the holder's mod and nowhere else: GraceUI never logs,
stores or counts it outside a test build, and a secret typed into a mod's field leaves only through that mod
(invariant 5, PV rules).

**Version.** The four kinds, the flag and `set_caret` are ABI 1.1, shown by `has_cap("holder-events")`
(`GRACEUI_CAP_HOLDER_EVENTS`), in GraceUI 0.2.1: a patch number, because GraceUI's minor follows its increments (§10),
and the change adds and changes nothing else. A mod that needs it asks for floor 0.2.1 and checks the capability.

## 3. What it does not do

- Feed the mod's Dear ImGui context from inside GraceUI: a context crossing the ABI is what invariant 16 forbids.
- A text-field widget of GraceUI's for mods' panels: a panel is the mod's own draw lists (D76); D57's field stays
  GraceUI's own pages' widget. A mod on Dear ImGui has `InputText` through `feed`.
- Characters only: a text field needs Enter, Backspace, Delete, the arrows, Home and End, with Ctrl and Shift.
- Characters batched as UTF-8 in the event's frame buffer: one code point an event is simpler, and typing is slow.
- Telling a refused mod when input is free (D77's "may ask to be told"): a later kind, when a mod needs it.

## 4. How it is tested

In the harness under Wine, on both toolchains: a mod's panel that asks with `GRACEUI_INPUT_EVENTS` gets keys with
their modifiers and repeats, characters (a surrogate pair, an ANSI window's double-byte character, an IME result),
the pointer in its panel's pixels, and `INPUT_ENDED` for each way out (released, hidden, the player's exit, danger,
focus lost); a mod that asked without the flag, and one built against ABI 1.0's event size, get none of the new kinds;
no event reaches a mod that does not hold input; the menu is unchanged; `set_caret` moves the IME's windows. The
spike observer's typing test then counts what its own panel received, as well as the test build's counter.

## 5. For EldenNet's session to check

1. Does the chat box want the pointer (a click to place the caret, to scroll)? It costs little, and D142 promised it.
2. Are pad buttons needed beyond the Steam floating keyboard's characters (for instance, a pad's B as "send" or
   "cancel")? If so, a fifth kind now rather than later.
3. GraceUI 0.2.1 as the version EldenNet's v0.2 asks for.
4. Is "told when input is free" wanted for v0.2, or later?

About a day's work once agreed, with its harness checks.

## 6. EldenNet's answers (2026-11-20)

1. **The pointer: yes.** While the chat box holds input, Dear ImGui draws its own cursor (EldenNet's NPC §5.5); a
   click places the caret and the wheel scrolls the box's history strip.
2. **Pad buttons: yes, a fifth kind now.** A pad player has no keyboard: while the box holds input, B is "cancel" and
   A is "send" once the Steam floating keyboard has closed. `GRACEUI_EVENT_PAD` (9): `button` (an XInput button's
   mask), `pressed`, `pad` (its index), delivered as `KEY` is, only while the holder takes the pad, the closing press
   masked from the game until released (invariant 8). XInput's buttons only; no sticks or triggers.
3. **GraceUI 0.2.1** is EldenNet v0.2's floor, with `has_cap("holder-events")` checked at connect.
4. **"Told when input is free": later.** In v0.2 a refused request gives a toast, and the player presses the talk key
   again.

Raised for later, not for 0.2.1: EldenNet's pad gesture that opens the box is a chord, View + D-pad Up held 0.4 s,
with View masked from the game while D-pad Up has been held 150 ms or more and replayed as a short tap if the chord
does not complete (its NPC §5.6, O11). `register_hotkey` answers `GRACEUI_NOT_YET` for a pad chord today; GraceUI
puts the chord and its masking to EldenNet's session as a D-entry for S4's second round, after R1.

