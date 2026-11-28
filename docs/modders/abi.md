# The C ABI

Every mod reaches GraceUI through one C ABI, `src/grace-abi/include/graceui.h`, which is both the ABI's source and its
public header (D73, D109; invariant 15). ABI 1 is a preview until GraceUI 1.1 opens the framework to other mods (G18):
until then a mod pins GraceUI by its release. The minors named on this page, such as (1.4), are ABI 1's; the GraceUI
release that brought each is in [Features by version](features.md).

## The rules

- Plain C, x64 only, default packing. No C++ vtables, STL, exceptions or heap ownership cross it (D73).
- Every struct a mod passes starts with its size: set it to `sizeof`. GraceUI reads the smaller of the two sizes, takes
  the rest as zero, and refuses a struct smaller than the header's first form of it. Of a struct it fills (an answer, an
  event), it writes the smaller of the two sizes. So an older mod's smaller struct is read as it was.
- Strings are UTF-8 with a length (no NUL needed), and are copied before the call returns.
- Nothing is freed across the boundary, and no call throws: every slot answers a status.

## Finding GraceUI

`graceui.dll` exports two functions (D70):

| Export | What it does |
|---|---|
| `GraceUI_Version()` | GraceUI's version, packed. Callable from any thread at any time after the DLL is loaded; it starts nothing |
| `GraceUI_RequestApi(major)` | The table of that ABI major (a `const GraceUiApi1*` for 1), or null. The first call in the process takes GraceUI's single-instance mutex and, in the copy that takes it, runs the anti-cheat check and starts the host (D71, D72, D133). A second copy loaded from another folder starts nothing and answers null to every call (invariant 18). Never call it from `DllMain` |

A version packs major (10 bits), minor (10 bits) and patch (12 bits): `GRACEUI_VERSION_PACK(major, minor, patch)`, and
`GRACEUI_VERSION_MAJOR`, `_MINOR` and `_PATCH` to read it.

A mod finds GraceUI with `graceui_sys.h` (`src/graceui-sys/include/`), header-only C compiled into the mod, or with its
thin C++ wrapper `graceui_sys.hpp` (D70, D136). Call it from a thread of the mod's own, never from `DllMain`: GraceUI's
first request takes a mutex and starts a thread, which must not happen under the loader's lock.
`graceui_sys_find(flags, timeout_ms, &found)` looks three ways, in this order:

1. the module named `graceui.dll`;
2. every module loaded in the process that exports `GraceUI_RequestApi`, the first whose table answers, so a renamed
   copy, or a second copy that idles, is no obstacle;
3. with `GRACEUI_SYS_LOAD`, GraceUI's canonical copy, `GraceUI\graceui.dll` in the folder of the game's executable,
   which it loads.

It looks again every 250 ms for at most `timeout_ms` (`GRACEUI_SYS_TIMEOUT_MS`, 30 s, EST). With
`GRACEUI_SYS_STOP_IF_ABSENT` it stops at the first look that finds GraceUI not installed (D164). `GraceUiSysFound` says
how it was found (`GRACEUI_SYS_BY_NAME`, `_BY_SCAN` or `_BY_PATH`), its version, the looks it took, the module, and
`installed`. Null means no GraceUI that serves ABI 1: the mod's UI is off, never an error dialog.

`hello-notice` (`examples/hello-notice/hello_notice.c`) starts its thread in `DllMain`:

    BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID unused) {
      (void)unused;
      if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread = NULL;
        DisableThreadLibraryCalls(module);
        thread = CreateThread(NULL, 0, &hello_worker, NULL, 0, NULL); /* it runs once the loader is done */
        if (thread != NULL) {
          CloseHandle(thread);
        }
      }
      return TRUE;
    }

and on that thread finds GraceUI and connects:

    memset(&found, 0, sizeof found);
    found.size = sizeof found;
    api = graceui_sys_find(GRACEUI_SYS_LOAD, GRACEUI_SYS_TIMEOUT_MS, &found);
    if (api == NULL || !GRACEUI_API1_COVERS(api, connect) || !GRACEUI_API1_COVERS(api, notice)) {
      return 0; /* no GraceUI for ABI 1: this mod shows nothing */
    }

    memset(&request, 0, sizeof request);
    request.size = sizeof request;
    request.abi_minor = GRACEUI_ABI1_MINOR;
    request.version = GRACEUI_VERSION_PACK(1, 0, 0);
    request.floor = GRACEUI_VERSION_PACK(0, 1, 0);
    request.id = HELLO_ID;
    request.id_length = sizeof HELLO_ID - 1;
    request.name = HELLO_NAME;
    request.name_length = sizeof HELLO_NAME - 1;
    request.caps = GRACEUI_CAP_NOTICES;
    request.caps_length = sizeof GRACEUI_CAP_NOTICES - 1;
    memset(&answer, 0, sizeof answer);
    answer.size = sizeof answer;
    status = api->connect(&request, &answer);
    if (status != GRACEUI_OK && !(status == GRACEUI_INACTIVE && answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT)) {
      return 0; /* too old, refused, or nothing can be drawn this session */
    }

## The table

`GraceUiApi1` starts with its `size`, its `abi_minor`, GraceUI's version and a reserved word, then its slots in D73's
order. A later minor only appends; a new major is a new table from the same export.

- **Covers.** A slot exists only if it ends within the table's `size`: call one only if
  `GRACEUI_API1_COVERS(api, slot)` holds. The C++ wrapper's calls check it themselves and answer `GRACEUI_NOT_YET` where
  the table is shorter.
- **The compatibility rule** (D74), the only one: a mod works with every GraceUI of the ABI major it was built for whose
  version is at least the floor it names in `connect`. Each major is served for at least two minor releases and 12
  months after the next (EST).
- **Every slot after `connect`** takes the mod's handle and answers `GRACEUI_NOT_CONNECTED` for a handle that names no
  connected mod, then `GRACEUI_INACTIVE` while GraceUI is inactive (`notice` excepted in the notice-only mode), then
  `GRACEUI_NOT_YET` where this GraceUI does not provide it. A handle from before a disconnect names nobody (D131).
- **Inside a callback.** From inside an `on_present` callback every slot but `has_cap` answers `GRACEUI_REFUSED`
  (D259).

## Caps

`has_cap(name, length)` answers 1 if this GraceUI provides the feature named, else 0, whatever the session's state. A
mod relies on a feature only if `has_cap` names it, never on GraceUI's version, and goes without it otherwise.

| Cap | Names | Since |
|---|---|---|
| `notices` | `notice` | 1.0 |
| `panels` | `create_panel`, `submit`, `show_panel`, `hide_panel` | 1.0 |
| `textures` | `register_texture` | 1.0 |
| `faces` | `register_face`, `glyph_table` | 1.0 |
| `input` | `request_input`, `release_input` | 1.0 |
| `world-state` | `set_world_state` | 1.0 |
| `hotkeys` | `register_hotkey`, and poll's hotkey events (D155) | 1.0 |
| `pages` | `register_pages`, `publish`, `answer`, and poll's protocol events (D169) | 1.0, GraceUI 0.2 |
| `holder-events` | `GRACEUI_INPUT_EVENTS`, poll's holder events, `set_caret` (D176) | 1.1 |
| `display` | `connect`'s display fields, `GRACEUI_EVENT_DISPLAY`, `GRACEUI_FACE_SCALED` (D180) | 1.2 |
| `pad-chords` | `register_hotkey`'s `pad_chord` (D204) | 1.3 |
| `hotkey-bindings` | `describe_hotkey`, `GRACEUI_EVENT_HOTKEY_CHANGED` (D210) | 1.4 |
| `wake` | `wake_event` (D212) | 1.5 |
| `hotkey-by` | `GraceUiEvent.hotkey_by` (D243) | 1.7 |
| `on-present` | `set_on_present` (D259) | 1.8 |
| `plain-text` | `connect`'s and `GRACEUI_EVENT_DISPLAY`'s `plain_text` (D268) | 1.9 |
| `pad-keyboard` | `pad_keyboard` (D276) | 1.10 |
| `hotkey-pause` | `set_hotkey_enabled` (D280) | 1.11 |

Minor 1.6, a file command's file in poll's event (D226), has no cap of its own.

## connect

`connect(&request, &answer)` connects a mod. It answers, never silence.

| `GraceUiConnectRequest` | Meaning |
|---|---|
| `size` | `sizeof(GraceUiConnectRequest)` |
| `abi_minor` | `GRACEUI_ABI1_MINOR` as the mod was built |
| `version` | The mod's own version, `GRACEUI_VERSION_PACK` |
| `floor` | The oldest GraceUI it works with, `GRACEUI_VERSION_PACK`; 0 for any |
| `id`, `id_length` | 3 to 32 of a-z, 0-9 and `-`, unique among the mods connected |
| `name`, `name_length` | Its display name, which players see: 1 to 64 bytes of UTF-8 |
| `caps`, `caps_length` | The GraceUI features it uses: cap names separated by spaces, at most 256 bytes, or empty |

| `GraceUiConnectAnswer` | Meaning |
|---|---|
| `size` | Set by the caller to `sizeof(GraceUiConnectAnswer)` |
| `status` | What `connect` answered |
| `reason` | With `GRACEUI_INACTIVE` a `GRACEUI_INACTIVE_*`, with `GRACEUI_REFUSED` a `GRACEUI_REFUSED_*` |
| `graceui_version` | This GraceUI's version |
| `floor` | With `GRACEUI_TOO_OLD`: the version the mod needs |
| `abi_minor` | The ABI minor this GraceUI serves |
| `consumer` | With `GRACEUI_OK` or `GRACEUI_INACTIVE`: the mod's handle; else 0 |
| `ui_scale`, `ui_scale_default` | GraceUI's Menu size and its default: a mod's content scale is (H / 1080) × `ui_scale` / `ui_scale_default` (1.2) |
| `display_width`, `display_height` | The back buffer's size; 0 until the game's swap chain is bound, often so at connect (1.2) |
| `plain_text` | 1 if the player wants prose and titles in a plain sans face, else 0 (1.9). A GraceUI without the cap leaves it as the mod set it: zero it first |

| Status | Meaning |
|---|---|
| `GRACEUI_OK` | Connected; the handle is in `consumer` |
| `GRACEUI_TOO_OLD` | This GraceUI is older than the mod's floor. `graceui_version` and `floor` name both, and GraceUI shows a notice naming the mod and both versions, and logs one line |
| `GRACEUI_DUPLICATE_ID` | A connected mod has this id already |
| `GRACEUI_INACTIVE` | Connected, with a handle, but GraceUI is inactive for the session; `reason` says why. Calls do nothing, but `notice` in the anti-cheat's notice-only mode |
| `GRACEUI_REFUSED` | An argument failed its check; `reason` says which. Nothing was done |

| `reason` | Meaning |
|---|---|
| `GRACEUI_INACTIVE_ANTI_CHEAT` | An anti-cheat was found before GraceUI hooked: notices only (D88) |
| `GRACEUI_INACTIVE_HOST_FAILED` | The overlay could not start (no game window, a hook refused) |
| `GRACEUI_INACTIVE_RENDER_DEAD` | The overlay turned itself off after a fault: nothing more is drawn |
| `GRACEUI_REFUSED_ARGUMENT` | A null pointer, or a struct smaller than its first size |
| `GRACEUI_REFUSED_ID` | The id is not 3 to 32 of a-z, 0-9 and `-` |
| `GRACEUI_REFUSED_NAME` | The display name is empty, longer than 64 bytes, or not UTF-8 text |
| `GRACEUI_REFUSED_CAPS` | The caps are longer than 256 bytes, or not cap names separated by spaces |
| `GRACEUI_REFUSED_TOO_MANY` | 32 mods are connected (`GRACEUI_MODS_MAX`) |

`connect` checks the strings, then the floor, then the id among the mods connected, then room (D131). With
`GRACEUI_OK` and with `GRACEUI_INACTIVE` the mod is connected and holds its handle; nothing else connects it. Any other
answer is a failed connect, which leaves the mod headless with a log line (invariant 15): its UI is off.

`disconnect(consumer)` ends the connection: the mod's notices are withdrawn and its id is free again. It returns only
once the mod's `on_present` callbacks are not running, waiting for the frame being drawn to end ([Threads and
budgets](threads.md)), and it closes Steam's floating keyboard if GraceUI opened one for the mod (D276).

## The slots

### Pages and topics

Since GraceUI 0.2, cap `pages` (D169). A mod's space is its pages, topics and commands, as the bridge protocol's
frames ([The protocol](protocol.md)).

- **`register_pages(consumer, &space)`** takes the mod's one space, copied before it returns. `GraceUiSpaceDesc` holds
  its `views` (`GRACEUI_VIEW_GAME`, the in-game menu; `GRACEUI_VIEW_BROWSER`, the browser view, whose server runs while
  a space asks for it; one or both), its `version` and `increment`, named in `welcome`, its page data (`space_pages` as
  UTF-8 JSON, its `space` the mod's id) and its settings description (`settings_description` as UTF-8 JSON), each 1
  MiB at most (EST).

  | Answer | When |
  |---|---|
  | `GRACEUI_OK` | Registered |
  | `GRACEUI_REFUSED` | Its JSON fails the protocol's decoders, its pages and settings disagree or a default fails, its `space` is not the mod's id, it names no known view, or its page data's minor is above this GraceUI's (D264) |
  | `GRACEUI_DUPLICATE_ID` | The mod, or another mod, has registered this space already |
  | `GRACEUI_FULL` | 16 spaces are registered |
  | `GRACEUI_INACTIVE` | The notice-only mode, where no server runs beside an anti-cheat |

- **`publish(consumer, frame, length)`** takes a `state` or `rows` frame of the mod's own space
  ([Publishing](protocol.md#publishing)).
- **`answer(consumer, reply, frame, length)`** takes the result of the command `reply` names
  ([Answers](protocol.md#answers)).

### Events: poll and wake_event

**`poll(consumer, &event)`** gives the mod's next event at once: `GRACEUI_OK` with one, `GRACEUI_EVENT_NONE` if nothing
is waiting, or `GRACEUI_REFUSED` for an event that is null or below its first size. It never waits (D155): call it
until it gives `GRACEUI_EVENT_NONE`. Hotkeys come first, then the space's events (D169). A frame is written into the
mod's buffer (`frame`, `frame_capacity`): a buffer too small answers `GRACEUI_FULL` with `frame_length` the size needed,
and the event waits for the next call. `GRACEUI_FRAME_FROM_PAGE_MAX`, 16 KiB, always suffices. A file command's file
goes into `file`, `file_capacity` (`GRACEUI_UPLOAD_MAX`, 32 MiB, always suffices), likewise (1.6).

Each kind's fields were appended with the minor that brought it: an event struct too small for them never gets that
kind, so an older mod sees what it saw. `GRACEUI_EVENT_HOTKEY_CHANGED` goes only to a mod connected with ABI minor 4 or
more.

| Kind | Value | What it says | Since |
|---|---|---|---|
| `GRACEUI_EVENT_NONE` | 0 | Nothing is waiting | 1.0 |
| `GRACEUI_EVENT_HOTKEY` | 1 | A hotkey of the mod's was pressed: `hotkey_id`, and `count`, its presses since its last event | 1.0 |
| `GRACEUI_EVENT_WATCHED` | 2 | A view now watches a topic of the mod's space: `frame` holds its `sub` frame | GraceUI 0.2 |
| `GRACEUI_EVENT_UNWATCHED` | 3 | No view watches the topic now: `frame` holds its `unsub` frame; stop building it | GraceUI 0.2 |
| `GRACEUI_EVENT_COMMAND` | 4 | A view's command: `frame` holds its `cmd` frame, `reply` the handle `answer` takes | GraceUI 0.2 |
| `GRACEUI_EVENT_KEY` | 5 | The holder's key: `key`, `modifiers`, `pressed`, `repeat` | 1.1 |
| `GRACEUI_EVENT_CHARACTER` | 6 | One character typed: `code_point` | 1.1 |
| `GRACEUI_EVENT_POINTER` | 7 | The holder's pointer: `x`, `y`; a button (`button_event`, `button`, `pressed`); the wheel | 1.1 |
| `GRACEUI_EVENT_INPUT_ENDED` | 8 | The mod's holder ended: `why` | 1.1 |
| `GRACEUI_EVENT_PAD` | 9 | The holder's pad button: `button`, `pressed`, `pad` | 1.1 |
| `GRACEUI_EVENT_DISPLAY` | 10 | The display: once after warm-up, then after each change of its size or Menu size, and from 1.9 of Plain text | 1.2 |
| `GRACEUI_EVENT_HOTKEY_CHANGED` | 11 | The player changed a hotkey's key or pad chord (`hotkey_id`): `describe_hotkey` gives it as it is now | 1.4 |

The holder's events are in [Input and focus](input.md).

**`wake_event(consumer, &event)`** (1.5, D212) gives the mod a Win32 `HANDLE` of its own, with SYNCHRONIZE access, to an
auto-reset event: `GRACEUI_OK`; `GRACEUI_REFUSED` for a null `event`; `GRACEUI_FAULT` if Windows made no event. GraceUI
sets it whenever something is kept for the mod's poll, and once when it is asked for. The mod waits on it, polls until
`GRACEUI_EVENT_NONE`, then waits again ([Threads and budgets](threads.md)).

### Notices

Cap `notices`. A notice is a short text GraceUI shows in game above everything else, the menu included (the notice
layer, D76).

| `GraceUiNotice` | Meaning |
|---|---|
| `tone` | `GRACEUI_TONE_NORMAL` (a gold accent), `_WARNING` (ember) or `_DANGER` (rot) |
| `key` | 0: a new notice. Otherwise the mod's own key: a notice with the key of one showing replaces it in place, and one with an empty text withdraws it |
| `duration_ms` | How long it shows, from the call; 0 until it is replaced or withdrawn (a banner) |
| `text`, `text_length` | 1 to 512 bytes of UTF-8 (`GRACEUI_NOTICE_TEXT_MAX`); empty, with a key, to withdraw |

`notice(consumer, &notice)` answers `GRACEUI_OK`; `GRACEUI_REFUSED` for a text that is not UTF-8, too long, or empty
without a key; `GRACEUI_FULL` with 4 of the mod's notices showing (`GRACEUI_NOTICES_PER_MOD`). It stays callable in the
anti-cheat's notice-only mode, after `connect` answered `GRACEUI_INACTIVE` with `GRACEUI_INACTIVE_ANTI_CHEAT` (D88).

### Panels, textures and faces

Caps `panels`, `textures` and `faces` (D76). A panel is a rectangle of the display a mod draws itself, on a layer,
hidden until it is shown.

- **`create_panel(consumer, &panel, &panel_id)`**: `GRACEUI_OK` with its id, `GRACEUI_REFUSED`, or `GRACEUI_FULL` (16
  a mod, `GRACEUI_PANELS_PER_MOD`). `GraceUiPanelDesc` holds its `layer` (`GRACEUI_LAYER_HUD`, `_PANEL` or `_MODAL`;
  from the bottom: hud, panel, GraceUI's menu, modal, then the notices), its `anchor` (one of nine points of the
  display, `GRACEUI_ANCHOR_TOP_LEFT` to `_BOTTOM_RIGHT`), `flags` (`GRACEUI_PANEL_TAKES_INPUT`: it may take input), and
  its bounds from the anchor in the display's pixels, `x`, `y` and a `width` and `height` from 1 to 16384.
- **`submit(consumer, panel_id, &list)`**: the panel's next draw list, copied and checked: `GRACEUI_OK` or
  `GRACEUI_REFUSED`. A list that fails is logged once, and the panel keeps its last list. The mod's first submit closes
  its registration window.
- **`show_panel(consumer, panel_id)`** and **`hide_panel(consumer, panel_id)`**. Hiding a panel that holds input ends
  its holder.

A draw list is in Dear ImGui's default `ImDrawVert` layout (position, texture coordinate, colour as bytes R, G, B, A),
with 16-bit indices relative to each command's own vertex range and commands that each name a clip rectangle within
the panel, a texture and their ranges. Its coordinates are the panel's own pixels, its top-left corner at 0,0; GraceUI
moves the list where the anchor puts the panel. The checks, the bounds and a mod's own headless Dear ImGui are in
[Panels and your own Dear ImGui](panels.md).

- **`register_texture(consumer, &texture, &texture_id)`**: in the mod's registration window, `GRACEUI_TEXTURE_RGBA8`
  (four bytes a pixel, R first, straight alpha), `row_bytes` at least `width` × 4, each side at most 4096, its pixels
  copied before the call returns. `GRACEUI_OK` with its id; `GRACEUI_REFUSED` for a format, size or row length GraceUI
  does not take; `GRACEUI_FULL` over the mod's budget or the session's; `GRACEUI_TOO_LATE` after its first submit.
- **`register_face(consumer, &face, &face_id)`**: a TrueType or OpenType font file for GraceUI's one shared atlas,
  before the overlay's warm-up, with its `pixel_height`, its code-point ranges (pairs of first and last; null for
  GraceUI's default) and, from 1.2, `flags` (`GRACEUI_FACE_SCALED`: baked at the player's scale). `GRACEUI_OK` with its
  id; `GRACEUI_REFUSED` for a file that is not TrueType or OpenType, a size outside 6 to 128 pixels, or bad ranges;
  `GRACEUI_FULL`; `GRACEUI_TOO_LATE` after warm-up.
- **`glyph_table(consumer, face_id, &table)`**: a face's glyphs in the shared atlas, GraceUI's own among them (face 1:
  the notices' face), for a mod to lay out its own text in its draw lists. `GRACEUI_OK`; `GRACEUI_NOT_READY` before
  warm-up; `GRACEUI_REFUSED` for an unknown face. A draw list built from a table names its `generation`.

The registration window and the budgets are in [Threads and budgets](threads.md).

### Input

Cap `input`; the holder's events and `set_caret` with cap `holder-events` (1.1, D176). [Input and
focus](input.md) holds the rules.

- **`request_input(consumer, &request, &answer)`**: `GRACEUI_OK` with the answer, given at once: granted, refused
  naming the holder, or not now with the reason. `GRACEUI_REFUSED` for a request that names no panel of the mod's that
  may take input, or a kind that does not suit its layer.
- **`release_input(consumer)`**: gives input back if the mod holds it; `GRACEUI_OK` either way.
- **`set_caret(consumer, &caret)`** (1.1): where the text caret of the mod's field is, in its panel's pixels, so that
  GraceUI places the IME's windows at it. `GRACEUI_OK`; `GRACEUI_REFUSED` for a panel not the mod's or a caret not
  finite.

### Hotkeys

Cap `hotkeys`; pad chords with `pad-chords` (1.3), bindings with `hotkey-bindings` (1.4), pausing with `hotkey-pause`
(1.11). [Input and focus](input.md#hotkeys) holds the rules.

- **`register_hotkey(consumer, &hotkey, &hotkey_id)`**: `GRACEUI_OK` with its id, bound; `GRACEUI_UNBOUND` with its
  id, registered but unbound (D164); `GRACEUI_REFUSED`; `GRACEUI_FULL` (8 a mod, or 64 in the session);
  `GRACEUI_NOT_YET` for a pad chord from a mod built before 1.3.
- **`describe_hotkey(consumer, hotkey_id, &binding)`** (1.4): the hotkey as the player has bound it now.
  `GRACEUI_OK`; `GRACEUI_REFUSED` for an id not the mod's, or a binding that is null or smaller than 1.4's.
- **`set_hotkey_enabled(consumer, hotkey_id, enabled)`** (1.11): pauses (`enabled` 0) or resumes one of the mod's own
  hotkeys. `GRACEUI_OK`, also when it was so already; `GRACEUI_REFUSED` for an id not the mod's.

### World state

Cap `world-state` (D78). **`set_world_state(consumer, &state)`** sets the mod's claims as a world-state provider,
replacing its earlier ones: `GRACEUI_OK`, or `GRACEUI_REFUSED` for an unknown flag. Every provider's claims are
combined.

| Flag | Claims |
|---|---|
| `GRACEUI_WORLD_DANGER` | Danger: every view closes and any confirmation is cancelled (invariant 9) |
| `GRACEUI_WORLD_LOADING` | A loading screen |
| `GRACEUI_WORLD_GAME_MENU` | A game menu, or the game's own text entry, is open |
| `GRACEUI_WORLD_TITLE` | The title screen |
| `GRACEUI_WORLD_MULTIPLAYER` | Multiplayer |
| `GRACEUI_WORLD_ANTI_CHEAT` | The provider found an anti-cheat: the browser view closes for the session (D81) |

Any provider's danger or loading ends the input holder. Without a provider there is no danger close: GraceUI reads no
game state (D78).

### Log

**`log(consumer, level, text, length)`**, with `GRACEUI_LOG_INFO`, `_WARNING` and `_ERROR`, is declared for the
increment that builds it, and answers `GRACEUI_NOT_YET` until then (D131, D169).

### on_present

Cap `on-present` (1.8, D259). **`set_on_present(consumer, panel_id, &hook)`** sets, replaces or clears (a null
callback) the callback that draws one of the mod's panels in each frame, on the game's present thread.
`GraceUiPresentHook` holds the room its lists need (`vertex_capacity`, `index_capacity`, `command_capacity`), the `callback` and a `user`
pointer given back to it as is. Setting one closes the mod's registration window, as its first submit does.

| Answer | When |
|---|---|
| `GRACEUI_OK` | Set, replaced or cleared |
| `GRACEUI_REFUSED` | A panel not the mod's, a hook null or below its size, capacities outside their bounds, a callback set while the mod's are suspended (clearing one is taken), or a call from inside a callback |
| `GRACEUI_FULL` | 4 callbacks a mod, or 8 in the session |

The callback's contract and its budget are in [Threads and budgets](threads.md#on_present).

### pad_keyboard

Cap `pad-keyboard` (1.10, D276). **`pad_keyboard(consumer, &keyboard)`** opens Steam's floating keyboard beside the
field of the mod's that holds input, or with a null keyboard closes the one GraceUI opened for it.

| Answer | When |
|---|---|
| `GRACEUI_OK` | Shown; or closed, or none to close |
| `GRACEUI_UNAVAILABLE` | No keyboard to show here; GraceUI's log says why |
| `GRACEUI_REFUSED` | The mod does not hold input, a panel not the mod's, an unknown mode, a field not finite or not more than 0 wide and high, a keyboard below its size, or a call from inside a callback |

[Input and focus](input.md#steams-floating-keyboard) holds the rules.

## Status codes

| Status | Value | Meaning |
|---|---|---|
| `GRACEUI_OK` | 0 | Done |
| `GRACEUI_TOO_OLD` | 1 | `connect`: this GraceUI is older than the mod's floor; the answer names both versions |
| `GRACEUI_DUPLICATE_ID` | 2 | `connect`: a connected mod has this id already; `register_pages`: the space is registered already |
| `GRACEUI_INACTIVE` | 3 | GraceUI is inactive for this session (`connect`'s answer says why): calls do nothing, but `notice` in the notice-only mode (D88) |
| `GRACEUI_REFUSED` | 4 | An argument failed its check; nothing was done |
| `GRACEUI_NOT_CONNECTED` | 5 | The handle names no connected mod |
| `GRACEUI_NOT_YET` | 6 | The slot is declared, but this GraceUI does not provide it yet |
| `GRACEUI_FULL` | 7 | A bounded store has no room: the mod's notices, panels, textures or faces, or the session's; or a buffer too small for poll's event |
| `GRACEUI_FAULT` | 8 | A fault inside GraceUI: nothing was done, and GraceUI's log has a line (D110) |
| `GRACEUI_NOT_READY` | 9 | Not yet possible in this session: ask again later (`glyph_table` before warm-up) |
| `GRACEUI_TOO_LATE` | 10 | Past its time: a texture after the mod's registration window, a face after warm-up |
| `GRACEUI_UNBOUND` | 11 | `register_hotkey`: registered, with its id, but its default is taken or it has none; it fires nothing until the player binds it (D164) |
| `GRACEUI_UNAVAILABLE` | 12 | `pad_keyboard`: no Steam floating keyboard to show now (1.10, D276) |
