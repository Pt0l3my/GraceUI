// SPDX-License-Identifier: MIT
// main.tsx: the browser view's entry. The release bundle reaches the game through the WebSocket transport, on this
// page's own origin alone (invariant 6); the dev server plays the fixtures instead, and that branch is not built into a
// release.
import "./tokens.css";
import "./styles/grace.css";
import { render } from "preact";
import { Session } from "./model/session.ts";
import type { Transport } from "./transport/transport.ts";
import { WebSocketTransport } from "./transport/websocket.ts";
import { App } from "./ui/app.tsx";

async function transport(): Promise<() => Transport> {
  if (import.meta.env.DEV) {
    const dev = await import("./dev/fixtures.ts");
    return dev.devTransport;
  }
  return () => new WebSocketTransport({ bundle: __GRACEUI_BUNDLE__ });
}

const root = document.getElementById("app");
if (root) {
  void transport().then((make) => {
    const session = new Session({ transport: make });
    render(<App session={session} />, root);
    void session.start();
  });
}
