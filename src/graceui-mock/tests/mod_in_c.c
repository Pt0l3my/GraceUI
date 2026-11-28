/* SPDX-License-Identifier: MIT */
/*
 * mod_in_c.c: a mod's logic written in C, as a mod author would test it with the mock (D285): it connects, registers a
 * space, and answers its one command, on the table it is handed. Invented content. It also shows that graceui_mock.h
 * and graceui.h compile as C.
 */
#include <stdio.h>
#include <string.h>

#include "graceui_mock.h"

static const GraceUiApi1* g_api = NULL;
static GraceUiConsumer g_me = 0;
static int g_count = 0;
static char g_frame[GRACEUI_FRAME_FROM_PAGE_MAX + 1]; /* a frame, and its NUL after it */

static const char kPages[] = "{\"space\":\"c-mod\",\"name\":\"C mod\",\"minor\":0,\"categories\":[],\"pages\":[],"
                             "\"topics\":[{\"id\":\"c.count\",\"shape\":\"state\"}],\"commands\":[\"c.add\"]}";
static const char kSettings[] = "{\"schema\":{\"number\":1,\"hash\":\"0000000000000000\"},\"settings\":[]}";

/* 0 once connected and registered; GraceUI's status otherwise. */
GraceUiStatus c_mod_start(const GraceUiApi1* api) {
  GraceUiConnectRequest request;
  GraceUiConnectAnswer answer;
  GraceUiSpaceDesc space;
  GraceUiStatus status;
  if (api == NULL || !GRACEUI_API1_COVERS(api, register_pages) || !api->has_cap(GRACEUI_CAP_PAGES, 5)) {
    return GRACEUI_NOT_YET;
  }
  memset(&request, 0, sizeof request);
  request.size = sizeof request;
  request.abi_minor = GRACEUI_ABI1_MINOR;
  request.version = GRACEUI_VERSION_PACK(1, 0, 0);
  request.floor = GRACEUI_VERSION_PACK(0, 2, 0);
  request.id = "c-mod";
  request.id_length = 5;
  request.name = "C mod";
  request.name_length = 5;
  memset(&answer, 0, sizeof answer);
  answer.size = sizeof answer;
  status = api->connect(&request, &answer);
  if (status != GRACEUI_OK) {
    return status;
  }
  memset(&space, 0, sizeof space);
  space.size = sizeof space;
  space.views = GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER;
  space.version = GRACEUI_VERSION_PACK(1, 0, 0);
  space.pages = kPages;
  space.pages_length = sizeof kPages - 1;
  space.settings = kSettings;
  space.settings_length = sizeof kSettings - 1;
  space.increment = "0.2";
  space.increment_length = 3;
  status = api->register_pages(answer.consumer, &space);
  if (status != GRACEUI_OK) {
    return status;
  }
  g_api = api;
  g_me = answer.consumer;
  g_count = 0;
  return GRACEUI_OK;
}

/* Polls until nothing is left: a watch of c.count is answered with its state, c.add adds one and is answered ok, any
 * other command is refused in the mod's own words. Answers the events handled. */
int c_mod_poll(void) {
  int handled = 0;
  for (;;) {
    GraceUiEvent event;
    char text[256];
    int length;
    memset(&event, 0, sizeof event);
    event.size = sizeof event;
    event.frame = g_frame;
    event.frame_capacity = GRACEUI_FRAME_FROM_PAGE_MAX;
    if (g_api->poll(g_me, &event) != GRACEUI_OK || event.kind == GRACEUI_EVENT_NONE) {
      return handled;
    }
    ++handled;
    g_frame[event.frame_length] = '\0';
    if (event.kind == GRACEUI_EVENT_COMMAND) {
      const int add = strstr(g_frame, "\"name\":\"c.add\"") != NULL;
      if (add) {
        ++g_count;
      }
      length = snprintf(text, sizeof text, add ? "{\"v\":1,\"t\":\"result\",\"space\":\"c-mod\",\"id\":0,\"ok\":true}"
                        : "{\"v\":1,\"t\":\"result\",\"space\":\"c-mod\",\"id\":0,\"error\":\"unknown_command\","
                          "\"detail\":\"C mod has no such command.\"}");
      g_api->answer(g_me, event.reply, text, (uint64_t)length);
    }
    if (event.kind == GRACEUI_EVENT_WATCHED || (event.kind == GRACEUI_EVENT_COMMAND && g_count > 0)) {
      length = snprintf(text, sizeof text,
                        "{\"v\":1,\"t\":\"state\",\"space\":\"c-mod\",\"topic\":\"c.count\",\"rev\":%d,"
                        "\"body\":{\"sentence\":\"The count is %d.\"}}", handled, g_count);
      g_api->publish(g_me, text, (uint64_t)length);
    }
  }
}
