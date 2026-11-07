# GraceUI research notes

The evidence behind [GraceUI's design](../DESIGN.md). Each note is in our own words. Third-party facts are kept, with a
pointer to where each was read; no third-party code, CSS, markup, text or art is copied into them or anywhere in this
repository.

| Code | Note | What it covers |
|---|---|---|
| **ML** | [magelight.md](magelight.md) | Magelight UI 1.7.1 (MIT host and SDK, zlib GPU driver, on the proprietary Ultralight engine): its architecture, what its changelog shows went wrong, and 30 lessons for a GraceUI host in Elden Ring (Direct3D 12, one hudhook overlay, Proton) |
| **LOOK** | [severactions-look.md](severactions-look.md) | SeverActions' visual language, described (it has no licence, so nothing is reused), and the proposed Grace theme: palette, contrast, fonts, type scale, spacing, components, states, motion, accessibility, scale and HDR |
| **IA** | [information-architecture.md](information-architecture.md) | The first proposal's sidebar of flavour-named sections and pages (superseded by DESIGN §4's framework categories, D38), every EldenNet setting and view mapped to a home, each page's contents, the spoiler rules (SP1–SP9) and privacy rules (PV1–PV8), and what GraceUI needs from EldenNet; the mapping and the rules still stand |
| **REN** | [renderers.md](renderers.md) | In-game renderer options (Dear ImGui, Ultralight, CEF, RmlUi, Blitz, Servo and others) against hudhook 0.9.3's limits, and the hardening of the local web server |
| **SND** | [skyrimnet-dashboard.md](skyrimnet-dashboard.md) | How SkyrimNet's dashboard groups an AI framework: its sidebar categories and pages, top bar, page parts, Home cards, settings groups and kinds of setting, each fact tagged with its source; and what EldenNet takes, adapts, defers or leaves out. The evidence for DESIGN §4 (D38–D48) |

## Citation conventions

- **GraceUI's own design** is cited as "GraceUI DESIGN §n" in these notes, and its Open rows and decisions as G1–G10 and
  D1–D66.
- **EldenNet's documents** are cited by EldenNet's own codes: DESIGN (its master design), ARCH (architecture), NPC
  (NPCs and dialogue), MEM (memory and knowledge), and research codes such as ER02 and SN01. They live in the EldenNet
  repository, under `docs/design/` and `docs/research/`, and are read there with its `wp` tool. Its spikes (S0–S10),
  test families (X1–X3) and success criteria (C1–C14) keep their EldenNet names.
- **Third-party sources** use the prefixes each note defines in its §0: for example `S:` and `R:` for Magelight UI's
  source archive and release package, `CL:n` for a line of Magelight's changelog, crate paths such as
  `hudhook-0.9.3/src/...` for published crates, and full URLs for web pages. Labels such as FACT, LIKELY, UNVERIFIED and
  EST mean what REN §0 says. SND tags the owner's screenshots, the devkit's documents and the rendered dashboard as its
  §0.3 lists.

## How the notes were made

- Untrusted copies (Magelight UI's archives, SeverActions' public repository, published crates) were unpacked into
  isolated folders outside any repository, read as text with `grep` or `python3 -I` from scripts kept elsewhere, and
  never built or run.
- SkyrimNet is used for ideas and structure only. ML, LOOK, IA and REN rely on EldenNet's research (SN01 §9) and opened
  none of its files. SND, written later the same day, adds three sources the owner supplied or asked for: the owner's
  screenshots of its dashboard (viewed as images), its devkit's documents (read as text with `grep` in an isolated
  folder, no code beyond what a document quotes), and the dashboard itself, served on 127.0.0.1 and viewed in a
  headless browser with every other request aborted, at the owner's request; no file of the bundle was read, printed
  or copied (SND §0.2). Its text, prompts, code, icons, styling and theme names are not reproduced.
- Owner-supplied material (a screenshot of SeverActions' menu, the SkyrimNet screenshots) is described, not committed.
  Personal details visible in it (an account name, local addresses, a local path) are not recorded.
- Each note was written on 2026-11-07 and edited the same day to stand alone in this repository. Web facts marked
  [search] should be re-read at their source before the design relies on them.
