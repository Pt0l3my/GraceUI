/* SPDX-License-Identifier: MIT */
/*
 * graceui_mock.c: the mock of GraceUI's ABI 1 (graceui_mock.h, D285). One mock in the process, its state below; every
 * slot records its call, then answers as GraceUI does for what a mod's test needs. Pure C99, the standard library
 * alone.
 */
#include "graceui_mock.h"

#include <stdlib.h>
#include <string.h>

#define MOCK_QUEUE_MAX 256u     /* a mod's events waiting, as GraceUI's holder queue holds */
#define MOCK_HOTKEYS_MAX 64u    /* GRACEUI_HOTKEYS_MAX's session */
#define MOCK_PANELS_MAX GRACEUI_PANELS_MAX
#define MOCK_REPLIES_MAX 1024u  /* commands queued in one test */
#define MOCK_CAPS_MAX 1024u

typedef struct MockEvent {
  GraceUiEvent event; /* its fields; frame and file unused */
  char* frame;        /* a protocol event's frame, owned */
  uint64_t frame_length;
} MockEvent;

typedef struct MockMod {
  GraceUiConsumer handle;
  uint32_t connected;
  uint32_t abi_minor;
  char id[GRACEUI_ID_MAX + 1];
  uint32_t registered_pages;
  uint32_t window_closed; /* its registration window: closed by its first submit or set_on_present */
  uint32_t textures;
  uint32_t faces;
  uint32_t panels;
  uint32_t hotkeys;
  MockEvent queue[MOCK_QUEUE_MAX];
  uint32_t queued;
} MockMod;

typedef struct MockHotkey {
  GraceUiConsumer owner;
  uint32_t key;
  uint32_t modifiers;
  uint32_t pad_chord;
  uint32_t bound;   /* GRACEUI_HOTKEY_*_BOUND */
  uint32_t enabled;
} MockHotkey;

typedef struct MockPanel {
  GraceUiConsumer owner;
  uint32_t shown;
  GraceUiPresentHook hook; /* its on_present callback, if set */
} MockPanel;

typedef struct MockCall {
  GraceUiMockCall call; /* as graceui_mock_call gives it */
  char* text;           /* the copy call.text points to, owned */
} MockCall;

typedef struct MockReply {
  GraceUiConsumer owner;
  uint32_t answered;
  char* frame; /* the result it was answered with, owned */
} MockReply;

static struct {
  uint32_t ready;
  GraceUiApi1 table;
  uint32_t version;
  uint32_t minor;
  char caps[MOCK_CAPS_MAX];
  uint32_t inactive;
  GraceUiStatus fail_next[GRACEUI_MOCK_SLOTS];
  uint32_t failing[GRACEUI_MOCK_SLOTS];
  uint32_t input_answer;
  uint32_t input_why;
  char input_holder_name[40];
  GraceUiConsumer holder;
  uint32_t glyph_ok;
  GraceUiGlyphTable glyphs;
  MockMod mods[GRACEUI_MODS_MAX];
  uint32_t mod_count;
  MockHotkey hotkeys[MOCK_HOTKEYS_MAX];
  uint32_t hotkey_count;
  MockPanel panels[MOCK_PANELS_MAX];
  uint32_t panel_count;
  uint64_t texture_count;
  uint32_t face_count;
  MockReply replies[MOCK_REPLIES_MAX];
  uint32_t reply_count;
  MockCall* calls;
  size_t call_count;
  size_t call_capacity;
} g;

static const char kAllCaps[] =
    GRACEUI_CAP_NOTICES " " GRACEUI_CAP_PANELS " " GRACEUI_CAP_TEXTURES " " GRACEUI_CAP_FACES " " GRACEUI_CAP_INPUT
    " " GRACEUI_CAP_WORLD_STATE " " GRACEUI_CAP_HOTKEYS " " GRACEUI_CAP_PAGES " " GRACEUI_CAP_HOLDER_EVENTS
    " " GRACEUI_CAP_DISPLAY " " GRACEUI_CAP_PAD_CHORDS " " GRACEUI_CAP_HOTKEY_BINDINGS " " GRACEUI_CAP_WAKE
    " " GRACEUI_CAP_HOTKEY_BY " " GRACEUI_CAP_ON_PRESENT " " GRACEUI_CAP_PLAIN_TEXT " " GRACEUI_CAP_PAD_KEYBOARD
    " " GRACEUI_CAP_HOTKEY_PAUSE;

static void build_table(void);

/* ---- Recording. */

static char* copy_text(const char* text, uint64_t length) {
  char* out = malloc((size_t)length + 1);
  if (out == NULL) {
    abort(); /* a test's mock out of memory: nothing sensible to answer */
  }
  if (length != 0 && text != NULL) {
    memcpy(out, text, (size_t)length);
  }
  out[length] = '\0';
  return out;
}

static GraceUiStatus record(uint32_t slot, GraceUiStatus status, GraceUiConsumer consumer, uint64_t id, uint64_t value,
                            const char* text, uint64_t text_length) {
  MockCall* made;
  GraceUiMockCall* call;
  if (g.call_count == g.call_capacity) {
    size_t capacity = g.call_capacity == 0 ? 64 : g.call_capacity * 2;
    MockCall* grown = realloc(g.calls, capacity * sizeof *grown);
    if (grown == NULL) {
      abort();
    }
    g.calls = grown;
    g.call_capacity = capacity;
  }
  made = &g.calls[g.call_count++];
  call = &made->call;
  call->slot = slot;
  call->status = status;
  call->consumer = consumer;
  call->id = id;
  call->value = value;
  call->text_length = text != NULL ? text_length : 0;
  made->text = copy_text(text, call->text_length);
  call->text = made->text;
  return status;
}

static void ensure_ready(void) {
  if (!g.ready) {
    graceui_mock_reset();
  }
}

/* The slot's failure the test asked for, once: 1 with *status set. */
static int failing(uint32_t slot, GraceUiStatus* status) {
  if (slot < GRACEUI_MOCK_SLOTS && g.failing[slot]) {
    g.failing[slot] = 0;
    *status = g.fail_next[slot];
    return 1;
  }
  return 0;
}

static MockMod* mod_of(GraceUiConsumer consumer) {
  uint32_t i;
  for (i = 0; i < g.mod_count; ++i) {
    if (g.mods[i].handle == consumer && g.mods[i].connected) {
      return &g.mods[i];
    }
  }
  return NULL;
}

/* The common gate of a slot that names a mod: GRACEUI_OK to go on, else the status to answer. */
static GraceUiStatus gate(uint32_t slot, GraceUiConsumer consumer, MockMod** mod, int works_inactive) {
  GraceUiStatus status;
  ensure_ready();
  *mod = mod_of(consumer);
  if (failing(slot, &status)) {
    return status;
  }
  if (*mod == NULL) {
    return GRACEUI_NOT_CONNECTED;
  }
  if (g.inactive != 0 && !works_inactive) {
    return GRACEUI_INACTIVE;
  }
  return GRACEUI_OK;
}

static int fits(uint32_t given, size_t first) {
  return given >= first;
}

/* ---- The slots. */

static int has_cap_named(const char* name, uint64_t length) {
  const char* at = g.caps;
  if (name == NULL || length == 0) {
    return 0;
  }
  while (*at != '\0') {
    const char* end = at;
    while (*end != '\0' && *end != ' ') {
      ++end;
    }
    if ((uint64_t)(end - at) == length && memcmp(at, name, (size_t)length) == 0) {
      return 1;
    }
    at = *end == ' ' ? end + 1 : end;
  }
  return 0;
}

static uint32_t mock_has_cap(const char* name, uint64_t name_length) {
  uint32_t has;
  ensure_ready();
  has = (uint32_t)has_cap_named(name, name_length);
  record(GRACEUI_MOCK_HAS_CAP, has, 0, 0, has, name, name != NULL ? name_length : 0);
  return has;
}

/* 1 if `text` is `least` to `most` of a-z, 0-9 and '-': a mod's id (3 to 32) or a hotkey's action (1 to 32). */
static int valid_name(const char* text, uint64_t length, uint64_t least, uint64_t most) {
  uint64_t i;
  if (text == NULL || length < least || length > most) {
    return 0;
  }
  for (i = 0; i < length; ++i) {
    char c = text[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return 0;
    }
  }
  return 1;
}

static GraceUiStatus mock_connect(const GraceUiConnectRequest* request, GraceUiConnectAnswer* answer) {
  GraceUiConnectAnswer out;
  GraceUiStatus status;
  uint32_t i;
  ensure_ready();
  memset(&out, 0, sizeof out);
  out.graceui_version = g.version;
  out.abi_minor = g.minor;
  if (answer == NULL || !fits(answer->size, offsetof(GraceUiConnectAnswer, ui_scale))) {
    return record(GRACEUI_MOCK_CONNECT, GRACEUI_REFUSED, 0, 0, 0, NULL, 0); /* nowhere to say why */
  }
  if (failing(GRACEUI_MOCK_CONNECT, &status)) {
    out.status = status;
  } else if (request == NULL || !fits(request->size, sizeof(GraceUiConnectRequest))) {
    out.status = GRACEUI_REFUSED;
    out.reason = GRACEUI_REFUSED_ARGUMENT;
  } else if (!valid_name(request->id, request->id_length, 3, GRACEUI_ID_MAX)) {
    out.status = GRACEUI_REFUSED;
    out.reason = GRACEUI_REFUSED_ID;
  } else if (request->name == NULL || request->name_length == 0 || request->name_length > GRACEUI_NAME_MAX) {
    out.status = GRACEUI_REFUSED;
    out.reason = GRACEUI_REFUSED_NAME;
  } else if (request->caps_length > GRACEUI_CAPS_MAX || (request->caps_length != 0 && request->caps == NULL)) {
    out.status = GRACEUI_REFUSED;
    out.reason = GRACEUI_REFUSED_CAPS;
  } else if (request->floor > g.version) {
    out.status = GRACEUI_TOO_OLD;
    out.floor = request->floor;
  } else {
    out.status = GRACEUI_OK;
    for (i = 0; i < g.mod_count; ++i) {
      if (g.mods[i].connected && strlen(g.mods[i].id) == request->id_length &&
          memcmp(g.mods[i].id, request->id, (size_t)request->id_length) == 0) {
        out.status = GRACEUI_DUPLICATE_ID;
      }
    }
    if (out.status == GRACEUI_OK && g.mod_count == GRACEUI_MODS_MAX) {
      out.status = GRACEUI_REFUSED;
      out.reason = GRACEUI_REFUSED_TOO_MANY;
    }
    if (out.status == GRACEUI_OK) {
      MockMod* mod = &g.mods[g.mod_count++];
      memset(mod, 0, sizeof *mod);
      mod->handle = g.mod_count; /* 1, 2, ...: never 0, never reused within a test */
      mod->connected = 1;
      mod->abi_minor = request->abi_minor;
      memcpy(mod->id, request->id, (size_t)request->id_length);
      out.consumer = mod->handle;
      if (g.inactive != 0) {
        out.status = GRACEUI_INACTIVE;
        out.reason = g.inactive;
      }
      out.ui_scale = 1.0f;
      out.ui_scale_default = 1.0f;
      out.display_width = 1920;
      out.display_height = 1080;
    }
  }
  out.size = answer->size;
  memcpy(answer, &out, answer->size < sizeof out ? answer->size : sizeof out);
  return record(GRACEUI_MOCK_CONNECT, out.status, out.consumer, 0, request != NULL ? request->abi_minor : 0,
                request != NULL ? request->id : NULL, request != NULL && request->id != NULL ? request->id_length : 0);
}

static void drop_queue(MockMod* mod) {
  uint32_t i;
  for (i = 0; i < mod->queued; ++i) {
    free(mod->queue[i].frame);
  }
  mod->queued = 0;
}

static GraceUiStatus mock_disconnect(GraceUiConsumer consumer) {
  MockMod* mod;
  uint32_t i;
  GraceUiStatus status = gate(GRACEUI_MOCK_DISCONNECT, consumer, &mod, 1);
  if (status == GRACEUI_OK) {
    drop_queue(mod);
    mod->connected = 0;
    if (g.holder == consumer) {
      g.holder = 0;
    }
    for (i = 0; i < g.panel_count; ++i) {
      if (g.panels[i].owner == consumer) {
        g.panels[i].shown = 0;
        g.panels[i].hook.callback = NULL;
      }
    }
  }
  return record(GRACEUI_MOCK_DISCONNECT, status, consumer, 0, 0, NULL, 0);
}

static GraceUiStatus mock_register_pages(GraceUiConsumer consumer, const GraceUiSpaceDesc* space) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_PAGES, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (space == NULL || !fits(space->size, sizeof(GraceUiSpaceDesc)) || space->pages == NULL ||
        space->pages_length == 0 || space->settings == NULL || space->settings_length == 0 ||
        (space->views & (GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER)) == 0 ||
        (space->views & ~(uint32_t)(GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER)) != 0) {
      status = GRACEUI_REFUSED;
    } else if (mod->registered_pages) {
      status = GRACEUI_DUPLICATE_ID;
    } else {
      mod->registered_pages = 1;
    }
  }
  return record(GRACEUI_MOCK_REGISTER_PAGES, status, consumer, space != NULL ? space->views : 0, 0,
                status == GRACEUI_OK ? space->pages : NULL, status == GRACEUI_OK ? space->pages_length : 0);
}

static GraceUiStatus mock_publish(GraceUiConsumer consumer, const char* frame, uint64_t frame_length) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_PUBLISH, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (!mod->registered_pages) {
      status = GRACEUI_NOT_YET;
    } else if (frame == NULL || frame_length == 0 || frame_length > GRACEUI_FRAME_TO_PAGE_MAX) {
      status = GRACEUI_REFUSED;
    }
  }
  return record(GRACEUI_MOCK_PUBLISH, status, consumer, 0, 0, frame, frame != NULL ? frame_length : 0);
}

/* The size an event must have to be given a kind (graceui.h's GraceUiEvent), and whether the mod is old enough. */
static int holds(const MockMod* mod, const GraceUiEvent* event, uint32_t kind) {
  switch (kind) {
    case GRACEUI_EVENT_HOTKEY:
      return 1;
    case GRACEUI_EVENT_WATCHED:
    case GRACEUI_EVENT_UNWATCHED:
    case GRACEUI_EVENT_COMMAND:
      return fits(event->size, offsetof(GraceUiEvent, key));
    case GRACEUI_EVENT_DISPLAY:
      return fits(event->size, offsetof(GraceUiEvent, file));
    case GRACEUI_EVENT_HOTKEY_CHANGED:
      return mod->abi_minor >= 4;
    default: /* a holder's events, ABI 1.1 */
      return fits(event->size, offsetof(GraceUiEvent, display_width));
  }
}

static GraceUiStatus mock_poll(GraceUiConsumer consumer, GraceUiEvent* event) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_POLL, consumer, &mod, 1);
  uint32_t kind = GRACEUI_EVENT_NONE;
  if (status == GRACEUI_OK && (event == NULL || !fits(event->size, offsetof(GraceUiEvent, reply)))) {
    status = GRACEUI_REFUSED;
  }
  if (status == GRACEUI_OK) {
    uint32_t i;
    GraceUiEvent out;
    uint32_t size = event->size < sizeof out ? event->size : (uint32_t)sizeof out;
    memset(&out, 0, sizeof out);
    memcpy(&out, event, size); /* the mod's own buffers and sizes, kept */
    out.kind = GRACEUI_EVENT_NONE;
    for (i = 0; i < mod->queued; ++i) {
      const MockEvent* next = &mod->queue[i];
      if (!holds(mod, event, next->event.kind)) {
        continue; /* passed by: it waits for a poll that can hold it */
      }
      if (next->frame != NULL) {
        out.frame_length = next->frame_length;
        if (out.frame == NULL || out.frame_capacity < next->frame_length) {
          status = GRACEUI_FULL; /* kept for the next call, frame_length the size needed */
          kind = next->event.kind;
          out.kind = next->event.kind;
          break;
        }
        memcpy(out.frame, next->frame, (size_t)next->frame_length);
      }
      {
        char* frame = out.frame;
        uint64_t capacity = out.frame_capacity;
        uint64_t length = out.frame_length;
        uint8_t* file = out.file;
        uint64_t file_capacity = out.file_capacity;
        uint32_t own_size = out.size;
        memcpy(&out, &next->event, sizeof out);
        out.size = own_size;
        out.frame = frame;
        out.frame_capacity = capacity;
        out.frame_length = next->frame != NULL ? length : 0;
        out.file = file;
        out.file_capacity = file_capacity;
        out.file_length = 0;
      }
      kind = out.kind;
      free(next->frame);
      memmove(&mod->queue[i], &mod->queue[i + 1], (mod->queued - i - 1) * sizeof mod->queue[0]);
      --mod->queued;
      break;
    }
    memcpy(event, &out, size);
  }
  return record(GRACEUI_MOCK_POLL, status, consumer, kind, 0, NULL, 0);
}

static GraceUiStatus mock_answer(GraceUiConsumer consumer, uint64_t reply, const char* frame, uint64_t frame_length) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_ANSWER, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    MockReply* known = reply >= 2 && reply - 2 < g.reply_count ? &g.replies[reply - 2] : NULL;
    if (known == NULL || known->owner != consumer || known->answered || frame == NULL || frame_length == 0 ||
        frame_length > GRACEUI_FRAME_TO_PAGE_MAX) {
      status = GRACEUI_REFUSED;
    } else {
      known->answered = 1;
      known->frame = copy_text(frame, frame_length);
    }
  }
  return record(GRACEUI_MOCK_ANSWER, status, consumer, reply, 0, frame, frame != NULL ? frame_length : 0);
}

static GraceUiStatus mock_register_texture(GraceUiConsumer consumer, const GraceUiTextureDesc* texture,
                                           uint64_t* texture_id) {
  MockMod* mod;
  uint64_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_TEXTURE, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (texture == NULL || texture_id == NULL || !fits(texture->size, sizeof(GraceUiTextureDesc)) ||
        texture->pixels == NULL || texture->width == 0 || texture->height == 0 ||
        texture->width > GRACEUI_TEXTURE_SIDE_MAX || texture->height > GRACEUI_TEXTURE_SIDE_MAX ||
        texture->row_bytes < texture->width * 4u) {
      status = GRACEUI_REFUSED;
    } else if (mod->window_closed) {
      status = GRACEUI_TOO_LATE;
    } else if (mod->textures == GRACEUI_TEXTURES_PER_MOD || g.texture_count == GRACEUI_TEXTURES_MAX) {
      status = GRACEUI_FULL;
    } else {
      ++mod->textures;
      id = ++g.texture_count;
      *texture_id = id;
    }
  }
  return record(GRACEUI_MOCK_REGISTER_TEXTURE, status, consumer, id, 0, NULL, 0);
}

static GraceUiStatus mock_register_face(GraceUiConsumer consumer, const GraceUiFaceDesc* face, uint32_t* face_id) {
  MockMod* mod;
  uint32_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_FACE, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    uint32_t most = strcmp(mod->id, "eldennet") == 0 ? GRACEUI_FACES_ELDENNET : GRACEUI_FACES_PER_MOD;
    if (face == NULL || face_id == NULL || !fits(face->size, offsetof(GraceUiFaceDesc, flags)) ||
        face->data == NULL || face->data_length == 0 || face->data_length > GRACEUI_FACE_BYTES_MAX ||
        face->pixel_height == 0) {
      status = GRACEUI_REFUSED;
    } else if (mod->faces == most || g.face_count == GRACEUI_FACES_ALL_MODS) {
      status = GRACEUI_FULL;
    } else {
      ++mod->faces;
      id = ++g.face_count;
      *face_id = id;
    }
  }
  return record(GRACEUI_MOCK_REGISTER_FACE, status, consumer, id, 0, NULL, 0);
}

static GraceUiStatus mock_glyph_table(GraceUiConsumer consumer, uint32_t face_id, GraceUiGlyphTable* table) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_GLYPH_TABLE, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (table == NULL || !fits(table->size, sizeof(GraceUiGlyphTable)) || face_id == 0 || face_id > g.face_count) {
      status = GRACEUI_REFUSED;
    } else if (!g.glyph_ok) {
      status = GRACEUI_NOT_READY;
    } else {
      GraceUiGlyphTable out = g.glyphs;
      out.size = table->size;
      out.face_id = face_id;
      *table = out;
    }
  }
  return record(GRACEUI_MOCK_GLYPH_TABLE, status, consumer, face_id, 0, NULL, 0);
}

static MockPanel* panel_of(GraceUiConsumer consumer, uint64_t panel_id) {
  return panel_id >= 1 && panel_id <= g.panel_count && g.panels[panel_id - 1].owner == consumer
             ? &g.panels[panel_id - 1]
             : NULL;
}

static GraceUiStatus mock_create_panel(GraceUiConsumer consumer, const GraceUiPanelDesc* panel, uint64_t* panel_id) {
  MockMod* mod;
  uint64_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_CREATE_PANEL, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (panel == NULL || panel_id == NULL || !fits(panel->size, sizeof(GraceUiPanelDesc)) || !(panel->width >= 1.0f) ||
        !(panel->height >= 1.0f) || panel->width > 16384.0f || panel->height > 16384.0f) {
      status = GRACEUI_REFUSED;
    } else if (mod->panels == GRACEUI_PANELS_PER_MOD || g.panel_count == MOCK_PANELS_MAX) {
      status = GRACEUI_FULL;
    } else {
      MockPanel* made = &g.panels[g.panel_count++];
      memset(made, 0, sizeof *made);
      made->owner = consumer;
      ++mod->panels;
      id = g.panel_count;
      *panel_id = id;
    }
  }
  return record(GRACEUI_MOCK_CREATE_PANEL, status, consumer, id, 0, NULL, 0);
}

/* A list's counts against GraceUI's limits (D76): the mock checks no vertex, index or command itself. */
static int list_in_bounds(const GraceUiDrawList* list) {
  if (list == NULL || !fits(list->size, sizeof(GraceUiDrawList))) {
    return 0;
  }
  if ((uint64_t)list->vertex_count * sizeof(GraceUiDrawVert) > GRACEUI_DRAW_VERTEX_BYTES_MAX ||
      (uint64_t)list->index_count * sizeof(uint16_t) > GRACEUI_DRAW_INDEX_BYTES_MAX ||
      list->command_count > GRACEUI_DRAW_COMMANDS_MAX || list->command_size > GRACEUI_DRAW_COMMAND_SIZE_MAX) {
    return 0;
  }
  return (list->vertex_count == 0 || list->vertices != NULL) && (list->index_count == 0 || list->indices != NULL) &&
         (list->command_count == 0 || (list->commands != NULL && list->command_size != 0));
}

static GraceUiStatus mock_submit(GraceUiConsumer consumer, uint64_t panel_id, const GraceUiDrawList* list) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_SUBMIT, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (panel_of(consumer, panel_id) == NULL || !list_in_bounds(list)) {
      status = GRACEUI_REFUSED;
    } else {
      mod->window_closed = 1;
    }
  }
  return record(GRACEUI_MOCK_SUBMIT, status, consumer, panel_id, list != NULL ? list->vertex_count : 0, NULL, 0);
}

static GraceUiStatus show(uint32_t slot, GraceUiConsumer consumer, uint64_t panel_id, uint32_t shown) {
  MockMod* mod;
  GraceUiStatus status = gate(slot, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    MockPanel* panel = panel_of(consumer, panel_id);
    if (panel == NULL) {
      status = GRACEUI_REFUSED;
    } else {
      panel->shown = shown;
    }
  }
  return record(slot, status, consumer, panel_id, shown, NULL, 0);
}

static GraceUiStatus mock_show_panel(GraceUiConsumer consumer, uint64_t panel_id) {
  return show(GRACEUI_MOCK_SHOW_PANEL, consumer, panel_id, 1);
}

static GraceUiStatus mock_hide_panel(GraceUiConsumer consumer, uint64_t panel_id) {
  return show(GRACEUI_MOCK_HIDE_PANEL, consumer, panel_id, 0);
}

static GraceUiStatus mock_request_input(GraceUiConsumer consumer, const GraceUiInputRequest* request,
                                        GraceUiInputAnswer* answer) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_REQUEST_INPUT, consumer, &mod, 0);
  uint32_t flags = 0;
  if (status == GRACEUI_OK) {
    if (request == NULL || answer == NULL || !fits(request->size, offsetof(GraceUiInputRequest, flags)) ||
        !fits(answer->size, sizeof(GraceUiInputAnswer)) ||
        (request->kind != GRACEUI_HOLDER_PANEL && request->kind != GRACEUI_HOLDER_MODAL) ||
        panel_of(consumer, request->panel) == NULL) {
      status = GRACEUI_REFUSED;
    } else {
      GraceUiInputAnswer out;
      memset(&out, 0, sizeof out);
      out.size = answer->size;
      flags = fits(request->size, sizeof(GraceUiInputRequest)) ? request->flags : 0;
      if (g.holder != 0 && g.holder != consumer) {
        const MockMod* holder = mod_of(g.holder);
        out.answer = GRACEUI_INPUT_REFUSED;
        out.holder_kind = GRACEUI_HOLDER_PANEL;
        if (holder != NULL) {
          memcpy(out.holder, holder->id, strlen(holder->id) + 1);
        }
      } else if (g.holder == 0 && g.input_answer != GRACEUI_INPUT_GRANTED) {
        out.answer = g.input_answer;
        out.why = g.input_answer == GRACEUI_INPUT_NOT_NOW ? g.input_why : 0;
        if (g.input_answer == GRACEUI_INPUT_REFUSED) {
          out.holder_kind = strcmp(g.input_holder_name, "graceui") == 0 ? GRACEUI_HOLDER_MENU : GRACEUI_HOLDER_PANEL;
          memcpy(out.holder, g.input_holder_name, sizeof out.holder);
        }
      } else {
        out.answer = GRACEUI_INPUT_GRANTED;
        g.holder = consumer;
      }
      memcpy(answer, &out, sizeof out);
    }
  }
  return record(GRACEUI_MOCK_REQUEST_INPUT, status, consumer, request != NULL ? request->panel : 0, flags, NULL, 0);
}

static GraceUiStatus mock_release_input(GraceUiConsumer consumer) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_RELEASE_INPUT, consumer, &mod, 0);
  if (status == GRACEUI_OK && g.holder == consumer) {
    g.holder = 0;
  }
  return record(GRACEUI_MOCK_RELEASE_INPUT, status, consumer, 0, 0, NULL, 0);
}

static GraceUiStatus mock_register_hotkey(GraceUiConsumer consumer, const GraceUiHotkeyDesc* hotkey,
                                          uint32_t* hotkey_id) {
  MockMod* mod;
  uint32_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_HOTKEY, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (hotkey == NULL || hotkey_id == NULL || !fits(hotkey->size, sizeof(GraceUiHotkeyDesc)) ||
        !valid_name(hotkey->action, hotkey->action_length, 1, 32) || hotkey->name == NULL ||
        hotkey->name_length == 0 || hotkey->name_length > 64 || hotkey->key == 0x1B /* Escape */) {
      status = GRACEUI_REFUSED;
    } else if (hotkey->pad_chord != 0 && mod->abi_minor < 3) {
      status = GRACEUI_NOT_YET;
    } else if (mod->hotkeys == 8u || g.hotkey_count == MOCK_HOTKEYS_MAX) {
      status = GRACEUI_FULL;
    } else {
      MockHotkey* made = &g.hotkeys[g.hotkey_count++];
      uint32_t i;
      memset(made, 0, sizeof *made);
      made->owner = consumer;
      made->key = hotkey->key;
      made->modifiers = hotkey->modifiers;
      made->pad_chord = hotkey->pad_chord;
      made->enabled = 1;
      made->bound = (hotkey->key != 0 ? GRACEUI_HOTKEY_KEY_BOUND : 0) |
                    (hotkey->pad_chord != 0 ? GRACEUI_HOTKEY_CHORD_BOUND : 0);
      for (i = 0; i + 1 < g.hotkey_count; ++i) { /* a default another hotkey has already is not bound (D164) */
        const MockHotkey* other = &g.hotkeys[i];
        if (hotkey->key != 0 && other->key == hotkey->key && other->modifiers == hotkey->modifiers &&
            (other->bound & GRACEUI_HOTKEY_KEY_BOUND) != 0) {
          made->bound &= ~(uint32_t)GRACEUI_HOTKEY_KEY_BOUND;
        }
        if (hotkey->pad_chord != 0 && other->pad_chord == hotkey->pad_chord &&
            (other->bound & GRACEUI_HOTKEY_CHORD_BOUND) != 0) {
          made->bound &= ~(uint32_t)GRACEUI_HOTKEY_CHORD_BOUND;
        }
      }
      ++mod->hotkeys;
      id = g.hotkey_count;
      *hotkey_id = id;
      status = made->bound != 0 ? GRACEUI_OK : GRACEUI_UNBOUND;
    }
  }
  return record(GRACEUI_MOCK_REGISTER_HOTKEY, status, consumer, id, hotkey != NULL ? hotkey->key : 0,
                hotkey != NULL ? hotkey->action : NULL, hotkey != NULL && hotkey->action != NULL ? hotkey->action_length : 0);
}

static MockHotkey* hotkey_of(GraceUiConsumer consumer, uint32_t hotkey_id) {
  return hotkey_id >= 1 && hotkey_id <= g.hotkey_count && g.hotkeys[hotkey_id - 1].owner == consumer
             ? &g.hotkeys[hotkey_id - 1]
             : NULL;
}

static GraceUiStatus mock_notice(GraceUiConsumer consumer, const GraceUiNotice* notice) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_NOTICE, consumer, &mod, 1);
  if (status == GRACEUI_OK) {
    if (notice == NULL || !fits(notice->size, sizeof(GraceUiNotice)) || notice->text_length > GRACEUI_NOTICE_TEXT_MAX ||
        (notice->text_length == 0 && notice->key == 0) || (notice->text_length != 0 && notice->text == NULL) ||
        notice->tone > 2u) {
      status = GRACEUI_REFUSED;
    }
  }
  return record(GRACEUI_MOCK_NOTICE, status, consumer, notice != NULL ? notice->key : 0,
                notice != NULL ? notice->tone : 0, notice != NULL ? notice->text : NULL,
                notice != NULL && notice->text != NULL ? notice->text_length : 0);
}

static GraceUiStatus mock_set_world_state(GraceUiConsumer consumer, const GraceUiWorldState* state) {
  MockMod* mod;
  const uint32_t known = GRACEUI_WORLD_DANGER | GRACEUI_WORLD_LOADING | GRACEUI_WORLD_GAME_MENU | GRACEUI_WORLD_TITLE |
                         GRACEUI_WORLD_MULTIPLAYER | GRACEUI_WORLD_ANTI_CHEAT;
  GraceUiStatus status = gate(GRACEUI_MOCK_SET_WORLD_STATE, consumer, &mod, 0);
  if (status == GRACEUI_OK && (state == NULL || !fits(state->size, sizeof(GraceUiWorldState)) ||
                               (state->flags & ~known) != 0)) {
    status = GRACEUI_REFUSED;
  }
  return record(GRACEUI_MOCK_SET_WORLD_STATE, status, consumer, state != NULL ? state->flags : 0, 0, NULL, 0);
}

static GraceUiStatus mock_log(GraceUiConsumer consumer, uint32_t level, const char* text, uint64_t text_length) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_LOG, consumer, &mod, 1);
  if (status == GRACEUI_OK && (text == NULL || text_length == 0 || level > GRACEUI_LOG_ERROR)) {
    status = GRACEUI_REFUSED;
  }
  return record(GRACEUI_MOCK_LOG, status, consumer, level, 0, text, text != NULL ? text_length : 0);
}

static GraceUiStatus mock_set_caret(GraceUiConsumer consumer, const GraceUiCaret* caret) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_SET_CARET, consumer, &mod, 0);
  if (status == GRACEUI_OK &&
      (caret == NULL || !fits(caret->size, sizeof(GraceUiCaret)) || panel_of(consumer, caret->panel) == NULL)) {
    status = GRACEUI_REFUSED;
  }
  return record(GRACEUI_MOCK_SET_CARET, status, consumer, caret != NULL ? caret->panel : 0, 0, NULL, 0);
}

static GraceUiStatus mock_describe_hotkey(GraceUiConsumer consumer, uint32_t hotkey_id, GraceUiHotkeyBinding* binding) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_DESCRIBE_HOTKEY, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    const MockHotkey* hotkey = hotkey_of(consumer, hotkey_id);
    if (hotkey == NULL || binding == NULL || !fits(binding->size, sizeof(GraceUiHotkeyBinding))) {
      status = GRACEUI_REFUSED;
    } else {
      GraceUiHotkeyBinding out; /* the defaults it registered: the mock has no Hotkeys page to rebind them, and no
                                   key names */
      memset(&out, 0, sizeof out);
      out.size = binding->size;
      out.bound = hotkey->bound;
      out.key = (hotkey->bound & GRACEUI_HOTKEY_KEY_BOUND) != 0 ? hotkey->key : 0;
      out.modifiers = (hotkey->bound & GRACEUI_HOTKEY_KEY_BOUND) != 0 ? hotkey->modifiers : 0;
      out.pad_chord = (hotkey->bound & GRACEUI_HOTKEY_CHORD_BOUND) != 0 ? hotkey->pad_chord : 0;
      memcpy(binding, &out, sizeof out);
    }
  }
  return record(GRACEUI_MOCK_DESCRIBE_HOTKEY, status, consumer, hotkey_id, 0, NULL, 0);
}

static GraceUiStatus mock_wake_event(GraceUiConsumer consumer, void** event) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_WAKE_EVENT, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    status = event == NULL ? GRACEUI_REFUSED : GRACEUI_NOT_YET; /* no event to wait on: a test polls */
  }
  if (event != NULL) {
    *event = NULL;
  }
  return record(GRACEUI_MOCK_WAKE_EVENT, status, consumer, 0, 0, NULL, 0);
}

static GraceUiStatus mock_set_on_present(GraceUiConsumer consumer, uint64_t panel_id, const GraceUiPresentHook* hook) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_SET_ON_PRESENT, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    MockPanel* panel = panel_of(consumer, panel_id);
    if (panel == NULL || hook == NULL || !fits(hook->size, sizeof(GraceUiPresentHook))) {
      status = GRACEUI_REFUSED;
    } else if (hook->callback != NULL &&
               (hook->vertex_capacity == 0 ||
                (uint64_t)hook->vertex_capacity * sizeof(GraceUiDrawVert) > GRACEUI_DRAW_VERTEX_BYTES_MAX ||
                hook->index_capacity == 0 ||
                (uint64_t)hook->index_capacity * sizeof(uint16_t) > GRACEUI_DRAW_INDEX_BYTES_MAX ||
                hook->command_capacity == 0 || hook->command_capacity > GRACEUI_DRAW_COMMANDS_MAX)) {
      status = GRACEUI_REFUSED;
    } else {
      panel->hook = *hook;
      if (hook->callback != NULL) {
        mod->window_closed = 1;
      }
    }
  }
  return record(GRACEUI_MOCK_SET_ON_PRESENT, status, consumer, panel_id,
                hook != NULL && hook->callback != NULL ? 1u : 0u, NULL, 0);
}

static GraceUiStatus mock_pad_keyboard(GraceUiConsumer consumer, const GraceUiPadKeyboard* keyboard) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_PAD_KEYBOARD, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (keyboard == NULL || !fits(keyboard->size, sizeof(GraceUiPadKeyboard)) ||
        panel_of(consumer, keyboard->panel) == NULL) {
      status = GRACEUI_REFUSED;
    } else if (g.holder != consumer) {
      status = GRACEUI_REFUSED; /* the holder's field alone */
    }
  }
  return record(GRACEUI_MOCK_PAD_KEYBOARD, status, consumer, keyboard != NULL ? keyboard->panel : 0,
                keyboard != NULL ? keyboard->mode : 0, NULL, 0);
}

static GraceUiStatus mock_set_hotkey_enabled(GraceUiConsumer consumer, uint32_t hotkey_id, uint32_t enabled) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_SET_HOTKEY_ENABLED, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    MockHotkey* hotkey = hotkey_of(consumer, hotkey_id);
    if (hotkey == NULL) {
      status = GRACEUI_REFUSED;
    } else {
      hotkey->enabled = enabled != 0;
    }
  }
  return record(GRACEUI_MOCK_SET_HOTKEY_ENABLED, status, consumer, hotkey_id, enabled != 0, NULL, 0);
}

/* ---- The table, cut to a minor's size. */

static uint32_t size_for_minor(uint32_t minor) {
  if (minor == 0) return (uint32_t)offsetof(GraceUiApi1, set_caret);
  if (minor <= 3) return (uint32_t)offsetof(GraceUiApi1, describe_hotkey);
  if (minor == 4) return (uint32_t)offsetof(GraceUiApi1, wake_event);
  if (minor <= 7) return (uint32_t)offsetof(GraceUiApi1, set_on_present);
  if (minor <= 9) return (uint32_t)offsetof(GraceUiApi1, pad_keyboard);
  if (minor == 10) return (uint32_t)offsetof(GraceUiApi1, set_hotkey_enabled);
  return (uint32_t)sizeof(GraceUiApi1);
}

static void build_table(void) {
  GraceUiApi1* t = &g.table;
  memset(t, 0, sizeof *t);
  t->size = size_for_minor(g.minor);
  t->abi_minor = g.minor;
  t->graceui_version = g.version;
  t->has_cap = mock_has_cap;
  t->connect = mock_connect;
  t->disconnect = mock_disconnect;
  t->register_pages = mock_register_pages;
  t->publish = mock_publish;
  t->poll = mock_poll;
  t->answer = mock_answer;
  t->register_texture = mock_register_texture;
  t->register_face = mock_register_face;
  t->glyph_table = mock_glyph_table;
  t->create_panel = mock_create_panel;
  t->submit = mock_submit;
  t->show_panel = mock_show_panel;
  t->hide_panel = mock_hide_panel;
  t->request_input = mock_request_input;
  t->release_input = mock_release_input;
  t->register_hotkey = mock_register_hotkey;
  t->notice = mock_notice;
  t->set_world_state = mock_set_world_state;
  t->log = mock_log;
  if (g.minor >= 1) t->set_caret = mock_set_caret;
  if (g.minor >= 4) t->describe_hotkey = mock_describe_hotkey;
  if (g.minor >= 5) t->wake_event = mock_wake_event;
  if (g.minor >= 8) t->set_on_present = mock_set_on_present;
  if (g.minor >= 10) t->pad_keyboard = mock_pad_keyboard;
  if (g.minor >= 11) t->set_hotkey_enabled = mock_set_hotkey_enabled;
}

/* ---- The mock's own calls. */

void graceui_mock_reset(void) {
  uint32_t i;
  size_t c;
  for (i = 0; i < g.mod_count; ++i) {
    drop_queue(&g.mods[i]);
  }
  for (i = 0; i < g.reply_count; ++i) {
    free(g.replies[i].frame);
  }
  for (c = 0; c < g.call_count; ++c) {
    free(g.calls[c].text);
  }
  free(g.calls);
  memset(&g, 0, sizeof g);
  g.version = GRACEUI_MOCK_VERSION;
  g.minor = GRACEUI_ABI1_MINOR;
  memcpy(g.caps, kAllCaps, sizeof kAllCaps);
  g.input_answer = GRACEUI_INPUT_GRANTED;
  g.ready = 1;
  build_table();
}

const GraceUiApi1* graceui_mock_api(void) {
  ensure_ready();
  return &g.table;
}

void graceui_mock_set_version(uint32_t version) {
  ensure_ready();
  g.version = version;
  build_table();
}

void graceui_mock_set_minor(uint32_t minor) {
  ensure_ready();
  g.minor = minor > GRACEUI_ABI1_MINOR ? GRACEUI_ABI1_MINOR : minor;
  build_table();
}

void graceui_mock_set_caps(const char* caps) {
  ensure_ready();
  if (caps == NULL) {
    memcpy(g.caps, kAllCaps, sizeof kAllCaps);
  } else {
    size_t length = strlen(caps);
    if (length >= sizeof g.caps) {
      length = sizeof g.caps - 1;
    }
    memcpy(g.caps, caps, length);
    g.caps[length] = '\0';
  }
}

void graceui_mock_set_inactive(uint32_t reason) {
  ensure_ready();
  g.inactive = reason;
}

void graceui_mock_fail_next(uint32_t slot, GraceUiStatus status) {
  ensure_ready();
  if (slot >= 1 && slot < GRACEUI_MOCK_SLOTS) {
    g.failing[slot] = 1;
    g.fail_next[slot] = status;
  }
}

void graceui_mock_set_input_answer(uint32_t answer, uint32_t why, const char* holder) {
  ensure_ready();
  g.input_answer = answer;
  g.input_why = why;
  memset(g.input_holder_name, 0, sizeof g.input_holder_name);
  if (holder != NULL) {
    size_t length = strlen(holder);
    memcpy(g.input_holder_name, holder, length < sizeof g.input_holder_name ? length : sizeof g.input_holder_name - 1);
  }
}

void graceui_mock_set_glyph_table(const GraceUiGlyphTable* table) {
  ensure_ready();
  g.glyph_ok = table != NULL;
  if (table != NULL) {
    g.glyphs = *table;
  }
}

static MockEvent* queue_slot(GraceUiConsumer consumer) {
  MockMod* mod;
  ensure_ready();
  mod = mod_of(consumer);
  if (mod == NULL || mod->queued == MOCK_QUEUE_MAX) {
    return NULL;
  }
  memset(&mod->queue[mod->queued], 0, sizeof mod->queue[0]);
  return &mod->queue[mod->queued++];
}

int graceui_mock_push_hotkey(GraceUiConsumer consumer, uint32_t hotkey_id, uint32_t count) {
  MockEvent* slot;
  if (count == 0 || hotkey_of(consumer, hotkey_id) == NULL) {
    return 0;
  }
  slot = queue_slot(consumer);
  if (slot == NULL) {
    return 0;
  }
  slot->event.kind = GRACEUI_EVENT_HOTKEY;
  slot->event.hotkey_id = hotkey_id;
  slot->event.count = count;
  slot->event.hotkey_by = GRACEUI_HOTKEY_BY_KEY;
  return 1;
}

uint64_t graceui_mock_push_frame(GraceUiConsumer consumer, uint32_t kind, const char* frame) {
  MockEvent* slot;
  uint64_t reply = 1;
  if (frame == NULL || (kind != GRACEUI_EVENT_WATCHED && kind != GRACEUI_EVENT_UNWATCHED &&
                        kind != GRACEUI_EVENT_COMMAND)) {
    return 0;
  }
  if (kind == GRACEUI_EVENT_COMMAND && g.reply_count == MOCK_REPLIES_MAX) {
    return 0;
  }
  slot = queue_slot(consumer);
  if (slot == NULL) {
    return 0;
  }
  slot->event.kind = kind;
  slot->frame_length = strlen(frame);
  slot->frame = copy_text(frame, slot->frame_length);
  if (kind == GRACEUI_EVENT_COMMAND) {
    MockReply* made = &g.replies[g.reply_count++];
    memset(made, 0, sizeof *made);
    made->owner = consumer;
    reply = g.reply_count + 1; /* 2, 3, ...: 0 is none and 1 a watch's answer */
    slot->event.reply = reply;
  }
  return reply;
}

int graceui_mock_push_event(GraceUiConsumer consumer, const GraceUiEvent* event) {
  MockEvent* slot;
  uint32_t size;
  if (event == NULL || event->kind == GRACEUI_EVENT_NONE || event->kind == GRACEUI_EVENT_WATCHED ||
      event->kind == GRACEUI_EVENT_UNWATCHED || event->kind == GRACEUI_EVENT_COMMAND) {
    return 0;
  }
  slot = queue_slot(consumer);
  if (slot == NULL) {
    return 0;
  }
  size = event->size < sizeof slot->event ? event->size : (uint32_t)sizeof slot->event;
  memcpy(&slot->event, event, size);
  slot->event.frame = NULL;
  slot->event.frame_capacity = 0;
  slot->event.frame_length = 0;
  slot->event.file = NULL;
  slot->event.file_capacity = 0;
  slot->event.file_length = 0;
  return 1;
}

size_t graceui_mock_call_count(void) {
  return g.call_count;
}

const GraceUiMockCall* graceui_mock_call(size_t index) {
  return index < g.call_count ? &g.calls[index].call : NULL;
}

size_t graceui_mock_count(uint32_t slot) {
  size_t count = 0;
  size_t c;
  for (c = 0; c < g.call_count; ++c) {
    count += g.calls[c].call.slot == slot;
  }
  return count;
}

const GraceUiMockCall* graceui_mock_last(uint32_t slot) {
  size_t c = g.call_count;
  while (c > 0) {
    --c;
    if (g.calls[c].call.slot == slot) {
      return &g.calls[c].call;
    }
  }
  return NULL;
}

const char* graceui_mock_answer_to(uint64_t reply) {
  return reply >= 2 && reply - 2 < g.reply_count && g.replies[reply - 2].answered ? g.replies[reply - 2].frame : NULL;
}

GraceUiConsumer graceui_mock_input_holder(void) {
  return g.holder;
}

uint32_t graceui_mock_panel_shown(uint64_t panel_id) {
  return panel_id >= 1 && panel_id <= g.panel_count ? g.panels[panel_id - 1].shown : 0;
}

uint32_t graceui_mock_hotkey_enabled(uint32_t hotkey_id) {
  return hotkey_id >= 1 && hotkey_id <= g.hotkey_count ? g.hotkeys[hotkey_id - 1].enabled : 0;
}

uint32_t graceui_mock_present(uint64_t panel_id, const GraceUiPresentFrame* frame, GraceUiDrawList* list) {
  GraceUiPresentFrame standard;
  const MockPanel* panel = panel_id >= 1 && panel_id <= g.panel_count ? &g.panels[panel_id - 1] : NULL;
  uint32_t answer;
  if (panel == NULL || panel->hook.callback == NULL || list == NULL) {
    return GRACEUI_PRESENT_KEEP;
  }
  if (frame == NULL) {
    memset(&standard, 0, sizeof standard);
    standard.size = sizeof standard;
    standard.frame = 1;
    standard.delta_seconds = 1.0f / 60.0f;
    standard.display_width = 1920;
    standard.display_height = 1080;
    frame = &standard;
  }
  standard = *frame;
  standard.panel = panel_id;
  memset(list, 0, sizeof *list);
  list->size = sizeof *list;
  list->command_size = sizeof(GraceUiDrawCommand);
  answer = panel->hook.callback(panel->hook.user, &standard, list);
  return answer == GRACEUI_PRESENT_DRAW ? GRACEUI_PRESENT_DRAW : GRACEUI_PRESENT_KEEP;
}
