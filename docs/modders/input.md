# Input and focus

In game, capture and masking are GraceUI's, for every input holder: its menu, a mod's panel that takes input, or a
modal, one at a time under one rule (DESIGN §5, D77, invariant 17). A mod hooks none of the functions GraceUI hooks for
input (the game's window procedure, DirectInput's `GetDeviceState`, XInput's `XInputGetState`; invariant 16): it asks
GraceUI for input, and registers its keys as GraceUI's hotkeys. The browser view is an ordinary web page, with no
capture (§5.9). Minors are named as ABI 1.1 or protocol 1.4.

## The input holder

A mod asks for input with `request_input(consumer, &request, &answer)` (cap `input`).

| `GraceUiInputRequest` | Meaning |
|---|---|
| `kind` | `GRACEUI_HOLDER_PANEL` or `GRACEUI_HOLDER_MODAL` |
| `wants` | `GRACEUI_WANT_KEYBOARD`, `_MOUSE` and `_PAD`, any together |
| `panel` | The panel whose view takes the input: the mod's own, shown, made with `GRACEUI_PANEL_TAKES_INPUT`; on the modal layer for a modal, on another for a panel |
| `flags` | `GRACEUI_INPUT_EVENTS` (ABI 1.1), or 0; an unknown flag is refused |

The slot answers `GRACEUI_REFUSED` for a request that names no panel of the mod's that may take input, or a kind that
does not suit its layer. Otherwise it answers `GRACEUI_OK`, with the answer given at once:

| `answer` | Meaning |
|---|---|
| `GRACEUI_INPUT_GRANTED` | The mod holds input |
| `GRACEUI_INPUT_REFUSED` | Another holds input: `holder_kind` is its `GRACEUI_HOLDER_*`, and `holder` its mod's id, NUL-terminated (`graceui` for the menu) |
| `GRACEUI_INPUT_NOT_NOW` | Not at this moment: `why` is `GRACEUI_NOT_NOW_NOT_READY` (the overlay cannot show the holder's view yet), `_DANGER`, `_LOADING` or `_GAME_MENU` |

There is one holder in the process at a time, and the first holder wins (D77). Nothing deposes the holder, and nothing
queues behind it: a request refused or not now is not kept. Telling a refused mod when input is free is left for when a
mod needs it (D176).

`release_input(consumer)` gives input back if the mod holds it, and answers `GRACEUI_OK` either way. Hiding the panel
that holds input ends the holder too.

While a mod holds input, the menu key shows "‹Mod› has the controls" and opens nothing (§5.1), and hotkeys stand aside
(D77).

## How a holder ends

GraceUI's one close routine ends every holder, whatever the exit: it releases capture, masks every key and button still
held until it is released, and tells the holder why (D77, invariant 7). A mod with the holder's events hears it as
`GRACEUI_EVENT_INPUT_ENDED`, its `why` one of:

| `why` | The exit |
|---|---|
| `GRACEUI_CLOSED_RELEASED` | `release_input`, the holding panel hidden, or its mod gone |
| `GRACEUI_CLOSED_MENU_KEY` | The menu key |
| `GRACEUI_CLOSED_DANGER` | A world-state provider's danger (invariant 9) |
| `GRACEUI_CLOSED_LOADING` | A loading screen |
| `GRACEUI_CLOSED_FOCUS_LOST` | The game's window lost focus, as at Alt+Tab: every holder but the menu ends (D233) |
| `GRACEUI_CLOSED_NO_RENDER` | No overlay frame for 2 s |
| `GRACEUI_CLOSED_FAULT` | A fault |
| `GRACEUI_CLOSED_PLAYER` | The player's exit |

### The player's exit

A second press of the menu key within 2 s of "‹Mod› has the controls", or Escape held 1 s (both EST), ends any holder,
whether or not its view has reported ready, and the holder is told `GRACEUI_CLOSED_PLAYER` (D77). No holder can capture
it (invariant 17). A single Escape reaches the holder as a key; GraceUI keeps the menu key; Alt+F4 reaches the game
(D176).

### The closing press and the opening press

The press that closes a holder never reaches the game, whoever held input: Escape would open the game's menu, and B
would dodge. The press that opened the menu does nothing in it until it is released (invariant 8, §5.2). A holder's
closing pad press is masked from the game until it is released (D176).

## The holder's events

With `GRACEUI_INPUT_EVENTS` in its request (ABI 1.1, cap `holder-events`, D176), the holder's input comes to its mod
through `poll`, in the order it happened, only while it holds input, and never to another mod. Without the flag, as for
every ABI 1.0 mod, the input is taken from the game and the mod hears nothing of it. An event struct smaller than ABI
1.1's never gets these kinds.

| Kind | Fields |
|---|---|
| `GRACEUI_EVENT_KEY` | A key's press, auto-repeat or release: `key` (its virtual key), `modifiers` (`GRACEUI_MOD_*` held with it), `pressed`, `repeat` |
| `GRACEUI_EVENT_CHARACTER` | One character typed, `code_point`: a surrogate pair joined, an IME's result |
| `GRACEUI_EVENT_POINTER` | With `GRACEUI_WANT_MOUSE`: `x` and `y` in the panel's own pixels (moves coalesced to the newest); with `button_event` 1, a button's press or release (`button`, `pressed`); the wheel (`wheel_x`, `wheel_y`, in notches) |
| `GRACEUI_EVENT_PAD` | With `GRACEUI_WANT_PAD`: a pad button's press or release (`button`, one XInput button's mask; `pressed`; `pad`, 0 to 3). No sticks or triggers |
| `GRACEUI_EVENT_INPUT_ENDED` | The holder ended: `why`. Once, and last |

Each event carries `dropped`, the holder's events lost before it because the mod polled too slowly: 256 wait at most,
and the newest are dropped, never reordered. When the holder ends, its events not yet polled are discarded before
`GRACEUI_EVENT_INPUT_ENDED` (D176).

What is typed goes from the window procedure to the holder's mod alone, and GraceUI never logs or stores it (D176,
invariant 5). The clipboard is not GraceUI's: its chords come as keys, and GraceUI never reads it.

**The caret** (ABI 1.1). `set_caret(consumer, &caret)` gives the text caret of the mod's field, in its panel's own
pixels from the top-left corner (`x`, `y`, `height`), so that GraceUI places the IME's composition and candidate windows
at it (§5.6). A height of 0 clears it. Only the holder's caret counts: one set while the mod does not hold input is
answered `GRACEUI_OK` and ignored, and the caret ends with its holder, so a mod sets it while it holds, each frame its
field has the caret.

## Hotkeys

Hotkeys belong to the framework (D77, D155). A mod registers a named action with a default key and modifiers, a pad
chord, or both, and the player binds every mod's keys on GraceUI's Hotkeys page. `register_hotkey(consumer, &hotkey,
&hotkey_id)` takes:

| `GraceUiHotkeyDesc` | Meaning |
|---|---|
| `key` | The default key, a Windows virtual key (not Escape, a modifier or a mouse button); 0 for none |
| `modifiers` | `GRACEUI_MOD_CTRL`, `_SHIFT` and `_ALT`; Alt with F4 is refused |
| `pad_chord` | The default pad chord, `GRACEUI_PAD_CHORD(anchor, key)`; 0 for none (ABI 1.3) |
| `action` | The action's id within the mod: 1 to 32 of a-z, 0-9 and `-`, not the mod's already |
| `name` | What the Hotkeys page calls it: 1 to 64 bytes of UTF-8 |

| Answer | Meaning |
|---|---|
| `GRACEUI_OK` | Registered, with its id, and bound |
| `GRACEUI_UNBOUND` | Registered, with its id, but its default is taken (by another hotkey, or the menu key) or it has none: it fires nothing until the player binds it, and GraceUI's log names it (D164). With a key and a chord, either taken; the other still fires it |
| `GRACEUI_REFUSED` | A reserved key, an unknown modifier, Alt+F4, an action that is not an id or already the mod's, a name that is not 1 to 64 bytes of UTF-8, a key with no place on the keyboard, or a pad chord that is not two different buttons |
| `GRACEUI_FULL` | 8 hotkeys a mod, or 64 in the session |
| `GRACEUI_NOT_YET` | A pad chord from a mod built before ABI 1.3 |

A mod that gets `GRACEUI_UNBOUND` may keep a fallback of its own (D164).

**A press.** GraceUI watches for the hotkey in the game's window procedure. A fresh press of its key with exactly its
modifiers, while no holder has input, is one press, which the mod takes through `poll` on its own thread as
`GRACEUI_EVENT_HOTKEY`, with `count`, its presses since its last event. The press, its repeats, characters and release
never reach the game; a key bound with no modifier is never the game's while it is bound, through DirectInput as well.
A press the game saw begin reaches the game to its release (D155). Hotkeys stand aside while any holder has input or a
text field has focus (§5.2); a holder that takes the keyboard gets the key as any other (D155).

`hello-debug-view` (`examples/hello-debug-view/hello_debug_view.cpp`) registers F11 through `graceui_sys.hpp` and
toggles its view on each press:

<!-- snippet: examples/hello-debug-view/hello_debug_view.cpp -->

    while ((status = api.poll(me, event)) == GRACEUI_OK && event.kind != GRACEUI_EVENT_NONE) {
      if (event.kind == GRACEUI_EVENT_HOTKEY && event.hotkey_id == hotkey) {
        toggles += event.count;
        if ((event.count & 1u) != 0) {
          open = !open;
          if (open) {
            build(list, ++frames, toggles);
            api.submit(me, panel, list.view());  // its first list before it shows, so it never shows empty
            api.show_panel(me, panel);
            banner(api, me, HELLO_DEBUG_BANNER_OPEN);
          } else {
            api.hide_panel(me, panel);
            banner(api, me, HELLO_DEBUG_BANNER_CLOSED);
          }
        }
      }
    }

### The Hotkeys page

GraceUI › Hotkeys lists the menu key first, then every hotkey registered, with its mod, its name, its key ("Ctrl+K", or
"No key"), its pad chord ("D-pad Up, then View", or "No pad chord") and its state: "Bound"; "Unbound: no default";
"Unbound: ‹mod› took F9 first"; "Paused by its mod for now" (D206, D280). There the player changes any mod's key and pad
chord, or resets it (D208):

- A key or chord another bound hotkey has is asked first, and moved with the player's yes, the other hotkey left with
  none.
- The menu's key is never taken from it. Escape, Alt+F4, the modifiers and the Windows keys are refused.
- The player's bindings are kept between sessions, by mod and action, and applied at each registration.

So a mod needs no hotkey page of its own. The bindings live in game only: a host outside the game holds no hotkey table
(D211).

### describe_hotkey and HOTKEY_CHANGED

A mod that names its key in its own words ("Press T to talk") reads it with `describe_hotkey(consumer, hotkey_id,
&binding)` (ABI 1.4, cap `hotkey-bindings`, D210):

| `GraceUiHotkeyBinding` | Meaning |
|---|---|
| `bound` | `GRACEUI_HOTKEY_KEY_BOUND` (its key fires it now), `GRACEUI_HOTKEY_CHORD_BOUND` (its pad chord fires it now), both or neither |
| `key`, `modifiers` | Its key, a Windows virtual key, and its `GRACEUI_MOD_*`; 0 for none |
| `pad_chord` | `GRACEUI_PAD_CHORD(anchor, key)`; 0 for none |
| `key_text` | The key as the Hotkeys page writes it ("Ctrl+K"), NUL-terminated; empty for none |
| `chord_text` | The chord likewise ("D-pad Up, then View"); empty for none |

When the player changes a hotkey's key or pad chord, a mod connected with ABI minor 4 or more gets
`GRACEUI_EVENT_HOTKEY_CHANGED` with its `hotkey_id`, after its presses, and reads the binding again.

### What pressed it

`GraceUiEvent.hotkey_by` (ABI 1.7, cap `hotkey-by`, D243) says what made the presses `count` counts:
`GRACEUI_HOTKEY_BY_KEY`, `GRACEUI_HOTKEY_BY_CHORD`, or both when both did since the mod last polled. A GraceUI without
the cap writes 0 there.

### Pausing a hotkey

`set_hotkey_enabled(consumer, hotkey_id, enabled)` (ABI 1.11, cap `hotkey-pause`, D280) pauses (`enabled` 0) or resumes
one of the mod's own hotkeys, such as while the game's own menu is open. GraceUI never guesses that moment: the mod
knows it.

- Paused, the hotkey neither fires nor keeps its key or pad chord from the game.
- The player's binding stays, held against other hotkeys, and the Hotkeys page says it is paused.
- A press under way stays whole on the side that saw it begin: one the hotkey took stays hidden from the game until
  released, and one the game saw begin reaches the game until released and fires nothing.
- It is not the player's change: no `GRACEUI_EVENT_HOTKEY_CHANGED`. A reconnect starts every hotkey live.
- It never waits on the game, so any of the mod's threads may call it. It answers `GRACEUI_OK`, also when the hotkey
  was so already, or `GRACEUI_REFUSED` for an id not the mod's.

## Pad chords

A hotkey's pad chord (ABI 1.3, cap `pad-chords`, D204) is two of XInput's buttons in order, an anchor and a key, given
as `GRACEUI_PAD_CHORD(anchor, key)`: each exactly one `XINPUT_GAMEPAD_*` button, not a trigger or a stick, the two
different.

- The anchor always reaches the game.
- A key pressed while the anchor has been held for 150 ms is held back from the game.
- Both held for 0.4 s from the key's press fire the hotkey; the key never reaches the game, and both are hidden from it
  until each is released.
- Either released sooner replays the key to the game as one tap.
- Chords stand aside while any holder has input and whenever `request_input` would answer not now (danger, a game menu,
  loading, not ready): then the key reaches the game at once.

The times are estimates (EST), to be confirmed on a pad. A chord another hotkey has bound registers unbound, as a taken
key does. One action may have a key and a chord, each bound on its own, and either fires it. A mod built before ABI 1.3,
and any GraceUI before 0.2.14, answers a chord `GRACEUI_NOT_YET`.

A space's row action may also take a key or a pad chord from the player before it is sent (protocol 1.4, D209): see
[Components](components.md).

## Danger

A mod that knows the game's state may register as a world-state provider with `set_world_state` (D78;
[The C ABI](abi.md#world-state)). Every provider's claims are combined:

- Any provider's danger closes every view at once and cancels any open confirmation (invariant 9); any provider's danger
  or loading ends the input holder.
- A view opens only when every provider allows it, or at the title screen.
- `request_input` answers not now with its reason: danger, loading, a game menu open, or the overlay not ready.

Without a provider there is no danger close: GraceUI reads no game state, views open at any time, and capture still
masks game input while a holder has it (D78).

## Steam's floating keyboard

The mod that holds input with the keyboard, whose field begins editing from the pad (its pad events say so), may ask
GraceUI to open Steam's floating keyboard beside the field with `pad_keyboard(consumer, &keyboard)` (ABI 1.10, cap
`pad-keyboard`, D276). The mod then makes no call into Steam of its own: GraceUI opens it through the
`steam_api64.dll` the game has already loaded, never loads it, and calls no game code.

| `GraceUiPadKeyboard` | Meaning |
|---|---|
| `mode` | `GRACEUI_PAD_KEYBOARD_LINE` (one line: Enter ends it), `_LINES` (several lines), `_EMAIL` (an e-mail address) or `_NUMERIC` (digits) |
| `panel` | The mod's panel that holds the field |
| `x`, `y`, `width`, `height` | The field in the panel, width and height more than 0 |

- The keyboard types into the game's window as keys do, so the mod's field takes its characters as the holder's other
  key events.
- A null keyboard closes the one GraceUI opened for the mod; GraceUI closes it itself when the mod's holder ends,
  whatever ends it (invariant 7), and at the mod's disconnect.
- Steam shows it on a Steam Deck or in Big Picture only. `GRACEUI_UNAVAILABLE` means no keyboard to show here (Steam's
  library or its exports missing, Steam not started, or Steam declining), and GraceUI's log says which.
- `GRACEUI_REFUSED` answers a mod that does not hold input with the keyboard, among the other checks in
  [The C ABI](abi.md#pad_keyboard).
- It may take a moment, as Steam's call does: call it from the mod's own thread.
