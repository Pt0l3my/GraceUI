// SPDX-License-Identifier: MIT
// confirm.ts: how long an action's confirmation waits before its second press counts (D274), as grace-view's
// PageView::ask does in game: the action's confirm_after_ms, at most the protocol's limit; none, at once.
import { LIMITS } from "../protocol.ts";

/** The confirmation's wait in ms: 0 to the protocol's limit (3000); anything not a positive number, 0. */
export function confirmWait(action: { confirm_after_ms?: number }): number {
  const ms = action.confirm_after_ms ?? 0;
  return Number.isFinite(ms) && ms > 0 ? Math.min(ms, LIMITS.confirmAfterMaxMs) : 0;
}
