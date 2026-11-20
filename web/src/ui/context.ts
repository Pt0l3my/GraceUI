// SPDX-License-Identifier: MIT
// context.ts: the session every part of the view reads, handed down once by the app.
import { createContext } from "preact";
import { useContext } from "preact/hooks";
import type { Session } from "../model/session.ts";

export const SessionContext = createContext<Session | null>(null);

/** The open page and its space's greyed pages (D188): a page's words, or null while it is usable. */
export interface Greyed {
  page: string;
  words: (page: string) => string | null;
}
export const GreyedContext = createContext<Greyed>({ page: "", words: () => null });

export function useSession(): Session {
  const session = useContext(SessionContext);
  if (session === null) {
    throw new Error("no session");
  }
  return session;
}
