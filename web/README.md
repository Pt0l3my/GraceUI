# GraceUI's browser view

The web shell (DESIGN §2.5, §4.2, §4.3, §5.9, §6, §7.2; G3; D166): Preact with signals and TypeScript, built with Vite.
It draws every space's pages from their page data with GraceUI's own components, as the in-game view does
(`src/grace-view`), and reaches the spaces only through one `Transport`: the WebSocket transport to `grace-serve` in
release, the fixture transport in the tests and on the dev page.

## Install, build, check

Node 22.18 or later. Install only from the lockfile, with install scripts off (`.npmrc` keeps them off too):

```sh
cd web
npm ci --ignore-scripts
npm audit
npm run build                            # dist/: index.html and content-hashed scripts and styles
python3 -I ../tools/web/licences.py      # the installed tree's licences; writes dist/licences.txt
npm run typecheck                        # tsc --strict over src/
npm test                                 # the logic tests (node:test)
npm run test:dom                         # the bundle scan, and Chromium against dist/ behind a fake host
```

The DOM tests use the Playwright and Chromium already installed on the machine, found as a global module; they are not
a dependency of the web app and are never installed by it. The Linux CTest build runs all of this (`web_*` tests) when
Node and `node_modules` are present: `cmake -S . -B build-linux -G Ninja`, then `ctest --test-dir build-linux`.

Other commands:

```sh
npm run dev                              # the dev page at http://127.0.0.1:5173, playing the fixtures
npm run screenshots -- <folder>          # the review shots, from dist/ (1920 × 1080 at device scale 2)
python3 -I ../tools/web/licences.py --check
```

## What is where

- `src/protocol.ts` and `src/tokens.css` are generated (`tools/protocol/generate.py`, `tools/tokens/build.py`): never
  edit them. `tools/tokens/build.py --check` also checks the stylesheets here: colours, type sizes and durations only
  from the tokens, no `url()`, no looping animation, no inline style.
- `src/model/`: pure logic, tested in Node. `format.ts` is grace-view's formatting and each press's value; `layout.ts`
  a page's rows in grace-view's order; `search.ts` global search; `session.ts` the spaces' state as signals and the one
  command path (edits pending until the space answers; refusals with their reason and focus back on the row).
- `src/transport/`: the `Transport` interface, the fixture transport and the space it plays (`fixture-space.ts`,
  shared with the tests' fake host), and the WebSocket transport (ticket by `POST /api/ticket`, socket at `/ws`, hello
  first; its paths are options, for `grace-serve` and `grace-dev`).
- `src/ui/`: the frame (rail, top bar, page), the settings row and its controls, the components, and the keyboard map.
- `tests/unit/`: logic tests, including the parity goldens that `grace_view_tests` writes
  (`src/grace-view/tests/golden/rows-order.txt` and `formats.txt`). `tests/dom/`: the bundle scan and the DOM tests.
  `tests/support/fake-host.ts` stands in for `grace-serve` (its CSP, cookie, ticket and socket) until `grace-dev`.

## Rules the code keeps

- Only what a space published is shown, counted or searched (invariants 1 and 2); every string is text, never markup.
- Every change is a command to the space that owns the page (invariant 3); a secret goes from its field into one
  command and is cleared, never held in state, storage or the DOM (invariant 5); nothing leaves the page's origin
  (invariant 6); nothing is kept in browser storage (PV3).
- No inline script or style, no `eval`, no source maps in a release, no remote URL: the page runs under grace-serve's
  CSP as it is (§7.2 item 10).
