// SPDX-License-Identifier: MIT
// icons.tsx: GraceUI's own line icons (DESIGN §3.15: 1.5 px strokes on an 18 px grid), drawn for this repository.
// Decorative: each sits beside a word or inside a control with its own accessible name.
import { ICON_SIZE, ICON_STROKE, ICONS } from "../icons.ts";

const common = {
  width: 18, height: 18, viewBox: "0 0 18 18", fill: "none", stroke: "currentColor", "stroke-width": 1.5,
  "stroke-linecap": "round" as const, "stroke-linejoin": "round" as const, "aria-hidden": true as const,
  focusable: "false" as const, class: "icon",
};

export function Chevron({ open }: { open: boolean }) {
  return (
    <svg {...common} class={open ? "icon icon--open" : "icon"}>
      <path d="M6 7.5 9 10.5 12 7.5" />
    </svg>
  );
}

export function ChevronSide() {
  return (
    <svg {...common}>
      <path d="M11 5 7 9l4 4" />
    </svg>
  );
}

export function HelpMark() {
  return (
    <svg {...common}>
      <circle cx="9" cy="9" r="7" />
      <path d="M7 7.2a2 2 0 1 1 2.9 1.8c-.6.3-.9.7-.9 1.3v.4" />
      <path d="M9 12.8v.2" />
    </svg>
  );
}

export function SearchGlass() {
  return (
    <svg {...common}>
      <circle cx="8" cy="8" r="4.75" />
      <path d="m11.5 11.5 3.5 3.5" />
    </svg>
  );
}

export function Palette() {
  return (
    <svg {...common}>
      <path d="M9 2.5a6.5 6.5 0 1 0 0 13c1 0 1.5-.6 1.5-1.3 0-1-.9-1.2-.9-2s.6-1.2 1.6-1.2h1.6a2.7 2.7 0 0 0 2.7-2.7C15.5 5 12.6 2.5 9 2.5Z" />
      <circle cx="5.75" cy="8.5" r=".6" />
      <circle cx="8" cy="5.5" r=".6" />
      <circle cx="11.5" cy="6" r=".6" />
    </svg>
  );
}

export function Arrow() {
  return (
    <svg {...common}>
      <path d="M3.5 9h11M10.5 5l4 4-4 4" />
    </svg>
  );
}

export function ResetMark() {
  return (
    <svg {...common}>
      <path d="M4 5.5V9h3.5" />
      <path d="M4.3 9A5 5 0 1 0 6 5.2" />
    </svg>
  );
}

export function Cross() {
  return (
    <svg {...common}>
      <path d="m5 5 8 8M13 5l-8 8" />
    </svg>
  );
}

/** A step done (D278): the check the in-game strip draws, on the same 18-unit grid. */
export function Check() {
  return (
    <svg {...common}>
      <path d="M5 9.5 8 12.5 13 6" />
    </svg>
  );
}

export function Warning() {
  return (
    <svg {...common}>
      <path d="M9 2.8 16 15H2Z" />
      <path d="M9 7.3v3.4M9 12.8v.2" />
    </svg>
  );
}

/** The lozenge, our ornament (§3.6): hollow, or filled for a value changed from its default. */
export function Lozenge({ filled = false }: { filled?: boolean }) {
  return (
    <svg {...common} width={10} height={10} viewBox="0 0 10 10" class={filled ? "lozenge lozenge--filled" : "lozenge"}
         stroke-width={1.25}>
      <path d="M5 1 9 5 5 9 1 5Z" />
    </svg>
  );
}

export function Clock() {
  return (
    <svg {...common}>
      <circle cx="9" cy="9" r="6.5" />
      <path d="M9 5.5V9l2.5 1.5" />
    </svg>
  );
}

export function Power() {
  return (
    <svg {...common}>
      <path d="M9 2.5v6" />
      <path d="M5.2 5.2a5.5 5.5 0 1 0 7.6 0" />
    </svg>
  );
}

/** A category's line icon (§3.8, D277), from tokens/icons.toml as the game draws it: segments with round ends, and
 * rings; nothing for a name the set does not have. */
export function CategoryIcon({ icon }: { icon: string }) {
  const data = ICONS[icon];
  if (!data) {
    return null;
  }
  return (
    <svg {...common} viewBox={`0 0 ${ICON_SIZE} ${ICON_SIZE}`} stroke-width={ICON_STROKE} class="icon icon--category"
         data-icon={icon}>
      {data.segments.map(([x1, y1, x2, y2], i) => <line key={`s${i}`} x1={x1} y1={y1} x2={x2} y2={y2} />)}
      {data.rings.map(([cx, cy, r], i) => <circle key={`r${i}`} cx={cx} cy={cy} r={r} />)}
    </svg>
  );
}
