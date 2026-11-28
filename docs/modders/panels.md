# Panels and your own Dear ImGui

A panel is a rectangle of the display that a mod draws itself, on one of GraceUI's layers. The mod builds each draw
list on its own thread and hands it to GraceUI, which copies it, checks it and draws the newest on the game's present
thread (D76). Any producer may build a list: a mod's own headless Dear ImGui, as the example mods do, or code of its
own. A mod links no hook library and no Dear ImGui renderer backend, hooks none of the functions GraceUI hooks, and
hands GraceUI no ImGui context (invariant 16). The slots' answers are in [The C ABI](abi.md#panels-textures-and-faces);
which thread calls what, the registration window and the budgets are in [Threads and budgets](threads.md).

Caps `panels`, `textures` and `faces`; `on-present` (ABI 1.8) for a panel drawn afresh in each frame.

## A panel

`create_panel(consumer, &panel, &panel_id)` makes one, hidden.

| `GraceUiPanelDesc` | Meaning |
|---|---|
| `layer` | `GRACEUI_LAYER_HUD`, `_PANEL` or `_MODAL` |
| `anchor` | One of nine points of the display, `GRACEUI_ANCHOR_TOP_LEFT` (0) to `_BOTTOM_RIGHT` (8) |
| `flags` | `GRACEUI_PANEL_TAKES_INPUT`: the panel may take input ([Input and focus](input.md)); any other bit is refused |
| `x`, `y` | Its offsets from the anchor, in the display's pixels: finite, at most 16384 either way |
| `width`, `height` | Its size in the display's pixels, 1 to 16384 |

The layers are drawn from the bottom: hud, panel, GraceUI's menu, modal, then the notices. A panel on the hud or the
panel layer is under the menu while it is open; a modal is above it; the notices are above everything.

The anchor names a point of the display and the same point of the panel. `x` and `y` move the panel inwards from the
anchor's edges: from the left edge for a left anchor, leftwards from the right edge for a right one, rightwards and
downwards from the centre for a centred one; `y` likewise from the top, the bottom or the middle. `hello-panel` sits 40
pixels from the display's left and 120 from its top (`GRACEUI_ANCHOR_TOP_LEFT`); `hello-debug-view` 40 from the left
and 40 up from the bottom (`GRACEUI_ANCHOR_BOTTOM_LEFT`).

| `create_panel` answers | When |
|---|---|
| `GRACEUI_OK` | Made, hidden, with its id |
| `GRACEUI_REFUSED` | An unknown layer, anchor or flag, or bounds that are not finite or not 1 to 16384 pixels |
| `GRACEUI_FULL` | 16 of the mod's (`GRACEUI_PANELS_PER_MOD`), or 64 in the session (`GRACEUI_PANELS_MAX`, EST) |

`show_panel(consumer, panel_id)` and `hide_panel(consumer, panel_id)` answer `GRACEUI_OK`, or `GRACEUI_REFUSED` for a
panel that is not the mod's. Hiding the panel that holds input ends its holder (D77). A mod's panels go when it
disconnects.

A panel's bounds are in the display's pixels. A mod sizes its content by its content scale, (H / 1080) × `ui_scale` /
`ui_scale_default`, from `connect`'s answer and `GRACEUI_EVENT_DISPLAY` (ABI 1.2; [connect](abi.md#connect)).

`hello-panel` makes its panel and shows it:

    GraceUiPanelDesc panel_desc{};
    panel_desc.size = sizeof panel_desc;
    panel_desc.layer = GRACEUI_LAYER_PANEL;
    panel_desc.anchor = GRACEUI_ANCHOR_TOP_LEFT;
    panel_desc.x = HELLO_PANEL_X;
    panel_desc.y = HELLO_PANEL_Y;
    panel_desc.width = kWidth;
    panel_desc.height = kHeight;
    std::uint64_t panel = 0;
    if (api.create_panel(me, panel_desc, panel) != GRACEUI_OK || api.show_panel(me, panel) != GRACEUI_OK) {
      return 0;
    }

## A draw list

A draw list is in Dear ImGui's default layout (D76): `ImDrawVert` and 16-bit indices.

| `GraceUiDrawList` | Meaning |
|---|---|
| `size` | `sizeof(GraceUiDrawList)` |
| `atlas_generation` | The shared atlas's generation the list was built against, if a command draws from it; else 0 |
| `vertex_count`, `index_count`, `command_count` | The lengths of the three arrays |
| `command_size` | `sizeof(GraceUiDrawCommand)` as the mod was built: the commands' stride |
| `vertices`, `indices`, `commands` | The mod's arrays, copied before `submit` returns |

| `GraceUiDrawVert` | Meaning |
|---|---|
| `x`, `y` | Its position, in the panel's own pixels |
| `u`, `v` | Its texture coordinate |
| `colour` | Four bytes: R, G, B, A |

| `GraceUiDrawCommand` | Meaning |
|---|---|
| `clip_x0`, `clip_y0`, `clip_x1`, `clip_y1` | Its clip rectangle, in the panel's pixels, within its width and height |
| `texture` | A texture of the mod's own (`register_texture`), or the shared atlas (`glyph_table`'s `atlas`) |
| `vertex_offset`, `vertex_count` | Its vertices: at most 65,536 from the offset |
| `index_offset`, `index_count` | Its indices, each relative to `vertex_offset` |

A list's coordinates are the panel's own pixels, its top-left corner at 0,0, wherever the panel is on the display:
GraceUI moves the list where the anchor puts the panel. Every clip rectangle lies within the panel, so nothing is drawn
outside it.

## submit and its checks

`submit(consumer, panel_id, &list)` copies the list on the mod's thread and checks GraceUI's copy, which the mod cannot
change while it is checked; only a list that passes is published to the panel. The present thread draws the newest
published list, one frame or more after the mod built it ([Threads and budgets](threads.md#the-present-thread)).

The checks, in order (D76):

| Check | Refused when | The log's words |
|---|---|---|
| Size, before anything is copied | More than 13,107 vertices (256 KiB), 131,072 indices (256 KiB) or 64 commands, or a `command_size` below ABI 1.0's command (40 bytes) or above 256 | more vertices, indices or commands than a list may hold |
| Arrays | A null array for a count that is not 0 | a null array for a count that is not 0 |
| Each command's vertices | Beyond the list's vertices, or more than 65,536 | a command's vertices outside the list's |
| Each command's indices | Beyond the list's indices, or a count that is not a multiple of 3 | a command's indices outside the list's, or not whole triangles |
| Each index | Not below its command's `vertex_count` | an index beyond its command's vertices |
| Each clip rectangle | Not finite, inverted, or outside 0 to the panel's width and height | a clip rectangle outside the panel's bounds |
| Each texture | Neither one of the mod's own textures nor the shared atlas | a texture that is neither the mod's nor the shared atlas |
| The atlas's generation | A list that draws from the shared atlas names another generation than the atlas's now, or comes before the atlas is baked | the shared atlas at another generation |

An empty list passes, and draws nothing.

| `submit` answers | When |
|---|---|
| `GRACEUI_OK` | Checked and published: the panel draws it from a coming frame |
| `GRACEUI_REFUSED` | A panel that is not the mod's, or a list that fails a check. The panel keeps its last list |
| `GRACEUI_FAULT` | GraceUI could not copy the list; nothing was done |
| `GRACEUI_NOT_CONNECTED` | The handle names no connected mod: `hello-panel` stops drawing on it |

A refused list is logged once for each connection of the mod, in GraceUI's log: "a draw list of ‹id› was refused:
‹why› (logged once for this mod)". A list an `on_present` callback draws is checked the same way
([on_present](threads.md#on_present)).

The mod's registration window closes at its first `submit` to a panel of its own, whether or not the list passes, or at
its first `set_on_present` ([The registration window](threads.md#the-registration-window)).

## Textures and faces

A command draws from one texture: one of the mod's own, or GraceUI's shared atlas.

- **A texture of the mod's own** is registered in its registration window, before its first submit:
  `register_texture`, four bytes a pixel, straight alpha, each side at most 4096
  ([The C ABI](abi.md#panels-textures-and-faces)). Its id is what a command names. A mod's own Dear ImGui font atlas is one such texture (below).
- **The shared atlas** holds the faces GraceUI bakes once at warm-up: GraceUI's own (face 1 is the notices' face) and
  the faces mods registered before warm-up, whose ids start at 16 (D139). `glyph_table(consumer, face_id, &table)`
  gives a face's glyphs and the atlas's texture id (`atlas`); it answers `GRACEUI_NOT_READY` until warm-up.

A glyph table's glyphs are sorted by code point. A glyph's quad is measured from the pen at the top of the line, as Dear
ImGui's are: the baseline is at the pen's y plus `ascent`, `descent` is below the baseline and negative, and the next
line is `pixel_height` below. A list that draws glyphs of the atlas names the table's `generation` in its
`atlas_generation`. The atlas is baked once a session (invariant 11), so a table stays valid for the session.

A face whose file Dear ImGui cannot read is left out at warm-up, with one log line (D139). The numbers of textures and
faces a mod may register, and their sizes, are in [Capacities](threads.md#capacities).

`hello-panel` registers, in this order: its face for the shared atlas (before warm-up, or never), its own Dear ImGui's
atlas as a texture of its own, and a second texture, its checker; then it makes its panel and draws. It asks for its
face's glyph table at each frame until it is given one.

## Notices

Cap `notices`. A notice is a short text GraceUI draws above everything else, the menu and modals included (D76, D134).

- **Tones.** `GRACEUI_TONE_NORMAL` (a gold accent), `_WARNING` (ember) and `_DANGER` (rot); an unknown tone is refused.
  There is no kind field: a notice with a duration of 0 is a banner, shown until it is replaced or withdrawn (D134).
- **Keys.** Key 0 is a new notice each time. A notice under one of the mod's own keys replaces the one showing under
  it in place, with its new text, tone and duration; an empty text under that key withdraws it.
- **Text.** 1 to 512 bytes of UTF-8 with no control character but the line feed.
- **Room.** 4 of a mod's at once (`GRACEUI_NOTICES_PER_MOD`), 16 in all, GraceUI's own among them: past either,
  `notice` answers `GRACEUI_FULL`. They are shown oldest first, and a mod's go when it disconnects.
- **Quiet.** A mod whose notices the player has turned off on GraceUI › Installed mods gets `GRACEUI_OK` as before;
  its notice is kept on that page instead of drawn over the game, and quieting it withdraws what it shows (D181).
- **Notice-only mode.** `notice` stays callable after `connect` answered `GRACEUI_INACTIVE` with
  `GRACEUI_INACTIVE_ANTI_CHEAT` (D88; [Versions and distribution](versions.md#the-anti-cheats-notice-only-mode)).

`hello-debug-view` keeps one banner under its own key, its text replaced as its view opens and closes:

    void banner(const graceui::Api& api, GraceUiConsumer me, const char* text) {
      api.notice(me, text, GRACEUI_TONE_NORMAL, HELLO_DEBUG_BANNER_KEY, 0);
    }

## Your own Dear ImGui

A mod may draw its panels with a Dear ImGui of its own: the core of Dear ImGui 1.93.0, GraceUI's version, compiled into
the mod, headless, with no renderer or platform backend and no hook (D156, invariant 16). Its context is the mod's
own, made on the mod's thread; GraceUI never sees it. Dear ImGui's default `ImDrawVert` and 16-bit `ImDrawIdx` are
D76's layout, so a list needs no conversion; a build that overrides either converts first (D76).

`graceui_imgui.hpp` (`src/graceui-sys/include/`, header-only C++, MIT) does the joining. Include `imgui.h` before it,
or let it include it.

| Call | When | What it does |
|---|---|---|
| `graceui::imgui::prepare(io)` | Once, after `ImGui::CreateContext()` | Sets `ImGuiBackendFlags_RendererHasVtxOffset` (GraceUI draws each command with its vertex offset), turns off Dear ImGui's own cursor (GraceUI's host draws the Grace pointer for a holder that takes the mouse) and its ini and log files |
| `graceui::imgui::register_atlas(api, me, io)` | Once, in the registration window, after the fonts are added | Bakes the font atlas once and registers it as one of the mod's textures, then tells Dear ImGui the atlas is on the GPU, so it never asks for an update during play (invariant 11). The texture's id, or 0 if GraceUI refused it |
| `DrawList::build(draw_data, width, height, atlas_generation)` | Each frame, after `ImGui::Render()` | Appends every `ImDrawList` of the frame into one GraceUI list, each command keeping its vertex offset, its clip rectangle moved into the panel's pixels and held within them. Commands with a user callback are left out: GraceUI draws vertices only. A frame past ABI 1's bounds builds nothing and answers false: skip that frame |
| `DrawList::view()` | Each submit | The list as `submit` takes it, valid until the next `build()` or `clear()` |
| `graceui::imgui::feed(io, event)` | For each holder event polled, before `ImGui::NewFrame()` (ABI 1.1) | The holder's keys, characters, pointer and pad as the context's input ([Input and focus](input.md#the-holders-events)) |

A frame is built in the panel's own pixels: `io.DisplaySize` is the panel's width and height. A texture of the mod's own
is drawn by its id as Dear ImGui's `ImTextureID`, and glyphs of the shared atlas under `glyph_table`'s `atlas`, as
`hello-panel` does with `ImGui::Image(ImTextureRef(checker), ...)` and `PushTexture(ImTextureRef(glyphs->atlas))`.

`hello-panel` (`examples/hello-panel/hello_panel.cpp`) sets up its Dear ImGui:

    // Its own Dear ImGui, headless: no backend, no platform; its atlas built once and registered as its own texture.
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    graceui::imgui::prepare(io);  // GraceUI draws each command with its vertex offset
    io.Fonts->AddFontDefaultBitmap();
    if (graceui::imgui::register_atlas(api, me, io) == 0) {
      return 0;
    }

begins each frame in the panel's pixels:

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(kWidth, kHeight);  // the panel's own pixels: GraceUI puts it where its anchor says
    io.DeltaTime = 0.01f;
    ImGui::NewFrame();

ends it with one list of GraceUI's:

    frame.build(*ImGui::GetDrawData(), kWidth, kHeight, glyphs != nullptr ? glyphs->generation : 0);

and submits a frame every 10 ms, from its own thread:

    // Its frames, from this thread: the first submit ends its registration window.
    GraceUiGlyphTable glyphs{};
    bool have_glyphs = false;
    graceui::imgui::DrawList frame;
    for (;;) {
      if (face != 0 && !have_glyphs) {
        have_glyphs = api.glyph_table(me, face, glyphs) == GRACEUI_OK;  // GRACEUI_NOT_READY until warm-up
      }
      build(frame, checker_id, have_glyphs ? &glyphs : nullptr);
      if (api.submit(me, panel, frame.view()) == GRACEUI_NOT_CONNECTED) {
        return 0;
      }
      Sleep(10);
    }

`hello-debug-view` keeps its panel hidden until its hotkey, and gives it a list before it shows, so it never shows
empty; it submits only a list that was built:

    build(list, ++frames, toggles);
    api.submit(me, panel, list.view());  // its first list before it shows, so it never shows empty
    api.show_panel(me, panel);

A panel whose content must change in every frame, such as a fade, asks for an `on_present` callback instead of waiting
for its next submit ([on_present](threads.md#on_present)).
