// SPDX-License-Identifier: MIT
// env.d.ts: what Vite gives the app at build time (import.meta.env, the bundle's version) and its CSS imports.
interface ImportMetaEnv {
  readonly DEV: boolean;
  readonly PROD: boolean;
  readonly MODE: string;
}
interface ImportMeta {
  readonly env: ImportMetaEnv;
}
declare const __GRACEUI_BUNDLE__: string;
declare module "*.css" {}
