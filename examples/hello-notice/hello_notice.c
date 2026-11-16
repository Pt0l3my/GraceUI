/* SPDX-License-Identifier: MIT */
/*
 * hello_notice.c: an example mod of our own, in C (D85): it finds GraceUI with graceui-sys, connects, and shows one
 * notice, which says how it found GraceUI. Everything about it is invented. It shows the shape every mod follows:
 *
 *   - Nothing in DllMain but starting a thread of its own: GraceUI is found and called from that thread (D70).
 *   - GraceUI absent, or too old: the mod's UI is simply off, with no error dialog.
 *   - Anti-cheat present: connect answers GRACEUI_INACTIVE with GRACEUI_INACTIVE_ANTI_CHEAT, and notice still works (D88).
 *
 * tools/grace-harness loads it and checks, through GraceUI, that it connected and that its notice shows.
 */
#include <stdio.h>
#include <string.h>

#include "graceui_sys.h"

#define HELLO_ID "hello-notice"
#define HELLO_NAME "Hello notice"

static const char* how_found(uint32_t how) {
  switch (how) {
    case GRACEUI_SYS_BY_NAME: return "by name";
    case GRACEUI_SYS_BY_SCAN: return "by a scan of the loaded modules";
    case GRACEUI_SYS_BY_PATH: return "in its own folder";
    default: return "somehow";
  }
}

static DWORD WINAPI hello_worker(LPVOID unused) {
  GraceUiSysFound found;
  GraceUiConnectRequest request;
  GraceUiConnectAnswer answer;
  GraceUiNotice notice;
  GraceUiStatus status = GRACEUI_OK;
  const GraceUiApi1* api = NULL;
  char text[200];
  int length = 0;
  (void)unused;

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

  length = snprintf(text, sizeof text, "Hello from an example mod: GraceUI %u.%u.%u, found %s at look %u.",
                    GRACEUI_VERSION_MAJOR(found.version), GRACEUI_VERSION_MINOR(found.version),
                    GRACEUI_VERSION_PATCH(found.version), how_found(found.how), found.attempts);
  if (length <= 0 || length >= (int)sizeof text) {
    return 0;
  }
  memset(&notice, 0, sizeof notice);
  notice.size = sizeof notice;
  notice.tone = GRACEUI_TONE_NORMAL;
  notice.key = 1;          /* the mod's one notice: a later one with key 1 would replace it */
  notice.duration_ms = 0;  /* until replaced or withdrawn, as a banner */
  notice.text = text;
  notice.text_length = (uint64_t)length;
  api->notice(answer.consumer, &notice);
  return 0;
}

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
