/* SPDX-License-Identifier: MIT */
/*
 * graceui_test.h: graceui.dll's test-only exports, for tools/grace-harness. They exist only in a build configured with
 * GRACEUI_TEST_EXPORTS=ON, never in a release build (tools/check_safety.py --exports checks a DLL's export table), and
 * they are not part of the public ABI: no mod may call them.
 */
#ifndef GRACEUI_TEST_H
#define GRACEUI_TEST_H

#include <stdint.h>

#include "graceui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The test panel: a filled rectangle of one known colour with a line of white text in it, drawn on the foreground of
 * every frame while it is on, in the swap chain's pixels. */
#define GRACEUI_TEST_PANEL_X0 24
#define GRACEUI_TEST_PANEL_Y0 24
#define GRACEUI_TEST_PANEL_X1 184
#define GRACEUI_TEST_PANEL_Y1 88
#define GRACEUI_TEST_PANEL_R 224
#define GRACEUI_TEST_PANEL_G 64
#define GRACEUI_TEST_PANEL_B 160
#define GRACEUI_TEST_TEXT_X 32
#define GRACEUI_TEST_TEXT_Y 40
#define GRACEUI_TEST_TEXT "GraceUI test panel"

/* The host's state, in the copy that answers. */
enum {
  GRACEUI_TEST_HOST_NOT_ASKED = 0,  /* GraceUI_RequestApi never called */
  GRACEUI_TEST_HOST_STARTED = 1,    /* this copy holds the mutex; its init thread runs */
  GRACEUI_TEST_HOST_HOOKED = 2,     /* the hooks are installed */
  GRACEUI_TEST_HOST_FAILED = 3,     /* the init thread gave up (no window, no vtables, a hook refused) */
  GRACEUI_TEST_HOST_IDLE_COPY = 4   /* another copy holds the mutex: this one idles (D72) */
};

/* How the present queue was found (D116). */
enum {
  GRACEUI_TEST_QUEUE_NONE = 0,
  GRACEUI_TEST_QUEUE_SWAP_CHAIN = 1,  /* the swap chain answered GetDevice for ID3D12CommandQueue */
  GRACEUI_TEST_QUEUE_RESIZE1 = 2,     /* the game named it in ResizeBuffers1 */
  GRACEUI_TEST_QUEUE_PRESENT = 3,     /* a direct queue executed inside the original Present */
  GRACEUI_TEST_QUEUE_ONLY_DIRECT = 4  /* the swap chain's device's only direct queue seen executing */
};

typedef struct GraceUiTestState {
  uint32_t size;                  /* set by the caller to sizeof(GraceUiTestState) */
  uint32_t host;                  /* GRACEUI_TEST_HOST_* */
  uint32_t init_threads_started;  /* init threads this copy started: at most 1 */
  uint32_t hook_install_runs;     /* times the install routine ran: at most 1 */
  uint32_t hooks_installed;       /* hooks enabled by this copy: 5 in the render half */
  uint32_t bound;                 /* 1 while a Direct3D 12 swap chain is bound */
  uint32_t binds;                 /* swap chains bound so far */
  uint32_t queue_method;          /* GRACEUI_TEST_QUEUE_* */
  uint32_t direct_queues_seen;    /* direct queues seen executing, on any device */
  uint32_t renderer_ready;        /* 1 once the context, both backends and the atlas's upload exist */
  uint32_t atlas_ready;           /* 1 once the atlas's upload has completed */
  uint32_t atlas_width;
  uint32_t atlas_height;
  uint32_t render_off;            /* 1 if the render path turned itself off for the session */
  uint32_t resizes;               /* ResizeBuffers and ResizeBuffers1 calls on the bound swap chain */
  uint32_t passed_through;        /* swap chains passed through untouched (not Direct3D 12, or not this process's) */
  uint64_t presents;              /* outermost Present and Present1 calls */
  uint64_t frames_drawn;
  uint64_t frames_skipped_busy;   /* frames not drawn because their slot was still in use by the GPU (never waited) */
  uint64_t allocations;           /* on the present thread, inside the detours, since the last reset */
  uint64_t frees;
} GraceUiTestState;

/* Turns the test panel on (1) or off (0). */
GRACEUI_API void GraceUI_Test_SetPanel(uint32_t on);
/* Fills *out (out->size set by the caller); answers 1, or 0 if the size is too small. */
GRACEUI_API uint32_t GraceUI_Test_GetState(GraceUiTestState* out);
/* Resets the allocation counters and the frame-cost samples. */
GRACEUI_API void GraceUI_Test_ResetCounters(void);
/* One frame's cost on the bound swap chain, in nanoseconds: the Present detour's whole time less the original Present's,
 * and the part of it spent inside the runtime's ExecuteCommandLists and Signal on GraceUI's own submission. */
typedef struct GraceUiTestFrameCost {
  uint32_t total_ns;
  uint32_t submit_ns;
} GraceUiTestFrameCost;

/* Copies up to `capacity` of the newest frames' costs, oldest first; answers how many. */
GRACEUI_API uint32_t GraceUI_Test_GetFrameCosts(GraceUiTestFrameCost* out, uint32_t capacity);
/* Copies log line `index` (0 the oldest kept) into `out`, NUL-terminated; answers its length, or 0 past the last. */
GRACEUI_API uint32_t GraceUI_Test_GetLogLine(uint32_t index, char* out, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* GRACEUI_TEST_H */
