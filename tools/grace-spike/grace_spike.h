/* SPDX-License-Identifier: MIT
 * grace_spike.h: what grace-spike.dll (grace_spike.cpp, D175) names, for the harness that checks it. */
#ifndef GRACE_SPIKE_H
#define GRACE_SPIKE_H

#define GRACE_SPIKE_ID "grace-spike"
#define GRACE_SPIKE_NAME "GraceUI spike observer"
#define GRACE_SPIKE_ACTION "typing-test"
#define GRACE_SPIKE_KEY 0x78 /* F9 */
#define GRACE_SPIKE_FILE L"graceui-spike.jsonl"
#define GRACE_SPIKE_LOG_FILE L"graceui-log.txt" /* GraceUI's own log lines, beside the report and never in it (D230) */
#define GRACE_SPIKE_DIR_VARIABLE L"ELDENNET_SPIKE_DIR" /* EldenNet's launcher names the run's folder; unset, nothing */
#define GRACE_SPIKE_BANNER_KEY 1
#define GRACE_SPIKE_BANNER "GraceUI spike: recording. F9 opens the typing test."
#define GRACE_SPIKE_TYPING_MS 60000u /* the typing test's length; GRACE_SPIKE_TYPING_MS in the environment shortens it */
#define GRACE_SPIKE_STATE_MS 5000u   /* between two state lines; GRACE_SPIKE_STATE_MS in the environment changes it */
#define GRACE_SPIKE_WIDTH 560
#define GRACE_SPIKE_HEIGHT 150
#define GRACE_STANDIN_FILE L"grace-standin.dll"

#endif /* GRACE_SPIKE_H */
