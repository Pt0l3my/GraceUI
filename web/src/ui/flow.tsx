// SPDX-License-Identifier: MIT
// flow.tsx: a space's flow in the browser (protocol 1.23, D278), as grace-view draws it in game: its steps as an
// ordered list of numbered names that takes no focus, the step its space publishes, and Back, Skip, Next or Done under
// it, each sending the flow's command once until its answer; Done, answered, opens the flow's next page. And the
// landing: a flow whose state says start opens once a page load, while the view is still on the page it started on.
import { useEffect, useRef, useState } from "preact/hooks";
import type { Json, Page } from "../protocol.ts";
import { flowBlocked, flowDone, flowOptional, flowStarts, flowsOf, flowThen } from "../model/flow.ts";
import { refusalWords } from "../model/session.ts";
import { useSession } from "./context.ts";
import { useTopic } from "./components.tsx";
import { Check, Warning } from "./icons.tsx";
import { domId } from "./view-state.ts";
import { focusHeading } from "./navigate.ts";

type Body = { [key: string]: Json } | undefined;
type Go = "back" | "skip" | "next" | "done";

/** The steps, the one shown marked as the current step and each one done checked. */
export function FlowSteps({ page, body, step }: { page: Page; body: Body; step: number }) {
  const done = flowDone(body);
  return (
    <ol class="flow-steps" aria-label={`Steps of ${page.name}`}>
      {(page.tabs ?? []).map((tab, i) => {
        const now = i === step;
        const was = !now && done.has(tab.id);
        return (
          <li key={tab.id} class={`flow-step${now ? " is-current" : was ? " is-done" : ""}`} data-step={tab.id}
              aria-current={now ? "step" : undefined}>
            <span class="flow-step__mark" aria-hidden="true">{was ? <Check /> : i + 1}</span>
            <span class="flow-step__name">{tab.name}</span>
            {was && <span class="visually-hidden"> (done)</span>}
          </li>
        );
      })}
    </ol>
  );
}

/** Back, Skip, Next or Done under the step, and above them the refusal of the last press or what Next waits for. */
export function FlowButtons({ space, page, step, body, inert }: {
  space: string; page: Page; step: number; body: Body; inert: boolean;
}) {
  const session = useSession();
  const [waiting, setWaiting] = useState<Go | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  // The newest press on the step shown: an answer to any other (an earlier step's) frees nothing and says nothing.
  const latest = useRef<object | null>(null);
  const steps = page.tabs ?? [];
  const id = steps[step]?.id ?? "";
  useEffect(() => {
    latest.current = null;  // a new step, the space's: its buttons take presses
    setWaiting(null);
    setRefusal(null);
  }, [page.id, id]);
  const flow = page.flow;
  if (!flow || steps.length === 0) {
    return null;
  }
  const last = step + 1 >= steps.length;
  const blocked = flowBlocked(body);
  const off = inert || waiting !== null;
  const noteId = domId("flow-note", space, page.id);
  const go = async (to: Go) => {
    if (off || ((to === "next" || to === "done") && blocked !== null)) {
      return;
    }
    const press = {};
    latest.current = press;
    setRefusal(null);
    setWaiting(to);
    const answer = await session.command(space, flow.command, { step: id, go: to });
    if (latest.current === press) {
      latest.current = null;
      setWaiting(null);
      if (!answer.ok) {
        setRefusal(refusalWords(answer));
      }
    }
    if (!answer.ok) {
      return;
    }
    // Done, answered: the flow's next page if the player is still on the flow, whatever step its space published since.
    const now = session.route.value;
    const pages = session.space(space)?.pages.value;
    if (to === "done" && now && now.space === space && now.page === page.id && pages) {
      const then = flowThen(pages, page);
      if (then !== undefined && session.open(space, then)) {
        focusHeading();
      }
    }
  };
  const label = (to: Go, words: string) => (waiting === to ? "Working…" : words);
  const forward: Go = last ? "done" : "next";
  return (
    <div class="flow-foot" data-flow={page.id}>
      {refusal !== null ? (
        <p class="flow-foot__note flow-foot__note--refused" id={noteId} role="alert"><Warning /> {refusal}</p>
      ) : blocked !== null ? (
        <p class="flow-foot__note" id={noteId} role="status" data-blocked="">{blocked}</p>
      ) : null}
      <div class="flow-foot__buttons">
        {step > 0 && (
          <button type="button" class="button button--secondary" data-flow-go="back" aria-disabled={off || undefined}
                  onClick={() => void go("back")}>
            {label("back", "Back")}
          </button>
        )}
        <span class="flow-foot__gap" />
        {flowOptional(page, step) && (
          <button type="button" class="button button--secondary" data-flow-go="skip" aria-disabled={off || undefined}
                  onClick={() => void go("skip")}>
            {label("skip", "Skip")}
          </button>
        )}
        <button type="button" class="button button--primary" data-flow-go={forward}
                aria-disabled={off || blocked !== null || undefined}
                aria-describedby={refusal !== null || blocked !== null ? noteId : undefined}
                onClick={() => void go(forward)}>
          {label(forward, last ? flow.done ?? "Done" : "Next")}
        </button>
      </div>
    </div>
  );
}

/** Watches the shown space's flows while the view may still land (D278), and lands on the first that asks. */
export function FlowLanding() {
  const session = useSession();
  const route = session.route.value;
  if (!session.landing.value || !route) {
    return null;
  }
  const pages = session.space(route.space)?.pages.value;
  return pages ? <>{flowsOf(pages).map((p) => <FlowWatch key={p.id} space={route.space} page={p} />)}</> : null;
}

function FlowWatch({ space, page }: { space: string; page: Page }) {
  const session = useSession();
  const data = useTopic(space, page.flow?.topic);
  const body = data?.kind === "state" ? data.body : undefined;
  useEffect(() => {
    if (flowStarts(body) && session.land(space, page.id)) {
      focusHeading();
    }
  }, [body]);
  return null;
}
