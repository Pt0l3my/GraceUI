// SPDX-License-Identifier: MIT
// The release bundle's scan (DESIGN §7.2 items 10 and 15, §7.5, §9.2; invariant 6): dist/ holds index.html and
// content-hashed scripts and styles, and the licence file; no source map; no inline script, style or style attribute;
// no remote URL anywhere but in the licence texts (and the three XML namespace names Preact writes elements with,
// which are names, never fetched); no eval or new Function; and nothing of the dev page or the fixtures.
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";
import { DIST } from "../support/browser.ts";

const built = existsSync(join(DIST, "index.html"));
export const LICENCE_FILE = "licences.txt";
// Namespace names, not addresses: createElementNS needs them to make SVG and MathML elements.
const NAMESPACES = new Set(["http://www.w3.org/2000/svg", "http://www.w3.org/1998/Math/MathML", "http://www.w3.org/1999/xhtml"]);

function files(dir: string): string[] {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    return statSync(path).isDirectory() ? files(path) : [relative(DIST, path).split(sep).join("/")];
  });
}

test("the release bundle: hashed files, no map, no inline code, no remote URL, no eval, no fixtures",
     { skip: built ? false : "build dist/ first (npm run build)" }, () => {
  const all = files(DIST);
  for (const name of all) {
    assert.match(name, /^(index\.html|licences\.txt|assets\/[A-Za-z0-9_-]+-[A-Za-z0-9_-]{8}\.(js|css|woff2))$/, name);
    assert.ok(!name.endsWith(".map"), `${name}: no source map`);
  }
  assert.ok(all.some((n) => n.endsWith(".js")) && all.some((n) => n.endsWith(".css")));
  const html = readFileSync(join(DIST, "index.html"), "utf8");
  assert.doesNotMatch(html, /<script(?![^>]*\bsrc=)[^>]*>/, "no inline script");
  assert.doesNotMatch(html, /<style/, "no style element");
  assert.doesNotMatch(html, /\sstyle=/, "no style attribute");
  assert.doesNotMatch(html, /\son[a-z]+=/, "no inline handler");
  for (const ref of html.matchAll(/(?:src|href)="([^"]*)"/g)) {
    assert.match(ref[1] ?? "", /^(\/assets\/[A-Za-z0-9_.-]+|data:,)$/, `${ref[1]}: a file of this origin`);
  }
  for (const name of all) {
    if (name === LICENCE_FILE) {
      continue;  // the licence texts name their projects' addresses
    }
    const text = readFileSync(join(DIST, name), "utf8");
    for (const url of text.matchAll(/https?:\/\/[^\s"'`)<>\\]*/g)) {
      assert.ok(NAMESPACES.has(url[0]), `${name}: ${url[0]}`);
    }
    assert.doesNotMatch(text, /\beval\s*\(/, `${name}: eval`);
    assert.doesNotMatch(text, /\bnew\s+Function\s*\(/, `${name}: new Function`);
    assert.doesNotMatch(text, /sourceMappingURL/, `${name}: a source map reference`);
    assert.doesNotMatch(text, /lantern|The lighthouse|fixtures\//i, `${name}: the dev page's fixtures`);
    assert.doesNotMatch(text, /\/\/# sourceURL|import\(\s*["'`]https?:/, `${name}: a remote import`);
  }
});
