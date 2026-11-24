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

/** A master page's detail (D215): the family its blocks may name, and the chosen row's member (null while none); and
 * since D217 every family the space declares, each of which a block on the page draws for the chosen row's key. */
export interface MasterDetail {
  family: string;
  member: string | null;
  key: string | null;
  families: readonly string[];
  /** The page, and its list's keys as published (D251: a document's links go only to these). */
  page: string;
  keys: readonly string[];
}
export const MasterContext = createContext<MasterDetail | null>(null);

/** The topic a block draws: its own, or on a master page for a block bound to a family the chosen row's member
 * (undefined while no row is chosen). */
export function topicFor(master: MasterDetail | null, topic: string | undefined): string | undefined {
  if (master === null || topic === undefined || !master.families.includes(topic)) {
    return topic;
  }
  return master.key !== null ? `${topic}/${master.key}` : undefined;
}

export function useSession(): Session {
  const session = useContext(SessionContext);
  if (session === null) {
    throw new Error("no session");
  }
  return session;
}
