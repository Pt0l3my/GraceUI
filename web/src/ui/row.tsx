// SPDX-License-Identifier: MIT
// row.tsx: the settings row and its controls (DESIGN §3.8, §4.2.4, §4.7; D162, D163): the label, the changed lozenge
// with its reset, the help line that opens on focus and ends with the default, the availability badge, the effect as a
// faint word, and one control per setting type. Every change goes out as settings.set or settings.reset, and the row
// shows it as pending ("Saving…") until the space answers; a refusal puts its reason under the row and focus back on its
// control (§5.7). A secret is write-only: its field is never bound to state and is cleared as soon as it is sent, and
// the row shows only Set or Not set (§7.4, invariant 5).
import { useContext, useEffect, useLayoutEffect, useRef, useState } from "preact/hooks";
import type { Json, Setting } from "../protocol.ts";
import { decimalsOf, formatValue, isNumeric, nextValue, roundHalfAway, sameJson, triLabels, triValues } from "../model/format.ts";
import { BROWSER_KEYS, LEAVES_CAPTURE, keyName } from "../model/keys.ts";
import { pageById, parseLink } from "../model/layout.ts";
import { GreyedContext, useSession } from "./context.ts";
import { useTopic } from "./components.tsx";
import { Lozenge, ResetMark, Warning } from "./icons.tsx";
import { revealSetting } from "./navigate.ts";
import { domId, found } from "./view-state.ts";

interface ControlProps {
  s: Setting;
  space: string;
  shown: Json | undefined;
  labelId: string;
  helpId: string;
  disabled: boolean;
}

const EFFECT_WORDS: { [effect: string]: string } = {
  next_moment: "From the next moment",
  next_request: "From the next request",
  next_launch: "At the next launch",
};

/** The availability badge in §4.7's words, as grace-view's (page_view.cpp); none for live, and "later" replaces the
 * control instead. `valueOf` and `labelOf` read the setting a `requires:` names. */
export function availabilityBadge(availability: string, valueOf: (key: string) => unknown = () => undefined,
                                  labelOf: (key: string) => string | undefined = () => undefined): string | null {
  if (availability.startsWith("check:")) {
    return "Waits on a game check";
  }
  if (availability.startsWith("version:")) {
    return `Takes effect in v${availability.slice(8)}`;
  }
  if (availability.startsWith("requires:")) {
    const key = availability.slice(9);
    const v = valueOf(key);
    return v === false || v === "off" ? `Off because ${labelOf(key) ?? key} is off` : null;
  }
  return availability === "inert" ? "No effect in this version" : null;
}

let handledFocus = 0;  // the last focus request a row took (one row answers each)

function SwitchControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const on = shown === true;
  const flip = () => {
    if (!disabled) {
      session.set(space, s.key, !on);
    }
  };
  return (
    <span class="switch-wrap">
      <button type="button" role="switch" class="switch" aria-checked={on} aria-labelledby={labelId}
              aria-describedby={helpId} aria-disabled={disabled || undefined} data-control="" onClick={flip}
              onKeyDown={(e) => {
                if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
                  e.preventDefault();
                  flip();
                }
              }}>
        <span class="switch__knob" />
      </button>
      <span class="switch__word" aria-hidden="true">{formatValue(s, shown)}</span>
    </span>
  );
}

function snap(s: Setting, raw: number): number {
  const lo = s.min ?? 0;
  const step = s.step ?? 1;
  let n = lo + Math.round((raw - lo) / step) * step;
  n = Math.min(Math.max(n, s.min ?? n), s.max ?? n);
  const scale = Math.pow(10, decimalsOf(step));
  n = roundHalfAway(n * scale) / scale;
  return s.type === "integer" ? roundHalfAway(n) : n;
}

function SliderControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const track = useRef<HTMLDivElement>(null);
  const [drag, setDrag] = useState<number | null>(null);
  const lo = s.min ?? 0;
  const hi = s.max ?? 1;
  const value = drag ?? (typeof shown === "number" ? shown : lo);
  const t = hi > lo ? Math.min(Math.max((value - lo) / (hi - lo), 0), 1) : 0;
  useLayoutEffect(() => {
    track.current?.style.setProperty("--fill", String(t));  // CSSOM, which the CSP allows (no inline style)
  }, [t]);
  const send = (v: Json | undefined) => {
    if (v !== undefined && !disabled) {
      session.set(space, s.key, v);
    }
  };
  const at = (x: number): number => {
    const rect = track.current?.getBoundingClientRect();
    if (!rect || rect.width <= 0) {
      return value;
    }
    return snap(s, lo + Math.min(Math.max((x - rect.left) / rect.width, 0), 1) * (hi - lo));
  };
  const text = formatValue(s, value);
  return (
    <span class="slider">
      <span ref={track} class="slider__track" role="slider" tabIndex={disabled ? -1 : 0} aria-valuemin={lo}
            aria-valuemax={hi} aria-valuenow={value} aria-valuetext={text} aria-labelledby={labelId}
            aria-describedby={helpId} aria-disabled={disabled || undefined} data-control=""
            onKeyDown={(e) => {
              if (disabled) {
                return;
              }
              const up = e.key === "ArrowRight" || e.key === "ArrowUp";
              const down = e.key === "ArrowLeft" || e.key === "ArrowDown";
              if (up || down) {
                e.preventDefault();
                send(nextValue(s, shown, up ? 1 : -1, e.shiftKey));  // Shift: ten steps (grace-view's)
              } else if (e.key === "Home" || e.key === "End") {
                e.preventDefault();
                send(e.key === "Home" ? lo : hi);
              }
            }}
            onPointerDown={(e) => {
              if (disabled || e.button !== 0) {
                return;
              }
              (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
              (e.currentTarget as HTMLElement).focus();
              setDrag(at(e.clientX));
            }}
            onPointerMove={(e) => {
              if (drag !== null) {
                setDrag(at(e.clientX));
              }
            }}
            onPointerUp={(e) => {
              if (drag !== null) {
                const v = at(e.clientX);
                setDrag(null);
                if (!sameJson(v, shown)) {
                  send(v);
                }
              }
            }}
            onPointerCancel={() => setDrag(null)}>
        <span class="slider__fill" />
        <span class="slider__thumb" />
      </span>
      <output class="slider__value" aria-hidden="true">{text}</output>
    </span>
  );
}

function Segmented({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const group = useRef<HTMLDivElement>(null);
  const options = s.type === "tri_state"
    ? triValues(s).map((value, i) => ({ value, label: triLabels(s)[i] ?? value }))
    : (s.choices ?? []).map((c) => ({ value: c.value, label: c.label }));
  const at = options.findIndex((o) => o.value === shown);
  const stop = at < 0 ? 0 : at;
  useEffect(() => {
    // Focus follows the chosen value while the group holds it (the radio group's arrows).
    const el = group.current;
    if (el && el.contains(document.activeElement)) {
      (el.querySelectorAll<HTMLElement>("[role=radio]")[stop])?.focus();
    }
  }, [stop]);
  const choose = (v: Json | undefined) => {
    if (v !== undefined && !disabled && !sameJson(v, shown)) {
      session.set(space, s.key, v);
    }
  };
  return (
    <div ref={group} role="radiogroup" class={`segmented segmented--${options.length}`} aria-labelledby={labelId}
         aria-describedby={helpId} aria-disabled={disabled || undefined}>
      {options.map((o, i) => (
        <button type="button" role="radio" class="segmented__part" aria-checked={i === at} tabIndex={i === stop ? 0 : -1}
                data-control={i === stop ? "" : undefined} key={o.value} onClick={() => choose(o.value)}
                onKeyDown={(e) => {
                  const forward = e.key === "ArrowRight" || e.key === "ArrowDown";
                  const back = e.key === "ArrowLeft" || e.key === "ArrowUp";
                  if (forward || back) {
                    e.preventDefault();
                    choose(nextValue(s, shown, forward ? 1 : -1));  // wrapping, as grace-view's ← →
                  }
                }}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

function TextControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const [draft, setDraft] = useState<string | null>(null);
  const value = draft ?? (typeof shown === "string" ? shown : "");
  const commit = () => {
    if (draft !== null && draft !== shown && !disabled) {
      session.set(space, s.key, draft);
    }
    setDraft(null);
  };
  return (
    <input type="text" class="field" value={value} spellcheck={false} autocomplete="off" aria-labelledby={labelId}
           aria-describedby={helpId} disabled={disabled} data-control=""
           onInput={(e) => setDraft((e.currentTarget as HTMLInputElement).value)}
           onKeyDown={(e) => {
             if (e.key === "Enter") {
               e.preventDefault();
               commit();
             } else if (e.key === "Escape" && draft !== null) {
               e.preventDefault();
               setDraft(null);
             }
           }}
           onBlur={commit} />
  );
}

function SecretControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const field = useRef<HTMLInputElement>(null);
  const status = formatValue(s, shown);
  // The value goes from the field straight into one command and the field is emptied: it is never held in the page's
  // state, an attribute, a log or storage (§7.4). Not a password field, and in no form, so no browser offers to keep it.
  const save = () => {
    const el = field.current;
    if (!el || el.value.length === 0 || disabled) {
      return;
    }
    const value = el.value;
    el.value = "";
    session.set(space, s.key, value);
  };
  useEffect(() => () => {
    if (field.current) {
      field.current.value = "";
    }
  }, []);
  return (
    <span class="secret">
      <span class={status === "Set" ? "badge badge--set" : "badge"}>{status}</span>
      <input ref={field} type="text" class="field field--secret" autocomplete="off" spellcheck={false}
             autocapitalize="off" aria-labelledby={labelId} aria-describedby={helpId} disabled={disabled}
             placeholder={status === "Set" ? "Type a new one to replace it" : "Type it here"} data-control=""
             onKeyDown={(e) => {
               if (e.key === "Enter") {
                 e.preventDefault();
                 save();
               }
             }} />
      <button type="button" class="button button--secondary button--small" disabled={disabled} onClick={save}>Save</button>
    </span>
  );
}

function KeyControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const [waiting, setWaiting] = useState(false);
  const capId = domId("cap", space, s.key);
  const noteId = domId("note", space, s.key);
  return (
    <span class="keycapture">
      <button type="button" class="keycap" data-control="" data-capturing={waiting ? "" : undefined}
              aria-labelledby={`${labelId} ${capId}`} aria-describedby={waiting ? `${noteId} ${helpId}` : helpId}
              aria-disabled={disabled || undefined}
              onClick={() => {
                if (!disabled) {
                  setWaiting(true);
                }
              }}
              onBlur={() => setWaiting(false)}
              onKeyDown={(e) => {
                if (!waiting) {
                  return;
                }
                if (e.code === LEAVES_CAPTURE) {
                  setWaiting(false);  // Tab moves on, binding nothing
                  return;
                }
                e.preventDefault();  // while it waits, F5 and F11 come here, not to the browser (§5.9)
                e.stopPropagation();
                if (e.key === "Escape") {
                  setWaiting(false);
                  return;
                }
                const name = keyName(e.code);
                if (name !== undefined) {
                  setWaiting(false);
                  session.set(space, s.key, name);
                }
              }}>
        <span id={capId}>{waiting ? "Press a key…" : formatValue(s, shown)}</span>
      </button>
      {waiting && (
        <span id={noteId} class="row__note" role="status">
          Press the key to use; Escape cancels. While this waits, {[...BROWSER_KEYS].join(" and ")} come here, not to
          the browser.
        </span>
      )}
    </span>
  );
}

// A picker (D193): a text setting whose choices the space publishes on a topic. The value stays shown even when the list
// no longer has it; an empty list says so. The space checks that a value is one of its choices (invariant 3).
function PickerControl({ s, space, shown, labelId, helpId, disabled }: ControlProps) {
  const session = useSession();
  const data = useTopic(space, s.choices_from);
  const body = data?.kind === "state" ? data.body : undefined;
  const raw = body !== undefined && Array.isArray(body.choices) ? body.choices : [];
  const choices: { value: string; label: string }[] = [];
  for (const c of raw) {
    if (typeof c === "object" && c !== null && !Array.isArray(c) && typeof c.value === "string" && c.value !== "" &&
        c.value.length <= 200 && choices.length < 256) {
      choices.push({ value: c.value, label: typeof c.label === "string" && c.label !== "" ? c.label : c.value });
    }
  }
  const value = typeof shown === "string" ? shown : "";
  if (choices.length === 0) {
    return (
      <span class="picker picker--empty">
        {value !== "" && <span class="picker__value">{value}</span>}
        <span class="picker__none">Nothing to choose from yet</span>
      </span>
    );
  }
  const listed = choices.some((c) => c.value === value);
  return (
    <select class="field picker" value={value} aria-labelledby={labelId} aria-describedby={helpId} disabled={disabled}
            data-control=""
            onChange={(e) => {
              const next = (e.currentTarget as HTMLSelectElement).value;
              if (next !== value) {
                session.set(space, s.key, next);
              }
            }}>
      {!listed && value !== "" && <option value={value}>{value}</option>}
      {choices.map((c) => <option key={c.value} value={c.value}>{c.label}</option>)}
    </select>
  );
}

function Control(props: ControlProps) {
  const { s } = props;
  if (s.type === "text" && s.choices_from !== undefined) {
    return <PickerControl {...props} />;
  }
  if (s.type === "bool") {
    return <SwitchControl {...props} />;
  }
  if (isNumeric(s.type)) {
    return <SliderControl {...props} />;
  }
  if (s.type === "choice" || s.type === "tri_state") {
    return <Segmented {...props} />;
  }
  if (s.type === "secret") {
    return <SecretControl {...props} />;
  }
  if (s.type === "key_name") {
    return <KeyControl {...props} />;
  }
  return <TextControl {...props} />;
}

/** One row, homed here (a setting) or elsewhere (a shortcut, which opens its home with the row focused). */
export function SettingRow({ space, settingKey, shortcut }: { space: string; settingKey: string; shortcut: boolean }) {
  const session = useSession();
  const greyedPage = useContext(GreyedContext);
  const row = useRef<HTMLDivElement>(null);
  const s = session.setting(space, settingKey);
  const request = session.focusRequest.value;
  const foundHere = found.value === space + "\u0000" + settingKey;
  useEffect(() => {
    if (!shortcut && request && request.space === space && request.key === settingKey && request.seq !== handledFocus) {
      handledFocus = request.seq;
      row.current?.querySelector<HTMLElement>("[data-control]")?.focus();
      row.current?.scrollIntoView?.({ block: "nearest" });
    }
  }, [request?.seq, s !== undefined]);
  if (!s) {
    return null;  // the space has published no such setting: nothing is shown (invariant 2)
  }
  const shown = session.shown(space, settingKey);
  const edit = session.edit(space, settingKey);
  const refusal = session.refusal(space, settingKey);
  const labelId = domId("label", space, settingKey);
  const helpId = domId("help", space, settingKey);
  const pages = session.space(space)?.pages.value;
  const rowKind = shortcut ? "shortcut" : "setting";
  // A page its space has greyed, or the setting's home greyed (D188): its control is inert.
  const greyed = greyedPage.words(greyedPage.page) !== null || greyedPage.words(s.home) !== null;
  if (shortcut) {
    const home = pages ? pageById(pages, s.home) : undefined;
    if (pages && !home) {
      return null;  // a shortcut to a page of a later increment (D173), as grace-view leaves it out
    }
    // Its home's control and its link (§4.7, D189), as grace-view draws it.
    const inert = s.locked || s.availability === "later" || greyed;
    return (
      <div ref={row} class={inert ? "row row--shortcut row--locked" : "row row--shortcut"}
           data-row={`${rowKind}:${settingKey}`} data-space={space}>
        <div class="row__text">
          <div class="row__line">
            <span id={labelId} class="row__label">{s.label}</span>
            {home && (
              <button type="button" class="lives-in" aria-describedby={labelId}
                      onClick={() => revealSetting(session, space, settingKey)}>
                Lives in {home.name}
              </button>
            )}
          </div>
          <p id={helpId} class="row__help">{s.help} Default: {formatValue(s, s.default)}.</p>
          {refusal && (
            <p class="row__refusal" role="alert"><Warning /> {refusal}</p>
          )}
        </div>
        <div class="row__control">
          <Control s={s} space={space} shown={shown} labelId={labelId} helpId={helpId} disabled={inert} />
        </div>
      </div>
    );
  }
  const changed = !sameJson(shown, s.default);
  const later = s.availability === "later";
  const disabled = s.locked || later || greyed;
  const badge = availabilityBadge(s.availability, (key) => session.shown(space, key),
                                  (key) => session.setting(space, key)?.label);
  const inherited = s.type === "tri_state" && shown === "inherit"
    ? session.space(space)?.settings.value?.inherited?.find((one) => one.key === settingKey) : undefined;
  const inheritedLink = inherited?.page ? parseLink(space, inherited.page) : undefined;
  const effect = EFFECT_WORDS[s.effect];
  const classes = ["row"];
  if (disabled) {
    classes.push("row--locked");
  }
  if (foundHere) {
    classes.push("row--found");
  }
  if (refusal) {
    classes.push("row--refused");
  }
  return (
    <div ref={row} class={classes.join(" ")} data-row={`${rowKind}:${settingKey}`} data-space={space}
         onFocusOut={(e) => {
           if (foundHere && !row.current?.contains(e.relatedTarget as Node | null)) {
             found.value = null;
           }
         }}>
      <div class="row__text">
        <div class="row__line">
          {changed && <span class="row__changed" title="Changed from its default"><Lozenge filled /></span>}
          <span id={labelId} class="row__label">{s.label}</span>
          {changed && <span class="visually-hidden">(changed from its default)</span>}
          {inherited && (
            <span class="row__inherited">
              {inherited.on ? "On" : "Off"}, from{" "}
              {inheritedLink ? (
                <button type="button" class="link" onClick={() => session.open(inheritedLink.space, inheritedLink.page)}>
                  {inherited.source}
                </button>
              ) : inherited.source}
            </span>
          )}
          {badge && <span class="badge badge--availability">{badge}</span>}
          {effect && <span class="row__effect">{effect}</span>}
        </div>
        <p id={helpId} class="row__help">{s.help} Default: {formatValue(s, s.default)}.</p>
        {edit && (
          <p class="row__pending" role="status"><span class="dots" aria-hidden="true">···</span> Saving…</p>
        )}
        {refusal && (
          <p class="row__refusal" role="alert"><Warning /> {refusal}</p>
        )}
      </div>
      {changed && !disabled && (
        <button type="button" class="reset" aria-label={`Reset ${s.label} to its default`} title="Reset to its default"
                onClick={() => {
                  session.reset(space, settingKey);
                  row.current?.querySelector<HTMLElement>("[data-control]")?.focus();
                }}>
          <ResetMark />
        </button>
      )}
      <div class="row__control">
        {later
          ? <button type="button" class="badge badge--later" data-control="" aria-disabled="true"
                    aria-labelledby={`${labelId} ${domId("later", space, settingKey)}`} aria-describedby={helpId}>
              <span id={domId("later", space, settingKey)}>Later version</span>
            </button>
          : <Control s={s} space={space} shown={shown} labelId={labelId} helpId={helpId} disabled={disabled} />}
      </div>
    </div>
  );
}
