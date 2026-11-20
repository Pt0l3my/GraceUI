// SPDX-License-Identifier: MIT
// vite.config.mjs: the browser view's build (DESIGN §7.2 item 10, §7.5; G3). The release output, dist/, is index.html
// and content-hashed scripts and styles: no inline script or style, no source maps, no module-preload polyfill, no
// asset inlined as a data URL, so the page runs under grace-serve's CSP as it is. The build also records which
// packages' code the bundle holds, for tools/web/licences.py to write the bundle's licence file.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const here = dirname(fileURLToPath(import.meta.url));
const pkg = JSON.parse(readFileSync(join(here, "package.json"), "utf8"));

// The packages whose modules are in the output, from Rollup's own record of each chunk, written beside the installed
// tree (node_modules/.cache, never served) for the licence tool.
function bundledPackages() {
  return {
    name: "graceui-bundled-packages",
    apply: "build",
    generateBundle(_options, bundle) {
      const found = new Map();
      for (const chunk of Object.values(bundle)) {
        if (chunk.type !== "chunk") {
          continue;
        }
        for (const raw of Object.keys(chunk.modules)) {
          const id = raw.split(sep).join("/");
          const at = id.lastIndexOf("/node_modules/");
          if (at < 0 || id.startsWith("\0")) {
            continue;
          }
          const rest = id.slice(at + 14).split("/");
          const name = rest[0]?.startsWith("@") ? `${rest[0]}/${rest[1]}` : rest[0];
          const meta = JSON.parse(readFileSync(join(here, "node_modules", name, "package.json"), "utf8"));
          found.set(name, meta.version);
        }
      }
      const out = join(here, "node_modules", ".cache", "graceui");
      mkdirSync(out, { recursive: true });
      const list = [...found].sort().map(([name, version]) => ({ name, version }));
      writeFileSync(join(out, "bundled.json"), JSON.stringify({ bundle: pkg.version, packages: list }, null, 2) + "\n");
    },
  };
}

export default defineConfig(({ command }) => ({
  root: here,
  base: "/",
  publicDir: false,
  define: { __GRACEUI_BUNDLE__: JSON.stringify(pkg.version) },
  esbuild: { jsx: "automatic", jsxImportSource: "preact", legalComments: "none" },
  server: command === "serve" ? { fs: { allow: [join(here, "..")] }, host: "127.0.0.1" } : undefined,
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    sourcemap: false,
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    modulePreload: { polyfill: false },
    reportCompressedSize: false,
  },
  plugins: [bundledPackages()],
}));
