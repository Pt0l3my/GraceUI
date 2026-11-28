/* SPDX-License-Identifier: MIT */
/*
 * graceui_mock.c: the mock of GraceUI's ABI 1 (graceui_mock.h, D285). One mock in the process, its state below; every
 * slot records its call, then answers as GraceUI does for what a mod's test needs (src/graceui/abi_table.cpp and
 * src/grace-host/ are the reference). Pure C99, the standard library alone.
 */
#include "graceui_mock.h"

#include <stdlib.h>
#include <string.h>

#define MOCK_QUEUE_MAX 256u      /* a mod's events waiting, as GraceUI's holder queue holds */
#define MOCK_HOTKEYS_MAX 64u     /* the session's hotkeys (GraceUI's GRACEUI_HOTKEYS_MAX, EST) */
#define MOCK_HOTKEY_IDS 1024u    /* hotkeys registered in one test, the disconnected ones' among them */
#define MOCK_PANEL_IDS 1024u     /* panels created in one test */
#define MOCK_REPLIES_MAX 1024u   /* commands queued in one test */
#define MOCK_SPACES_MAX 16u      /* mods' spaces in a session (EST) */
#define MOCK_CALLS_PER_CHUNK 1024u
#define MOCK_CHUNKS_MAX 4096u    /* 4,194,304 calls in one test */
#define MOCK_CAPS_MAX 1024u
#define MOCK_FIRST_FACE 16u      /* mods' faces after GraceUI's own (grace-abi's kFirstModFace) */
#define MOCK_COMMAND_SIZE_10 40u /* GraceUiDrawCommand's size at ABI 1.0: the least stride GraceUI reads */
#define MOCK_TEXTURE_BYTES_ALL (64ull << 20)
#define MOCK_PAGES_MAX (1ull << 20) /* a space's page data or settings description (EST) */
#define MOCK_LOG_TEXT_MAX 4096u  /* the most of a log line the mock keeps */
#define MOCK_MENU_KEY 0x74u      /* F5, GraceUI's menu key by default: taken for every mod's hotkey */
#define MOCK_CHORD_BUTTONS 0xF3FFu /* XInput's buttons; 0x0400 and 0x0800 are none */

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
  uint64_t texture_bytes;
  uint32_t faces;
  uint32_t panels;
  uint32_t hotkeys;
  uint32_t present_hooks;
  uint32_t world;         /* its claims as a world-state provider */
  MockEvent queue[MOCK_QUEUE_MAX];
  uint32_t queued;
} MockMod;

typedef struct MockHotkey {
  GraceUiConsumer owner; /* 0 once its mod disconnected */
  char action[33];
  uint32_t key;
  uint32_t modifiers;
  uint32_t pad_chord;
  uint32_t bound;   /* GRACEUI_HOTKEY_*_BOUND */
  uint32_t enabled;
} MockHotkey;

typedef struct MockPanel {
  GraceUiConsumer owner; /* 0 once its mod disconnected */
  GraceUiPanelDesc desc;
  uint32_t shown;
  GraceUiPresentHook hook; /* its on_present callback, if set */
} MockPanel;

typedef struct MockReply {
  GraceUiConsumer owner;
  uint32_t answered;
  char* frame; /* the result it was answered with, owned */
} MockReply;

typedef struct MockCall {
  GraceUiMockCall call; /* as graceui_mock_call gives it */
  char* text;           /* the copy call.text points to, owned */
} MockCall;

static struct {
  uint32_t ready;
  GraceUiApi1 table;
  uint32_t version;
  uint32_t minor;
  char caps[MOCK_CAPS_MAX];
  uint32_t caps_set; /* the test named the caps: set_minor leaves them */
  uint32_t inactive;
  GraceUiStatus fail_next[GRACEUI_MOCK_SLOTS];
  uint32_t failing[GRACEUI_MOCK_SLOTS];
  uint32_t input_answer;
  uint32_t input_why;
  char input_holder_name[40];
  GraceUiConsumer holder;
  uint64_t holder_panel;
  uint32_t holder_events;
  uint32_t in_present;
  uint32_t warmed_up;
  GraceUiGlyphTable glyphs;
  MockMod mods[GRACEUI_MODS_MAX];
  uint32_t mod_count;
  MockHotkey hotkeys[MOCK_HOTKEY_IDS];
  uint32_t hotkey_count;
  MockPanel panels[MOCK_PANEL_IDS];
  uint32_t panel_count;
  uint32_t textures;
  uint64_t texture_bytes;
  uint32_t face_count;
  uint32_t spaces;
  uint32_t present_hooks;
  MockReply replies[MOCK_REPLIES_MAX];
  uint32_t reply_count;
  MockCall* chunks[MOCK_CHUNKS_MAX]; /* calls in chunks, so that a call's address holds until reset */
  size_t call_count;
} g;

/* Each cap, with the ABI minor that brought it, for a table cut to an older minor. The mock has no wake event, so its
 * cap is named only when a test names it. */
static const struct {
  const char* name;
  uint32_t minor;
} kCaps[] = {
    {GRACEUI_CAP_NOTICES, 0},         {GRACEUI_CAP_PANELS, 0},          {GRACEUI_CAP_TEXTURES, 0},
    {GRACEUI_CAP_FACES, 0},           {GRACEUI_CAP_INPUT, 0},           {GRACEUI_CAP_WORLD_STATE, 0},
    {GRACEUI_CAP_HOTKEYS, 0},         {GRACEUI_CAP_PAGES, 0},           {GRACEUI_CAP_HOLDER_EVENTS, 1},
    {GRACEUI_CAP_DISPLAY, 2},         {GRACEUI_CAP_PAD_CHORDS, 3},      {GRACEUI_CAP_HOTKEY_BINDINGS, 4},
    {GRACEUI_CAP_HOTKEY_BY, 7},       {GRACEUI_CAP_ON_PRESENT, 8},      {GRACEUI_CAP_PLAIN_TEXT, 9},
    {GRACEUI_CAP_PAD_KEYBOARD, 10},   {GRACEUI_CAP_HOTKEY_PAUSE, 11},
};

/* The GraceUI that brought each ABI minor, the version an older table answers (graceui.h's GRACEUI_ABI1_MINOR). */
static const uint32_t kVersionOfMinor[] = {
    GRACEUI_VERSION_PACK(0, 2, 0),  GRACEUI_VERSION_PACK(0, 2, 1),  GRACEUI_VERSION_PACK(0, 2, 2),
    GRACEUI_VERSION_PACK(0, 2, 14), GRACEUI_VERSION_PACK(0, 2, 16), GRACEUI_VERSION_PACK(0, 2, 17),
    GRACEUI_VERSION_PACK(0, 2, 26), GRACEUI_VERSION_PACK(0, 2, 38), GRACEUI_VERSION_PACK(0, 2, 52),
    GRACEUI_VERSION_PACK(0, 2, 61), GRACEUI_VERSION_PACK(0, 2, 69), GRACEUI_VERSION_PACK(0, 2, 72),
};
/* A minor added to graceui.h without its GraceUI here fails to compile. */
typedef char mock_minor_table_is_whole[sizeof kVersionOfMinor / sizeof kVersionOfMinor[0] == GRACEUI_ABI1_MINOR + 1u
                                           ? 1
                                           : -1];

static uint32_t default_version(void) {
#ifdef GRACEUI_MOCK_VERSION
  return GRACEUI_MOCK_VERSION;
#else
  return kVersionOfMinor[GRACEUI_ABI1_MINOR];
#endif
}

static void build_table(void);
static void queue_ended(GraceUiConsumer consumer, uint32_t why);

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

/* A call's text length to keep, if it is within `most` bytes: never read past what a slot would take, whatever length
 * it was told. */
static uint64_t within(const char* text, uint64_t length, uint64_t most) {
  return text != NULL && length <= most ? length : 0;
}

static GraceUiStatus record(uint32_t slot, GraceUiStatus status, GraceUiConsumer consumer, uint64_t id, uint64_t value,
                            const char* text, uint64_t text_length) {
  MockCall* made;
  size_t chunk = g.call_count / MOCK_CALLS_PER_CHUNK;
  if (chunk == MOCK_CHUNKS_MAX) {
    abort(); /* a test that never resets its mock */
  }
  if (g.chunks[chunk] == NULL) {
    g.chunks[chunk] = calloc(MOCK_CALLS_PER_CHUNK, sizeof(MockCall));
    if (g.chunks[chunk] == NULL) {
      abort();
    }
  }
  made = &g.chunks[chunk][g.call_count % MOCK_CALLS_PER_CHUNK];
  ++g.call_count;
  made->call.slot = slot;
  made->call.status = status;
  made->call.consumer = consumer;
  made->call.id = id;
  made->call.value = value;
  made->call.text_length = text != NULL ? text_length : 0;
  made->text = copy_text(text, made->call.text_length);
  made->call.text = made->text;
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

/* The common gate of a slot that names a mod, as GraceUI's: refused from inside an on_present callback, not connected
 * for an unknown handle, inactive in the notice-only mode unless the slot still works there. GRACEUI_OK to go on. */
static GraceUiStatus gate(uint32_t slot, GraceUiConsumer consumer, MockMod** mod, int works_inactive) {
  GraceUiStatus status;
  ensure_ready();
  *mod = mod_of(consumer);
  if (failing(slot, &status)) {
    return status;
  }
  if (g.in_present) {
    return GRACEUI_REFUSED; /* no slot from inside a callback (D259) */
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

/* The holder ends: its mod told once, with why, if it asked for its holder's events (0: its mod is gone). */
static void end_holder(uint32_t why) {
  GraceUiConsumer holder = g.holder;
  uint32_t events = g.holder_events;
  g.holder = 0;
  g.holder_panel = 0;
  g.holder_events = 0;
  if (holder != 0 && events && why != 0) {
    queue_ended(holder, why);
  }
}

static uint32_t world_flags(void) {
  uint32_t flags = 0;
  uint32_t i;
  for (i = 0; i < g.mod_count; ++i) {
    if (g.mods[i].connected) {
      flags |= g.mods[i].world;
    }
  }
  return flags;
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
  record(GRACEUI_MOCK_HAS_CAP, has, 0, 0, has, name, within(name, name_length, GRACEUI_CAPS_MAX));
  return has;
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
  } else if (g.in_present || request == NULL || !fits(request->size, sizeof(GraceUiConnectRequest))) {
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
                request != NULL ? request->id : NULL,
                request != NULL ? within(request->id, request->id_length, GRACEUI_ID_MAX) : 0);
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
    if (g.holder == consumer) {
      end_holder(0); /* its mod gone: nothing to tell */
    }
    drop_queue(mod);
    mod->connected = 0;
    if (mod->registered_pages) {
      --g.spaces;
    }
    g.textures -= mod->textures;
    g.texture_bytes -= mod->texture_bytes;
    g.present_hooks -= mod->present_hooks;
    for (i = 0; i < g.panel_count; ++i) {
      if (g.panels[i].owner == consumer) {
        memset(&g.panels[i], 0, sizeof g.panels[i]);
      }
    }
    for (i = 0; i < g.hotkey_count; ++i) {
      if (g.hotkeys[i].owner == consumer) {
        memset(&g.hotkeys[i], 0, sizeof g.hotkeys[i]); /* its defaults free for the next mod */
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
        space->pages_length == 0 || space->pages_length > MOCK_PAGES_MAX || space->settings == NULL ||
        space->settings_length == 0 || space->settings_length > MOCK_PAGES_MAX ||
        (space->views & (GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER)) == 0 ||
        (space->views & ~(uint32_t)(GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER)) != 0) {
      status = GRACEUI_REFUSED;
    } else if (mod->registered_pages) {
      status = GRACEUI_DUPLICATE_ID;
    } else if (g.spaces == MOCK_SPACES_MAX) {
      status = GRACEUI_FULL;
    } else {
      mod->registered_pages = 1;
      ++g.spaces;
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
  return record(GRACEUI_MOCK_PUBLISH, status, consumer, 0, 0, frame,
                within(frame, frame_length, GRACEUI_FRAME_TO_PAGE_MAX));
}

/* An event's class, in the order GraceUI's poll gives them: a holder's, then the display, then hotkeys, then a space's
 * frames (src/grace-host/surfaces.cpp). */
static int class_of(uint32_t kind) {
  switch (kind) {
    case GRACEUI_EVENT_DISPLAY: return 1;
    case GRACEUI_EVENT_HOTKEY:
    case GRACEUI_EVENT_HOTKEY_CHANGED: return 2;
    case GRACEUI_EVENT_WATCHED:
    case GRACEUI_EVENT_UNWATCHED:
    case GRACEUI_EVENT_COMMAND: return 3;
    default: return 0;
  }
}

/* Whether an event the mod's poll handed can hold a kind (graceui.h's GraceUiEvent), and whether the mod is new enough
 * for it. */
static int holds(const MockMod* mod, const GraceUiEvent* event, uint32_t kind) {
  switch (class_of(kind)) {
    case 0: return fits(event->size, offsetof(GraceUiEvent, display_width)); /* ABI 1.1 */
    case 1: return fits(event->size, offsetof(GraceUiEvent, file));          /* ABI 1.2 */
    case 2: return kind == GRACEUI_EVENT_HOTKEY || mod->abi_minor >= 4;
    default: return fits(event->size, offsetof(GraceUiEvent, key));          /* GraceUI 0.2 */
  }
}

static GraceUiStatus mock_poll(GraceUiConsumer consumer, GraceUiEvent* event) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_POLL, consumer, &mod, 0);
  uint32_t kind = GRACEUI_EVENT_NONE;
  if (status == GRACEUI_OK && (event == NULL || !fits(event->size, offsetof(GraceUiEvent, reply)))) {
    status = GRACEUI_REFUSED;
  }
  if (status == GRACEUI_OK) {
    GraceUiEvent out;
    uint32_t size = event->size < sizeof out ? event->size : (uint32_t)sizeof out;
    uint32_t chosen = MOCK_QUEUE_MAX;
    uint32_t i;
    int cls;
    memset(&out, 0, sizeof out);
    memcpy(&out, event, size); /* the mod's own buffers and sizes, kept */
    out.kind = GRACEUI_EVENT_NONE;
    for (cls = 0; cls < 4 && chosen == MOCK_QUEUE_MAX; ++cls) {
      for (i = 0; i < mod->queued; ++i) {
        if (class_of(mod->queue[i].event.kind) == cls && holds(mod, event, mod->queue[i].event.kind)) {
          chosen = i;
          break;
        }
      }
    }
    if (chosen != MOCK_QUEUE_MAX) {
      const MockEvent* next = &mod->queue[chosen];
      char* frame = out.frame;
      uint64_t capacity = out.frame_capacity;
      uint8_t* file = out.file;
      uint64_t file_capacity = out.file_capacity;
      uint32_t own_size = out.size;
      kind = next->event.kind;
      if (next->frame != NULL && (frame == NULL || capacity < next->frame_length)) {
        status = GRACEUI_FULL; /* kept for the next call, frame_length the size needed */
        out.kind = kind;
        out.frame_length = next->frame_length;
      } else {
        memcpy(&out, &next->event, sizeof out);
        out.size = own_size;
        out.frame = frame;
        out.frame_capacity = capacity;
        out.frame_length = 0;
        out.file = file;
        out.file_capacity = file_capacity;
        out.file_length = 0;
        if (next->frame != NULL) {
          memcpy(frame, next->frame, (size_t)next->frame_length);
          out.frame_length = next->frame_length;
        }
        if (g.minor < 7) {
          out.hotkey_by = 0; /* a GraceUI before ABI 1.7 writes nothing there */
        }
        free(next->frame);
        memmove(&mod->queue[chosen], &mod->queue[chosen + 1], (mod->queued - chosen - 1) * sizeof mod->queue[0]);
        --mod->queued;
      }
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
  return record(GRACEUI_MOCK_ANSWER, status, consumer, reply, 0, frame,
                within(frame, frame_length, GRACEUI_FRAME_TO_PAGE_MAX));
}

static GraceUiStatus mock_register_texture(GraceUiConsumer consumer, const GraceUiTextureDesc* texture,
                                           uint64_t* texture_id) {
  MockMod* mod;
  uint64_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_TEXTURE, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    uint64_t bytes = texture != NULL ? (uint64_t)texture->width * texture->height * 4u : 0;
    if (texture == NULL || texture_id == NULL || !fits(texture->size, sizeof(GraceUiTextureDesc)) ||
        texture->format != GRACEUI_TEXTURE_RGBA8 || texture->pixels == NULL || texture->width == 0 ||
        texture->height == 0 || texture->width > GRACEUI_TEXTURE_SIDE_MAX ||
        texture->height > GRACEUI_TEXTURE_SIDE_MAX || texture->row_bytes < texture->width * 4u) {
      status = GRACEUI_REFUSED;
    } else if (mod->window_closed) {
      status = GRACEUI_TOO_LATE;
    } else if (mod->textures == GRACEUI_TEXTURES_PER_MOD || g.textures == GRACEUI_TEXTURES_MAX ||
               mod->texture_bytes + bytes > GRACEUI_TEXTURE_BYTES_PER_MOD ||
               g.texture_bytes + bytes > MOCK_TEXTURE_BYTES_ALL) {
      status = GRACEUI_FULL;
    } else {
      ++mod->textures;
      mod->texture_bytes += bytes;
      ++g.textures;
      g.texture_bytes += bytes;
      id = g.textures;
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
        face->pixel_height < 6 || face->pixel_height > 128 || face->range_count > GRACEUI_FACE_RANGES_MAX ||
        (face->range_count != 0 && face->ranges == NULL)) {
      status = GRACEUI_REFUSED;
    } else if (g.warmed_up) {
      status = GRACEUI_TOO_LATE; /* the shared atlas is baked once, at warm-up */
    } else if (mod->faces == most || g.face_count == GRACEUI_FACES_ALL_MODS) {
      status = GRACEUI_FULL;
    } else {
      ++mod->faces;
      id = MOCK_FIRST_FACE + g.face_count++;
      *face_id = id;
    }
  }
  return record(GRACEUI_MOCK_REGISTER_FACE, status, consumer, id, 0, NULL, 0);
}

static GraceUiStatus mock_glyph_table(GraceUiConsumer consumer, uint32_t face_id, GraceUiGlyphTable* table) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_GLYPH_TABLE, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (table == NULL || !fits(table->size, sizeof(GraceUiGlyphTable)) || face_id < MOCK_FIRST_FACE ||
        face_id >= MOCK_FIRST_FACE + g.face_count) {
      status = GRACEUI_REFUSED;
    } else if (!g.warmed_up) {
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
  return panel_id >= 1 && panel_id <= g.panel_count && consumer != 0 && g.panels[panel_id - 1].owner == consumer
             ? &g.panels[panel_id - 1]
             : NULL;
}

static GraceUiStatus mock_create_panel(GraceUiConsumer consumer, const GraceUiPanelDesc* panel, uint64_t* panel_id) {
  MockMod* mod;
  uint64_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_CREATE_PANEL, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (panel == NULL || panel_id == NULL || !fits(panel->size, sizeof(GraceUiPanelDesc)) ||
        (panel->layer != GRACEUI_LAYER_HUD && panel->layer != GRACEUI_LAYER_PANEL &&
         panel->layer != GRACEUI_LAYER_MODAL) ||
        panel->anchor > GRACEUI_ANCHOR_BOTTOM_RIGHT || (panel->flags & ~(uint32_t)GRACEUI_PANEL_TAKES_INPUT) != 0 ||
        !(panel->width >= 1.0f) || !(panel->height >= 1.0f) || panel->width > 16384.0f ||
        panel->height > 16384.0f || !(panel->x == panel->x) || !(panel->y == panel->y)) {
      status = GRACEUI_REFUSED;
    } else if (mod->panels == GRACEUI_PANELS_PER_MOD || g.panel_count == MOCK_PANEL_IDS) {
      status = GRACEUI_FULL;
    } else {
      uint32_t live = 0;
      uint32_t i;
      for (i = 0; i < g.panel_count; ++i) {
        live += g.panels[i].owner != 0;
      }
      if (live == GRACEUI_PANELS_MAX) {
        status = GRACEUI_FULL;
      } else {
        MockPanel* made = &g.panels[g.panel_count++];
        memset(made, 0, sizeof *made);
        made->owner = consumer;
        made->desc = *panel;
        ++mod->panels;
        id = g.panel_count;
        *panel_id = id;
      }
    }
  }
  return record(GRACEUI_MOCK_CREATE_PANEL, status, consumer, id, 0, NULL, 0);
}

/* A list's shape against GraceUI's bounds (D76, grace-abi's list_shape_fits): the mock checks no vertex, index or
 * command itself. */
static int list_in_bounds(const GraceUiDrawList* list) {
  if (list == NULL || !fits(list->size, sizeof(GraceUiDrawList))) {
    return 0;
  }
  if ((uint64_t)list->vertex_count * sizeof(GraceUiDrawVert) > GRACEUI_DRAW_VERTEX_BYTES_MAX ||
      (uint64_t)list->index_count * sizeof(uint16_t) > GRACEUI_DRAW_INDEX_BYTES_MAX ||
      list->command_count > GRACEUI_DRAW_COMMANDS_MAX || list->command_size < MOCK_COMMAND_SIZE_10 ||
      list->command_size > GRACEUI_DRAW_COMMAND_SIZE_MAX) {
    return 0;
  }
  return (list->vertex_count == 0 || list->vertices != NULL) && (list->index_count == 0 || list->indices != NULL) &&
         (list->command_count == 0 || list->commands != NULL);
}

static GraceUiStatus mock_submit(GraceUiConsumer consumer, uint64_t panel_id, const GraceUiDrawList* list) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_SUBMIT, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    if (panel_of(consumer, panel_id) == NULL) {
      status = GRACEUI_REFUSED;
    } else {
      mod->window_closed = 1; /* a submit to its own panel ends the window, taken or not, as GraceUI's does */
      if (!list_in_bounds(list)) {
        status = GRACEUI_REFUSED;
      }
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
      if (!shown && g.holder == consumer && g.holder_panel == panel_id) {
        end_holder(GRACEUI_CLOSED_RELEASED); /* the holding panel hidden */
      }
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
    const MockPanel* panel = request != NULL ? panel_of(consumer, request->panel) : NULL;
    int modal = request != NULL && request->kind == GRACEUI_HOLDER_MODAL;
    flags = request != NULL && fits(request->size, sizeof(GraceUiInputRequest)) ? request->flags : 0;
    if (request == NULL || answer == NULL || !fits(request->size, offsetof(GraceUiInputRequest, flags)) ||
        !fits(answer->size, sizeof(GraceUiInputAnswer)) ||
        (request->kind != GRACEUI_HOLDER_PANEL && !modal) || request->wants == 0 || (request->wants & ~7u) != 0 ||
        (flags & ~(uint32_t)GRACEUI_INPUT_EVENTS) != 0 || panel == NULL ||
        (panel->desc.flags & GRACEUI_PANEL_TAKES_INPUT) == 0 || modal != (panel->desc.layer == GRACEUI_LAYER_MODAL)) {
      status = GRACEUI_REFUSED;
    } else {
      GraceUiInputAnswer out;
      uint32_t world = world_flags();
      memset(&out, 0, sizeof out);
      out.size = answer->size;
      if (!panel->shown) {
        out.answer = GRACEUI_INPUT_NOT_NOW;
        out.why = GRACEUI_NOT_NOW_NOT_READY; /* a hidden view cannot be shown holding input */
      } else if (g.holder == consumer) {
        out.answer = GRACEUI_INPUT_GRANTED; /* asked again: it still holds it */
      } else if (g.holder != 0) {
        const MockMod* holder = mod_of(g.holder);
        out.answer = GRACEUI_INPUT_REFUSED;
        out.holder_kind = GRACEUI_HOLDER_PANEL;
        if (holder != NULL) {
          memcpy(out.holder, holder->id, strlen(holder->id) + 1);
        }
      } else if (g.input_answer == GRACEUI_INPUT_REFUSED) {
        out.answer = GRACEUI_INPUT_REFUSED;
        out.holder_kind = strcmp(g.input_holder_name, "graceui") == 0 ? GRACEUI_HOLDER_MENU : GRACEUI_HOLDER_PANEL;
        memcpy(out.holder, g.input_holder_name, sizeof out.holder);
      } else if (g.input_answer == GRACEUI_INPUT_NOT_NOW ||
                 (world & (GRACEUI_WORLD_DANGER | GRACEUI_WORLD_LOADING | GRACEUI_WORLD_GAME_MENU)) != 0) {
        out.answer = GRACEUI_INPUT_NOT_NOW;
        if (g.input_answer == GRACEUI_INPUT_NOT_NOW) {
          out.why = g.input_why;
        } else if ((world & GRACEUI_WORLD_DANGER) != 0) {
          out.why = GRACEUI_NOT_NOW_DANGER;
        } else if ((world & GRACEUI_WORLD_LOADING) != 0) {
          out.why = GRACEUI_NOT_NOW_LOADING;
        } else {
          out.why = GRACEUI_NOT_NOW_GAME_MENU;
        }
      } else {
        out.answer = GRACEUI_INPUT_GRANTED;
        g.holder = consumer;
        g.holder_panel = request->panel;
        g.holder_events = (flags & GRACEUI_INPUT_EVENTS) != 0;
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
    end_holder(GRACEUI_CLOSED_RELEASED);
  }
  return record(GRACEUI_MOCK_RELEASE_INPUT, status, consumer, 0, 0, NULL, 0);
}

/* A key no hotkey takes as its own (graceui.h: not Escape, a modifier or a mouse button). */
static int reserved_key(uint32_t key) {
  switch (key) {
    case 0x01: case 0x02: case 0x04: case 0x05: case 0x06: /* the mouse's buttons */
    case 0x10: case 0x11: case 0x12:                       /* Shift, Ctrl, Alt */
    case 0x1B:                                             /* Escape */
    case 0x5B: case 0x5C:                                  /* the Windows keys */
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
      return 1;
    default:
      return key > 0xFE;
  }
}

static int one_button(uint32_t bits) {
  return bits != 0 && (bits & (bits - 1)) == 0 && (bits & ~(uint32_t)MOCK_CHORD_BUTTONS) == 0;
}

static int valid_chord(uint32_t chord) {
  return one_button(chord & 0xFFFFu) && one_button(chord >> 16) && (chord & 0xFFFFu) != chord >> 16;
}

static GraceUiStatus mock_register_hotkey(GraceUiConsumer consumer, const GraceUiHotkeyDesc* hotkey,
                                          uint32_t* hotkey_id) {
  MockMod* mod;
  uint32_t id = 0;
  GraceUiStatus status = gate(GRACEUI_MOCK_REGISTER_HOTKEY, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    const uint32_t modifiers = hotkey != NULL ? hotkey->modifiers : 0;
    const uint32_t chord = hotkey != NULL && g.minor >= 3 ? hotkey->pad_chord : 0; /* before 1.3: not read */
    const int named = hotkey != NULL && hotkey_id != NULL && fits(hotkey->size, sizeof(GraceUiHotkeyDesc)) &&
                      valid_name(hotkey->action, hotkey->action_length, 1, 32);
    int same_action = 0;
    uint32_t i;
    for (i = 0; named && i < g.hotkey_count; ++i) {
      if (g.hotkeys[i].owner == consumer && strlen(g.hotkeys[i].action) == hotkey->action_length &&
          memcmp(g.hotkeys[i].action, hotkey->action, (size_t)hotkey->action_length) == 0) {
        same_action = 1;
      }
    }
    if (!named || same_action || hotkey->name == NULL || hotkey->name_length == 0 || hotkey->name_length > 64 ||
        (modifiers & ~7u) != 0 || reserved_key(hotkey->key) ||
        (hotkey->key == 0x73 && (modifiers & GRACEUI_MOD_ALT) != 0) || (chord != 0 && !valid_chord(chord))) {
      status = GRACEUI_REFUSED;
    } else if (chord != 0 && mod->abi_minor < 3) {
      status = GRACEUI_NOT_YET;
    } else {
      uint32_t live = 0;
      for (i = 0; i < g.hotkey_count; ++i) {
        live += g.hotkeys[i].owner != 0;
      }
      if (mod->hotkeys == GRACEUI_HOTKEYS_PER_MOD || live == MOCK_HOTKEYS_MAX || g.hotkey_count == MOCK_HOTKEY_IDS) {
        status = GRACEUI_FULL;
      } else {
        MockHotkey* made = &g.hotkeys[g.hotkey_count++];
        int key_taken = hotkey->key == 0 || hotkey->key == MOCK_MENU_KEY; /* the menu key, whatever the modifiers */
        int chord_taken = chord == 0;
        for (i = 0; i + 1 < g.hotkey_count; ++i) { /* a default another hotkey has already is not bound (D164) */
          const MockHotkey* other = &g.hotkeys[i];
          if (other->owner == 0) {
            continue;
          }
          if (hotkey->key != 0 && (other->bound & GRACEUI_HOTKEY_KEY_BOUND) != 0 && other->key == hotkey->key &&
              other->modifiers == modifiers) {
            key_taken = 1;
          }
          if (chord != 0 && (other->bound & GRACEUI_HOTKEY_CHORD_BOUND) != 0 && other->pad_chord == chord) {
            chord_taken = 1;
          }
        }
        memset(made, 0, sizeof *made);
        made->owner = consumer;
        memcpy(made->action, hotkey->action, (size_t)hotkey->action_length);
        made->key = hotkey->key;
        made->modifiers = modifiers;
        made->pad_chord = chord;
        made->enabled = 1;
        made->bound = (key_taken ? 0u : GRACEUI_HOTKEY_KEY_BOUND) | (chord_taken ? 0u : GRACEUI_HOTKEY_CHORD_BOUND);
        ++mod->hotkeys;
        id = g.hotkey_count;
        *hotkey_id = id;
        /* unbound when it names no default, or when a default it names is taken (D164, D204) */
        status = (hotkey->key == 0 && chord == 0) || (hotkey->key != 0 && key_taken) || (chord != 0 && chord_taken)
                     ? GRACEUI_UNBOUND
                     : GRACEUI_OK;
      }
    }
  }
  return record(GRACEUI_MOCK_REGISTER_HOTKEY, status, consumer, id, hotkey != NULL ? hotkey->key : 0,
                hotkey != NULL ? hotkey->action : NULL,
                hotkey != NULL ? within(hotkey->action, hotkey->action_length, 32) : 0);
}

static MockHotkey* hotkey_of(GraceUiConsumer consumer, uint32_t hotkey_id) {
  return hotkey_id >= 1 && hotkey_id <= g.hotkey_count && consumer != 0 && g.hotkeys[hotkey_id - 1].owner == consumer
             ? &g.hotkeys[hotkey_id - 1]
             : NULL;
}

static GraceUiStatus mock_notice(GraceUiConsumer consumer, const GraceUiNotice* notice) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_NOTICE, consumer, &mod, 1);
  if (status == GRACEUI_OK && g.inactive != 0 && g.inactive != GRACEUI_INACTIVE_ANTI_CHEAT) {
    status = GRACEUI_INACTIVE; /* nothing is drawn this session; in the notice-only mode notices still show (D88) */
  }
  if (status == GRACEUI_OK) {
    if (notice == NULL || !fits(notice->size, sizeof(GraceUiNotice)) || notice->text_length > GRACEUI_NOTICE_TEXT_MAX ||
        (notice->text_length == 0 && notice->key == 0) || (notice->text_length != 0 && notice->text == NULL) ||
        notice->tone > 2u) {
      status = GRACEUI_REFUSED;
    }
  }
  return record(GRACEUI_MOCK_NOTICE, status, consumer, notice != NULL ? notice->key : 0,
                notice != NULL ? notice->tone : 0, notice != NULL ? notice->text : NULL,
                notice != NULL ? within(notice->text, notice->text_length, GRACEUI_NOTICE_TEXT_MAX) : 0);
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
  if (status == GRACEUI_OK) {
    uint32_t now;
    mod->world = state->flags;
    now = world_flags();
    if ((now & GRACEUI_WORLD_DANGER) != 0) {
      end_holder(GRACEUI_CLOSED_DANGER); /* danger closes every view at once (invariant 9) */
    } else if ((now & GRACEUI_WORLD_LOADING) != 0) {
      end_holder(GRACEUI_CLOSED_LOADING);
    }
  }
  return record(GRACEUI_MOCK_SET_WORLD_STATE, status, consumer, state != NULL ? state->flags : 0, 0, NULL, 0);
}

static GraceUiStatus mock_log(GraceUiConsumer consumer, uint32_t level, const char* text, uint64_t text_length) {
  MockMod* mod;
  GraceUiStatus status = gate(GRACEUI_MOCK_LOG, consumer, &mod, 0);
  if (status == GRACEUI_OK) {
    status = GRACEUI_NOT_YET; /* GraceUI declares log but does not provide it yet: the mock keeps the line all the same */
  }
  return record(GRACEUI_MOCK_LOG, status, consumer, level, 0, text, within(text, text_length, MOCK_LOG_TEXT_MAX));
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
    status = event == NULL ? GRACEUI_REFUSED : GRACEUI_NOT_YET; /* no event to wait on, and no "wake" cap: a test polls */
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
    } else if (hook->callback != NULL && panel->hook.callback == NULL &&
               (mod->present_hooks == GRACEUI_PRESENT_HOOKS_PER_MOD || g.present_hooks == GRACEUI_PRESENT_HOOKS_MAX)) {
      status = GRACEUI_FULL;
    } else {
      if (hook->callback != NULL && panel->hook.callback == NULL) {
        ++mod->present_hooks;
        ++g.present_hooks;
      } else if (hook->callback == NULL && panel->hook.callback != NULL) {
        --mod->present_hooks;
        --g.present_hooks;
      }
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
  if (status == GRACEUI_OK && keyboard != NULL) { /* a null keyboard closes the one open, or none: ok */
    if (!fits(keyboard->size, sizeof(GraceUiPadKeyboard)) || panel_of(consumer, keyboard->panel) == NULL ||
        keyboard->mode > GRACEUI_PAD_KEYBOARD_NUMERIC || !(keyboard->width > 0.0f) || !(keyboard->height > 0.0f) ||
        !(keyboard->x == keyboard->x) || !(keyboard->y == keyboard->y) || g.holder != consumer) {
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

/* ---- The table, cut to a minor's size; the caps a minor's GraceUI names. */

static uint32_t size_for_minor(uint32_t minor) {
  if (minor == 0) return (uint32_t)offsetof(GraceUiApi1, set_caret);
  if (minor <= 3) return (uint32_t)offsetof(GraceUiApi1, describe_hotkey);
  if (minor == 4) return (uint32_t)offsetof(GraceUiApi1, wake_event);
  if (minor <= 7) return (uint32_t)offsetof(GraceUiApi1, set_on_present);
  if (minor <= 9) return (uint32_t)offsetof(GraceUiApi1, pad_keyboard);
  if (minor == 10) return (uint32_t)offsetof(GraceUiApi1, set_hotkey_enabled);
  return (uint32_t)sizeof(GraceUiApi1);
}

static void caps_for_minor(void) {
  size_t at = 0;
  size_t i;
  g.caps[0] = '\0';
  for (i = 0; i < sizeof kCaps / sizeof kCaps[0]; ++i) {
    size_t length = strlen(kCaps[i].name);
    if (kCaps[i].minor > g.minor || at + length + 2 > sizeof g.caps) {
      continue;
    }
    if (at != 0) {
      g.caps[at++] = ' ';
    }
    memcpy(g.caps + at, kCaps[i].name, length + 1);
    at += length;
  }
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
    free(g.chunks[c / MOCK_CALLS_PER_CHUNK][c % MOCK_CALLS_PER_CHUNK].text);
  }
  for (c = 0; c < MOCK_CHUNKS_MAX && g.chunks[c] != NULL; ++c) {
    free(g.chunks[c]);
  }
  memset(&g, 0, sizeof g);
  g.version = default_version();
  g.minor = GRACEUI_ABI1_MINOR;
  caps_for_minor();
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
  g.version = kVersionOfMinor[g.minor];
  if (!g.caps_set) {
    caps_for_minor();
  }
  build_table();
}

void graceui_mock_set_caps(const char* caps) {
  ensure_ready();
  if (caps == NULL) {
    g.caps_set = 0;
    caps_for_minor();
  } else {
    size_t length = strlen(caps);
    if (length >= sizeof g.caps) {
      length = sizeof g.caps - 1;
    }
    memcpy(g.caps, caps, length);
    g.caps[length] = '\0';
    g.caps_set = 1;
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
  g.warmed_up = table != NULL;
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

static void queue_ended(GraceUiConsumer consumer, uint32_t why) {
  MockEvent* slot = queue_slot(consumer);
  if (slot != NULL) {
    slot->event.kind = GRACEUI_EVENT_INPUT_ENDED;
    slot->event.why = why;
  }
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
  if (event == NULL || !fits(event->size, offsetof(GraceUiEvent, hotkey_id)) || event->kind == GRACEUI_EVENT_NONE ||
      event->kind == GRACEUI_EVENT_WATCHED || event->kind == GRACEUI_EVENT_UNWATCHED ||
      event->kind == GRACEUI_EVENT_COMMAND) {
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
  return index < g.call_count ? &g.chunks[index / MOCK_CALLS_PER_CHUNK][index % MOCK_CALLS_PER_CHUNK].call : NULL;
}

size_t graceui_mock_count(uint32_t slot) {
  size_t count = 0;
  size_t c;
  for (c = 0; c < g.call_count; ++c) {
    count += graceui_mock_call(c)->slot == slot;
  }
  return count;
}

const GraceUiMockCall* graceui_mock_last(uint32_t slot) {
  size_t c = g.call_count;
  while (c > 0) {
    --c;
    if (graceui_mock_call(c)->slot == slot) {
      return graceui_mock_call(c);
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
  return hotkey_id >= 1 && hotkey_id <= g.hotkey_count && g.hotkeys[hotkey_id - 1].owner != 0
             ? g.hotkeys[hotkey_id - 1].enabled
             : 0;
}

uint32_t graceui_mock_present(uint64_t panel_id, const GraceUiPresentFrame* frame, GraceUiDrawList* list) {
  GraceUiPresentFrame standard;
  const MockPanel* panel = panel_id >= 1 && panel_id <= g.panel_count ? &g.panels[panel_id - 1] : NULL;
  uint32_t answer;
  if (panel == NULL || panel->owner == 0 || panel->hook.callback == NULL || list == NULL) {
    return GRACEUI_PRESENT_KEEP;
  }
  if (frame == NULL) {
    memset(&standard, 0, sizeof standard);
    standard.size = sizeof standard;
    standard.frame = 1;
    standard.delta_seconds = 1.0f / 60.0f;
    standard.x = panel->desc.x;
    standard.y = panel->desc.y;
    standard.width = panel->desc.width;
    standard.height = panel->desc.height;
    standard.display_width = 1920;
    standard.display_height = 1080;
    frame = &standard;
  }
  standard = *frame;
  standard.panel = panel_id;
  memset(list, 0, sizeof *list);
  list->size = sizeof *list;
  list->command_size = sizeof(GraceUiDrawCommand);
  g.in_present = 1; /* every slot but has_cap refuses a call from inside it, as GraceUI's do */
  answer = panel->hook.callback(panel->hook.user, &standard, list);
  g.in_present = 0;
  if (answer != GRACEUI_PRESENT_DRAW || !list_in_bounds(list) || list->vertex_count > panel->hook.vertex_capacity ||
      list->index_count > panel->hook.index_capacity || list->command_count > panel->hook.command_capacity) {
    return GRACEUI_PRESENT_KEEP; /* GraceUI draws a list that does not fit, or fails its check, as the submitted one */
  }
  return GRACEUI_PRESENT_DRAW;
}
