// SPDX-License-Identifier: MIT
// context.ts: the session every part of the view reads, handed down once by the app.
import { createContext } from "preact";
import { useContext } from "preact/hooks";
import type { Session } from "../model/session.ts";

export const SessionContext = createContext<Session | null>(null);

export function useSession(): Session {
  const session = useContext(SessionContext);
  if (session === null) {
    throw new Error("no session");
  }
  return session;
}
