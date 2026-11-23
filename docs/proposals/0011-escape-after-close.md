# Proposal 0011: One Escape for a mod after its box has closed

**Status: proposal, 2026-11-23, put to the owner as G39 (EldenNet's roadmap S11.4.d, its WHY§437). Declined the same
day, answer (c): Esc and the pad act on a chat box only while it is open, and are the game's again once it closes, so
nothing here is built and EldenNet drops its second Esc (G39).** A bare § is a
section of [DESIGN.md](../DESIGN.md); NPC is EldenNet's `docs/design/npcs-and-dialogue.md`.

## 1. Why

EldenNet's rule for ending a conversation (NPC §5.7, from its DESIGN §8.6) has two steps when a
reply is still pacing out, page by page:

- **The first Esc** closes the chat box, and the reply goes on pacing in the subtitles, then the conversation ends.
- **A second Esc** ends it at once and drops the text not yet shown.

The first press reaches EldenNet: its box holds input (D77), and a holder's keys come to its mod (D176). The second
cannot. With the box closed nothing holds input, so GraceUI gives the mod no keys, and Escape can't be a hotkey (D155
refuses it, because the game's own menu is on it). The press goes to the game, which opens its menu while the reply
goes on pacing over it. EldenNet built the rule and waits on GraceUI for a way in (WHY§437).

Nothing in ABI 1.5 serves. Holding input again would freeze the player: a holder's input is masked from the game, so
the character couldn't move while the reply paces. A hotkey on Escape would take Escape from the game for as long as
the mod runs.

## 2. What the player would see

Only this: **while a reply is still pacing after its box has closed, Esc stops the reply instead of opening the game's
menu.** That Esc is the only one taken; the next opens the game's menu as always. Once the reply has finished, nothing
is taken. EldenNet shows the hint ("Esc: stop") in its own subtitles; GraceUI draws nothing new.

## 3. The proposal: an armed Escape

A mod may **arm** Escape for a while. While Escape is armed and nothing holds input, the **next press of Escape** goes to
that mod as an event and never reaches the game. Then the arming ends.

- **One press, then disarmed.** The press disarms it. The game never loses more than one Esc, and only while the mod
  says it waits for one.
- **Bounded.** An arming lasts at most `duration_ms`, 60 s at the most (EldenNet's longest reply paces out well within
  that), and the mod may disarm it sooner (it does when the reply ends). It may arm again while the conditions below
  hold, for its next reply.
- **One mod at a time.** Like `request_input` (D77, invariant 17), it is answered at once and honestly:
  - `granted`;
  - `refused`, naming the mod whose Escape is armed;
  - `not_now`, saying why: input is held (the menu, a panel, a modal), danger, loading, no render yet, or a game menu
    open.

  Nothing queues and nothing deposes.
- **Anything that matters more disarms it, and the mod is told why** (one event, as `INPUT_ENDED` tells a holder):
  - any holder taking input (the menu's key, another mod's panel, this mod's own box opening again);
  - danger or loading from any world-state provider (invariant 9);
  - the game window losing focus;
  - no overlay frame for 2 s;
  - a fault;
  - the time running out.
- **Escape only, the keyboard only.** No other key is affected. The press is masked from the game until it is released,
  as a holder's closing press is (invariant 8). The pad's B is not armed: it dodges in the game, and a dodge must never
  be eaten. A pad player stops a reply as today, by letting it end or by reopening the box (§4 (2)).
- **Escape held 1 s** stays the player's exit wherever a holder holds input (D77). It has nothing to end here: the one
  press taken is a single press, and the next is the game's.

**The ABI**, appended as ABI 1.6 with `has_cap("escape")`:

- `arm_escape(consumer, duration_ms, GraceUiInputAnswer* answer)`: arming with `duration_ms` from 1 to 60 000,
  disarming with 0. It answers in the shape `request_input` does.
- Two new kinds for `poll` (and the wait handle, D212):
  - `GRACEUI_EVENT_ESCAPE`: the armed press;
  - `GRACEUI_EVENT_ESCAPE_ENDED`: disarmed with a `why` of the `GRACEUI_CLOSED_*` set, plus `EXPIRED`.

  Like the holder's events, they reach only an event struct of 1.1's size or larger. The time of the press is the mod's
  to take when it polls.
- No change of the protocol or the pages. The Hotkeys page lists nothing for it: it is not a binding.

## 4. Questions for the owner (G39)

1. **May a mod take one Escape press while nothing holds input?**
   - (a) **Yes, armed by the mod, one press, at most 60 s, Escape only, as §3 has it.** *Recommended.* It is EldenNet's
     rule as designed, it costs the player at most one Esc while a reply paces, and it is honest in the way D77 is.
   - (b) Only within a short fixed time, 5 s, after the mod's own box closes, with no arming. Simpler, but a long reply
     outlives it, and the press then opens the game's menu again over the reply.
   - (c) No. The rule changes on EldenNet's side: a reply stops only while its box is open. NPC §5.7 then needs a
     change of the owner's, and the first Esc either waits for the reply or drops it.
2. **The pad:**
   - (a) **The keyboard only for now.** *Recommended.* B dodges, and the owner tests with keyboard and mouse only
     (D205).
   - (b) The pad's B too, while armed. Pad players could then stop a reply, at the risk of a lost dodge; their testers
     would check it on S4's second round.

## 5. What it would take

If the answers are (1) (a) and (2) (a):

- **grace-input:** the arming (one owner, its generation, its deadline), checked against the holder and the world
  state each frame. The press is taken in the window procedure's hook and masked from DirectInput until it is released,
  with the held-key masking that a holder's close already uses.
- **grace-abi:** ABI 1.6, its two event kinds, and `has_cap`.
- **Tests:**
  - unit tests of the arming's rules (each answer, each disarming, one press);
  - in the harness, an "escape" scenario: a mod arms, a press reaches it and not the game, a second press reaches the
    game; disarmed by the menu, by danger, by focus and by time.
- **EldenNet:** it arms when the box closes while its reply paces, disarms when the reply ends, and ends the
  conversation on `GRACEUI_EVENT_ESCAPE` (its S11.4.d); its subtitles show the hint.

Built and tested under Wine like the rest. In the real game it waits, like every hook of GraceUI's, for the spikes S0-E
and S4 (EldenNet's S10.1.g and S11.1.a).
