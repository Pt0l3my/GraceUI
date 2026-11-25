// SPDX-License-Identifier: MIT
// share.ts: a report preview's shareable text (D263), as its body gives it. Pure. grace-view's PageView::shared takes
// the same: a text of at most 256 KiB and a file name of 1 to 64 of a-z, 0-9, -, _ and ., ending .txt; else none, with
// the space's blocked words (at most 400 bytes) if it gave some.
import type { JsonObject } from "../protocol.ts";

const SHARE_FILE = /^[a-z0-9._-]{1,60}\.txt$/;
const utf8Length = (words: string) => new TextEncoder().encode(words).length;
export function shareOf(body: JsonObject): { text: string; file: string } | { blocked: string | null } {
  const share = body.share;
  if (share !== null && typeof share === "object" && !Array.isArray(share)) {
    if ("blocked" in share) {
      const words = typeof share.blocked === "string" && utf8Length(share.blocked) <= 400 ? share.blocked : null;
      return { blocked: words !== "" ? words : null };
    }
    const { text: words, file } = share;
    if (typeof words === "string" && words !== "" && utf8Length(words) <= 262144 && typeof file === "string" &&
        SHARE_FILE.test(file)) {
      return { text: words, file };
    }
  }
  return { blocked: null };
}
