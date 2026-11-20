# Proposal 0004: The display, for every mod

**Status: proposal, 2026-11-20, put to EldenNet's session for its subtitles and boxes (S11.4.c) and answered by it the
same day; the owner's go-ahead the same day ("go ahead with both in 0.2.2", with the saving of GraceUI's settings,
D179). Built as D180 in GraceUI 0.2.2 (ABI 1.2); where this text and the design differ, the design holds.** A bare §
is a section of [DESIGN.md](../DESIGN.md).

## 1. Why

§3.13 and D84 promise mods "the host's display-changed event, which carries the scale", and EldenNet's text follows the
scale by it (its NPC §10.7; its O93 makes that `(H / 1080) × ui.scale / 1.25`). ABI 1 never had it: GraceUI 0.2.1's
poll kinds are 1 to 9, and none says the display's size or `ui.scale`. Found when EldenNet asked for `ui.scale`'s
default beside its value (D177 made it 1.25).

A second question comes with it. A mod's faces go into the shared atlas in its registration window, before warm-up
(D139), and the atlas is baked once (invariant 11). A mod that sizes its text by the display must size its faces before
it can know the display: it usually connects before the game's swap chain is bound.

## 2. What it adds

**At connect.** `GraceUiConnectAnswer` gains `ui_scale` (the player's, kept from the last session since D179, or the
default), `ui_scale_default`, and the back buffer's `display_width` and `display_height` (0 until the game's swap chain
is bound, which is usual at connect).

**An event.** `GRACEUI_EVENT_DISPLAY` (10), through `poll`, to every mod that polls with an event of ABI 1.2's size:
once after warm-up, then once after each change of the size (a resize, a new swap chain) or of `ui.scale`, coalesced
(a mod that polls late gets the newest only). Its fields: `display_width`, `display_height`, `ui_scale`,
`ui_scale_default`, `scale` (the effective scale, `(H / 1080) × ui.scale`, GraceUI's own text's), `content_scale`
(`scale / ui_scale_default`: a mod's design size at 1080 lines and the default Menu size, so EldenNet needs no constant)
and `faces_scale` (the content scale its scaled faces were baked at; 0 for a mod with none). A mod polling with a
smaller event never gets this kind.

**Scaled faces.** `GraceUiFaceDesc` gains `flags`; with `GRACEUI_FACE_SCALED` a face is baked at warm-up at its pixel
height times the content scale, rounded to a whole pixel, within 6 to 128 px: the mod registers its design size and
GraceUI, which knows the display and `ui.scale` at warm-up, sizes it. Its glyph table gives the size it was baked at.
The atlas's room is counted at a scale of 1 when the face registers; a larger bake that does not fit leaves faces out as
an atlas that does not fit does (D139).

**After a change.** The atlas is not baked again (invariant 11). After a resize or a new `ui.scale` a mod draws its
scaled faces' glyphs scaled by `content_scale / faces_scale`, a little soft, until the next session, which bakes at the
kept `ui.scale` (D179). GraceUI's own menu does the same with its faces.

**Version.** ABI 1.2, `has_cap("display")` (`GRACEUI_CAP_DISPLAY`), in GraceUI 0.2.2; no new slot.

## 3. What it does not do

- Rebuild the atlas when the display changes: an upload after load is what invariant 11 forbids (the map's, G26, is the
  one exception).
- Several sizes of a face at once: a mod that wants crisp text at two scales registers two faces, within its budget.
- A display event for a mod that does not poll: the values at connect are there for one that never polls.

## 4. EldenNet's answers (2026-11-20)

1. EldenNet's v0.2 needs it, before S11.4.c: the subtitles, the chat box and the call-out box are sized by NPC §10.7's
   scale, and their faces go into GraceUI's atlas in the registration window. v0.1 does not: its banner is a notice
   and its F11 view stays at 1.0.
2. Both: the values at connect, and the event once after warm-up and on every change; with `content_scale`, EldenNet
   drops its constant 1.25.
3. What happens to a mod's faces after a change of the scale after load: §2's last part (scaled faces; drawn scaled
   until the next session).

## 5. How it is tested

In the harness under Wine, on both toolchains ("display"): a connect before the swap chain is bound says `ui.scale` and
its default and no size, and a 1.0-sized answer is written no further than 1.0 reached; a scaled face of 16 px is baked
at 11 px at 1280 × 720 and a plain one at 16; nothing waits before warm-up, one event after it and none after that; a
poll of 1.1's size gets none; a new `ui.scale` from the menu and a resize each give one event with the new values and
the faces' scale unchanged; a mod that connects later has the size in its answer and the event at its first poll.
