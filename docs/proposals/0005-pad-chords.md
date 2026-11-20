# Proposal 0005: Pad chords for hotkeys

**Status: proposal, 2026-11-20, put to the owner and to EldenNet's session for S4's second round (its NPC §5.6, O11),
not built.** A bare § is a section of [DESIGN.md](../DESIGN.md). Figures marked EST are estimates for S4 to confirm.

## 1. Why

EldenNet's chat box opens on a pad by a chord: "hold View + D-pad Up for 0.4 s" (its NPC §5.6, O11), with the View
press kept from the game while the chord resolves and given back as a tap if it fails. Only GraceUI may do that: it
alone hooks XInput (invariant 16), and a mod that wants a pad gesture must ask GraceUI for it. ABI 1 already has the
field: `GraceUiHotkeyDesc.pad_chord`, "the default pad chord, XInput's button bits", which `register_hotkey` answers
`not_yet` for anything but 0 since GraceUI 0.1 (D155). D33 promised a chord "if S4 finds one free". EldenNet's roadmap
waits on a D-entry of GraceUI's for S4's second round (its S11.4).

This proposal says what a chord is, how the game's view of the pad changes while one resolves, and how a mod asks for
one. It changes nothing in GraceUI 0.2.3 and nothing in ABI 1.2: when built, it is ABI 1.3, an addition.

## 2. What a chord is

- **Two buttons, in order.** An *anchor*, pressed first, and a *key*, pressed while the anchor is held. EldenNet's is
  anchor D-pad Up, key View. Any two of XInput's fourteen buttons, different; no triggers or sticks (they are
  analogue, and the game reads them as such). A chord pressed in the other order is not the chord: the key reaches the
  game as it always does.
- **The anchor always reaches the game** when it is pressed: one spell switch, as NPC §5.6 says. A chord costs the
  player nothing they did not press.
- **The key is held back** only when it is pressed while the anchor has been held for at least **150 ms** (EST, NPC
  §5.6). Pressed sooner, it is not a chord: it reaches the game at once. So a quick Up-then-View, or both at once, plays
  as the game expects.
- **The chord completes** when both are held for **0.4 s** (EST) from the key's press. Then:
  - the hotkey fires: one `GRACEUI_EVENT_HOTKEY` for the mod, as a key's does (D155), on its own thread through `poll`;
  - the key is never delivered to the game, and from that moment both buttons are hidden from the game until each is
    released (the pad ledger's rule for a held control, invariant 8), so that the game sees neither a late View nor a
    long-held Up. If the mod then asks for input and is granted it, the holder's own masking takes over as today.
- **The chord fails** when either button is released before 0.4 s. The held-back key is then **replayed as a tap**:
  the game sees it pressed for at least two of its own reads and at least 50 ms (EST), then released, with the packet
  number changed at each step so that the game reads both (as at a grant and a close, NPC §5.5). The tap comes late by
  at most 0.4 s, and comes once.

## 3. When chords stand aside

A chord neither holds a key back nor fires:

- while any holder has input (the menu, a panel, a modal): the holder gets the pad's buttons itself (D176), as hotkeys
  stand aside today (§5.2);
- in the notice-only mode (D88), and while GraceUI's input is off;
- **while a request for input would be answered `not_now`** (a world-state provider's danger, a game menu open, a
  loading screen, the menu's faces not ready; D77). *Recommended*, and the one question of §6: a chord exists to open
  something, and what it would open cannot open then; holding View back in a fight would only cost the player. A mod
  that wants a gesture in a fight uses a key.

## 4. How a mod asks for one

- `register_hotkey` takes `pad_chord` as **the key's bit in the low 16 bits and the anchor's in the high 16**, each
  exactly one of XInput's button bits (EldenNet's: `XINPUT_GAMEPAD_BACK | (XINPUT_GAMEPAD_DPAD_UP << 16)`). Anything
  else is `refused`, with one log line. A bit mask alone cannot say which button comes first, and ABI 1's field is a
  `uint32_t` that GraceUI has refused to read until now, so giving its halves meaning breaks no mod.
- One action may have a default key and a default chord; either may be 0. A chord that another mod's action already
  has is registered unbound, as a taken key is (D77), and named on the Hotkeys page.
- **Version:** ABI 1.3, `has_cap("pad-chords")`, in the GraceUI that builds it. Older GraceUI answers `not_yet` for a
  chord, so a mod checks the cap first, or registers again without the chord on `not_yet`.
- **The player's binding** of chords comes with GraceUI's Hotkeys page (0.6): until then each action has its default.
- GraceUI's own menu keeps no default chord (D33); its Hotkeys page may offer one later, under the same rules.

## 5. How it is tested

- **The pad ledger, pure, in `grace-input`'s unit tests**, by timelines of pad states and times: the anchor passes; a
  key at 100 ms passes; a key at 200 ms is held back; complete at 0.4 s fires once, hides both until released; a
  release of either before 0.4 s replays one tap of two reads and 50 ms, packet numbers changing; a holder, the
  notice-only mode and `not_now` each leave the pad untouched; two pads at once, each its own; a chord registered twice
  is unbound the second time.
- **In the harness under Wine**, on both toolchains, through `GraceUI_Test_Pad` (Wine has no pad): what the game's
  `XInputGetState` returns frame by frame, and the mod's hotkey event; a world-state danger standing the chord aside.
- **In S4's second round**, in the game: that View + D-pad Up is free (no game function on the chord itself), that the
  late View tap opens the map as a press would, and that a spell switch from the anchor costs nothing. S4 confirms the
  three estimates (150 ms, 0.4 s, the tap's length) or changes them.

## 6. For the owner and EldenNet's session

1. **Stand aside when input would be refused** (§3's last case): *recommended*, so that a chord never delays the
   game's View in a fight or a game menu. The alternative holds View back whenever the anchor is held, and fires the
   hotkey even when the mod cannot open anything.
2. **The encoding** (§4): anchor in the high half, key in the low. The alternative is a new field after `name`
   (ABI 1's structs may grow), which costs a struct change for one bit of order.
3. **For EldenNet's session:** does §2 match NPC §5.6 as you mean it (the anchor first; a key pressed within 150 ms
   not a chord; both hidden after completion until released)? And is one hotkey event enough, with EldenNet asking
   for input on it as for F11's view?
