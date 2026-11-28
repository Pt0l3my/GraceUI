#!/usr/bin/env python3
"""GraceUI's checks of D110, invariant 16 and the ABI's append-only rule, until CI runs them (§2.9 asks for a grep and a
clang-tidy rule; this is the grep). Run it with python3 -I from anywhere:

    python3 -I tools/check_safety.py                      # the source checks
    python3 -I tools/check_safety.py --exports DLL        # and a release build's export table: the two exports only;
                                                          # and its imports, against the reviewed list (D170)
    python3 -I tools/check_safety.py --exports DLL --test # a test build's: those two and GraceUI_Test_* only
    python3 -I tools/check_safety.py --abi-self-test      # shows that check 8 refuses what it must, and nothing else

The source checks, over src/, examples/ and tools/grace-dev/ (our own code; third-party code is fetched into the build
tree and not looked at; the web app's node_modules/ and dist/ are never walked):
  1. reinterpret_cast, const_cast and std::bit_cast appear only in grace-host's abi, hooks and draw files, the DLL's
     abi files (D110), graceui-sys's headers (a mod's side: GetProcAddress's answer made a function) and grace-serve's
     Winsock file (a SOCKET made the HANDLE SetHandleInformation takes, so that no socket is inherited, §7.2 item 24),
     and grace-host's steam.cpp (Steam's flat exports made functions, for its floating keyboard, D275), each with a "// SAFETY:" comment on its statement or in the three lines before it; a C file's pointer casts only in
     those files too.
  2. Each of those files that holds raw pointers from callers, casts or hook code carries a SAFETY comment.
  3. MinHook is included and called only in grace-host's hooks files, and so is any other way of hooking a window
     (SetWindowLongPtr, SetWindowsHookEx), and Dear ImGui's backends only in grace-host (invariant 16): graceui-sys and
     the example mods, a mod's side, hook nothing; only grace-host's CMakeLists.txt links MinHook or Dear ImGui with
     its backends (the module graph). Dear ImGui's core alone (gu_imgui_core: no backend, D76) is linked only by the
     example mods and the spike observer (tools/grace-spike, a mod's side, D175), and its target compiles no backend.
  4. Only the DLL's abi files export anything (the public header declares the two exports).
  5. hooks.cpp's hook table names only functions of D102's published list, and is the only place MinHook makes a hook.
  6. No project that EldenNet's O47 and O48 exclude is named in a build file (D106).
  7. The pure modules (grace-input, grace-abi, grace-tokens and grace-serve's core) include no Windows header, no Dear
     ImGui and no MinHook, and their libraries link nothing (G15 (a), D124, D131, D158): their unit tests build on
     Linux, which proves the first, and this check names the line. grace-serve's I/O, in src/grace-serve/io/ (its own
     library, grace_serve), is not pure; so grace_serve_core compiles nothing from io/, and no file of the core includes
     io/'s headers or grace_serve's own (server.h, space.h, files.h).
  8. graceui.h only grows against ABI 1.0's first header, src/grace-abi/baseline/graceui-abi1-0.h (D109, D132): every
     constant keeps its value (GRACEUI_ABI1_MINOR may only rise), every typedef and export its declaration, and every
     struct, the table GraceUiApi1 among them, keeps its members in their order, adding members only at its end; a slot
     of the table declared as GraceUiReservedSlot may be given its type, keeping its name and place.
  9. grace-dev (tools/grace-dev/, D168) is a developer's tool, not a pure module and no part of graceui.dll: checks 1
     to 4 hold for its code as for src/'s, so it casts, hooks and exports nothing; its library, grace_dev_core, is
     linked by grace-dev and its tests alone; and it makes no shared library, so nothing of it is ever loaded by a game.
Exit status 0 when every check passes; each failure is printed with its file and line.
"""
import os
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
EXAMPLES = ROOT / "examples"
GRACE_DEV = ROOT / "tools" / "grace-dev"  # a tool, held to the source checks as src/ is (check 9)

ABI_HEADER = "src/grace-abi/include/graceui.h"
ABI_BASELINE = "src/grace-abi/baseline/graceui-abi1-0.h"

# D110: the files where raw pointers from callers, casts and hook code may live; and graceui-sys's headers, the mod's
# side of the boundary, whose one cast turns GetProcAddress's answer into the export's type.
ALLOWED = re.compile(
    r"^src/(grace-host/(abi|hooks|draw)[^/]*|graceui/abi[^/]*)\.(cpp|c|h)$|^src/graceui-sys/include/[^/]+\.(h|hpp)$"
    r"|^src/grace-serve/io/platform_win32\.cpp$|^src/grace-host/steam\.cpp$")
HOOK_FILES = re.compile(r"^src/grace-host/hooks[^/]*\.(cpp|c|h)$")
GRACE_HOST_FILES = re.compile(r"^src/grace-host/")
DLL_ABI_FILES = re.compile(r"^src/graceui/abi[^/]*\.cpp$")

CPP_CASTS = re.compile(r"\b(reinterpret_cast|const_cast|bit_cast)\s*<")
C_POINTER_CAST = re.compile(r"\(\s*(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\*+\s*\)|\(\s*grace_host_fn\s*\)")
SAFETY = "SAFETY:"

# D102's published list: the functions GraceUI hooks, append-only. The render half installs the first five.
PUBLISHED_HOOKS = {
    "IDXGISwapChain::Present",
    "IDXGISwapChain1::Present1",
    "IDXGISwapChain::ResizeBuffers",
    "IDXGISwapChain3::ResizeBuffers1",
    "ID3D12CommandQueue::ExecuteCommandLists",
    "the game's window procedure",
    "IDirectInputDevice8::GetDeviceState",
    "XInputGetState",
}

EXCLUDED_PROJECTS = re.compile(r"\bme3\b|fromsoftware-rs|fromsoftware_rs", re.IGNORECASE)

# The pure modules and their libraries: no Win32, no Dear ImGui, no MinHook (D124, D131).
PURE_MODULES = {"src/grace-input/": ("grace_input",), "src/grace-abi/": ("grace_abi", "grace_abi_rules"),
                "src/grace-tokens/": ("grace_tokens",), "src/grace-protocol/": ("grace_protocol",),
                "src/grace-serve/": ("grace_serve_core",), "src/grace-pages/": ("grace_pages",),
                "src/grace-view/": ("grace_view",), "src/grace-draw/": ("grace_draw",),
                "src/grace-guard/": ("grace_guard",)}
# What a pure module may link: nlohmann/json, header-only, for grace-protocol (D159); and other pure modules, for the
# in-game views (D162) and a space's settings store (D167).
PURE_MAY_LINK = {"grace_protocol": ("gu_nlohmann_json",), "grace_pages": ("grace_protocol",),
                 "grace_view": ("grace_protocol", "grace_tokens"),
                 "grace_draw": ("grace_view", "grace_tokens", "grace_abi_rules")}
# grace-serve's I/O (grace_serve, S11.2.i): beside its pure core, and not pure itself.
IMPURE_PARTS = ("src/grace-serve/io/", "src/grace-serve/include/grace/serve/server.h",
                "src/grace-serve/include/grace/serve/space.h", "src/grace-serve/include/grace/serve/files.h")
SERVE_IO_HEADERS = re.compile(r'#\s*include\s*[<"](?:[^">]*/)?(?:platform|internal|engine)\.h[">]|'
                              r'#\s*include\s*[<"]grace/serve/(?:server|space|files)\.h[">]')
IMPURE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](windows|winuser|windef|winbase|imm|dinput|xinput|d3d1\d|dxgi[0-9_]*|psapi|tlhelp32|'
    r'imgui[a-z_]*|MinHook)\.h[">]', re.IGNORECASE)

failures = []


def fail(path, line, message):
    failures.append(f"{path}:{line}: {message}")


# Folders never walked: build trees, worktrees, and the web app's installed packages and build output (D166), which
# are third-party code and generated files, never ours to check here.
SKIPPED_DIRS = ("node_modules", "dist", ".git")


def project_files(name):
    """Every file called `name` in the repository, skipping build trees, .claude/ and the web app's node_modules/ and
    dist/ without descending into them."""
    found = []
    for folder, dirs, files in os.walk(ROOT):
        rel = pathlib.Path(folder).relative_to(ROOT).as_posix()
        dirs[:] = sorted(d for d in dirs if d not in SKIPPED_DIRS and not (rel == "." and (d.startswith("build") or
                                                                                         d == ".claude")))
        if name in files:
            found.append(pathlib.Path(folder) / name)
    return sorted(found)


def own_sources():
    for folder in (SRC, EXAMPLES, GRACE_DEV):
        for path in sorted(folder.rglob("*")):
            rel = path.relative_to(ROOT).as_posix()
            if path.suffix in (".cpp", ".c", ".h", ".hpp") and path.is_file() and not rel.startswith(
                    "src/grace-abi/baseline/"):
                yield path, rel


def comment_free(line, in_c_comment):
    """The line without comments (// and /* */), and whether a /* */ comment runs on past it."""
    out = []
    i = 0
    while i < len(line):
        if in_c_comment:
            end = line.find("*/", i)
            if end < 0:
                return "".join(out), True
            i = end + 2
            in_c_comment = False
        elif line.startswith("//", i):
            break
        elif line.startswith("/*", i):
            in_c_comment = True
            i += 2
        elif line[i] == '"':
            end = i + 1
            while end < len(line) and line[end] != '"':
                end += 2 if line[end] == "\\" else 1
            out.append(" ")
            i = end + 1
        else:
            out.append(line[i])
            i += 1
    return "".join(out), in_c_comment


def safety_near(lines, index):
    """A SAFETY comment on the statement that holds line `index`, or in the three lines before that statement."""
    start = index
    while start > 0:
        previous = lines[start - 1].split("//", 1)[0].rstrip()
        if previous == "" or previous.endswith((";", "{", "}")):
            break
        start -= 1
    return any(SAFETY in lines[j] for j in range(max(0, start - 3), index + 1))


def check_sources():
    for path, rel in own_sources():
        lines = path.read_text(encoding="utf-8").splitlines()
        allowed = bool(ALLOWED.match(rel))
        has_safety = any(SAFETY in line for line in lines)
        needs_safety = False
        in_comment = False
        for number, raw in enumerate(lines, 1):
            code, in_comment = comment_free(raw, in_comment)
            if CPP_CASTS.search(code):
                needs_safety = True
                if not allowed:
                    fail(rel, number, "a reinterpret_cast, const_cast or bit_cast outside the files D110 allows")
                elif not safety_near(lines, number - 1):
                    fail(rel, number, "a cast with no SAFETY comment on its statement or the three lines before")
            if path.suffix == ".c" and C_POINTER_CAST.search(code):
                needs_safety = True
                if not allowed:
                    fail(rel, number, "a pointer cast in C outside the files D110 allows")
            include = raw.strip() if raw.lstrip().startswith("#") and not in_comment else ""
            if re.search(r'#\s*include\s*[<"]MinHook\.h[">]', include) or re.search(r"\bMH_[A-Z][A-Za-z]+\s*\(", code):
                needs_safety = True
                if not HOOK_FILES.match(rel):
                    fail(rel, number, "MinHook outside grace-host's hooks files (invariant 16)")
            if re.search(r"\b(SetWindowLongPtr[AW]?|SetWindowLong[AW]?|SetClassLongPtr[AW]?|SetWindowsHookEx[AW]?)\s*\(",
                         code) and not HOOK_FILES.match(rel):
                fail(rel, number, "a window hooked outside grace-host's hooks files (invariant 16)")
            if re.search(r'#\s*include\s*[<"]imgui_impl_', include) and not GRACE_HOST_FILES.match(rel):
                fail(rel, number, "a Dear ImGui backend outside grace-host (invariant 16)")
            if re.search(r"__declspec\s*\(\s*dllexport\s*\)|\bGRACEUI_API\b\s+[A-Za-z_].*\(", code):
                if not DLL_ABI_FILES.match(rel) and not rel.startswith("src/grace-abi/include/"):
                    fail(rel, number, "an export outside the DLL's abi files")
        if allowed and needs_safety and not has_safety:
            fail(rel, 1, "raw pointers, casts or hook code with no SAFETY comment in the file (D110)")


def check_module_graph():
    for path in project_files("CMakeLists.txt"):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(("build", ".claude/")):
            continue
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            code = line.split("#", 1)[0]
            if re.search(r"\bgu_(minhook|imgui)\b", code) and "target_link_libraries" in text:
                if rel != "src/grace-host/CMakeLists.txt" and re.search(r"target_link_libraries", code):
                    fail(rel, number, "a module other than grace-host links MinHook or Dear ImGui (invariant 16)")
    for path in [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").glob("*.cmake")),
                 *sorted(SRC.rglob("CMakeLists.txt")), *sorted((ROOT / "tools").rglob("CMakeLists.txt")),
                 *sorted(EXAMPLES.rglob("CMakeLists.txt"))]:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if EXCLUDED_PROJECTS.search(line):
                fail(path.relative_to(ROOT).as_posix(), number, "names a project EldenNet's O47 or O48 excludes")


def check_imgui_core():
    """Dear ImGui's core alone is a mod's (D76): only the example mods and the spike observer (a test mod, D175) link it,
    and it compiles no backend."""
    for path in project_files("CMakeLists.txt"):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(("build", ".claude/")):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("#", 1)[0]
            if re.search(r"\bgu_imgui_core\b", code) and "target_link_libraries" in code and \
                    not rel.startswith(("examples/", "tools/grace-spike/")):
                fail(rel, number, "Dear ImGui's core linked outside the example mods and the spike observer (D76)")
    third_party = (ROOT / "cmake" / "third_party.cmake").read_text(encoding="utf-8")
    block = re.search(r"add_library\(gu_imgui_core\b(.*?)\)", third_party, re.S)
    if block is None:
        fail("cmake/third_party.cmake", 0, "gu_imgui_core, Dear ImGui's core alone, is missing")
    elif "backends" in block.group(1):
        fail("cmake/third_party.cmake", 0, "gu_imgui_core compiles a Dear ImGui backend (invariant 16)")


def check_grace_dev():
    """Check 9: grace-dev's library is its own and its tests', and grace-dev makes no shared library (D168)."""
    own = "tools/grace-dev/CMakeLists.txt"
    for path in sorted(ROOT.rglob("CMakeLists.txt")):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(("build", ".claude/")):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("#", 1)[0]
            if re.search(r"\bgrace_dev_core\b", code) and "target_link_libraries" in code and rel != own:
                fail(rel, number, "grace-dev's library linked outside grace-dev (D168)")
            if rel == own and re.search(r"add_library\s*\([^)]*\b(SHARED|MODULE)\b", code):
                fail(rel, number, "grace-dev makes a shared library: it is a program alone (D168)")


def check_pure_modules():
    for module, libraries in PURE_MODULES.items():
        folder = ROOT / module
        linking = re.compile(r"target_link_libraries\s*\(\s*(" + "|".join(libraries) + r")\b(.*)")
        for path in sorted(folder.rglob("*")):
            rel = path.relative_to(ROOT).as_posix()
            if rel.startswith(IMPURE_PARTS):
                continue
            if path.suffix in (".cpp", ".c", ".h"):
                for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                    if IMPURE_INCLUDE.search(line):
                        fail(rel, number, "a pure module includes Win32, Dear ImGui or MinHook (D124)")
                    if module == "src/grace-serve/" and SERVE_IO_HEADERS.search(line):
                        fail(rel, number, "grace-serve's pure core includes a header of its I/O (D158)")
            elif path.name == "CMakeLists.txt":
                for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                    code = line.split("#", 1)[0]
                    found = linking.search(code)
                    if found:
                        linked = set(found.group(2).replace(")", " ").split()) - {"PUBLIC", "PRIVATE", "INTERFACE"}
                        if linked - set(PURE_MAY_LINK.get(found.group(1), ())):
                            fail(rel, number, "a pure module's library links a library (D124)")
                core = re.search(r"add_library\(grace_serve_core\b(.*?)\)", path.read_text(encoding="utf-8"), re.S)
                if core is not None and "io/" in core.group(1):
                    fail(rel, 0, "grace_serve_core compiles a file of grace-serve's I/O (D158)")


# ---- Check 8: graceui.h only grows against ABI 1.0's first header (D109, D132).

RESERVED_SLOT = "GraceUiReservedSlot"
GROWING_DEFINES = {"GRACEUI_ABI1_MINOR"}  # may rise with each minor, never fall


def _without_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def _tokens(text):
    return " ".join(re.findall(r"[A-Za-z_]\w*|\d\w*|\"[^\"]*\"|\S", text))


def _declared_name(member):
    pointer = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", member)
    if pointer:
        return pointer.group(1)
    names = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[[^\]]*\]", "", member))
    return names[-1] if names else ""


def _members(body):
    """A struct's members, one per declarator: "float x0, y0" is two members, "float x0" and "float y0"."""
    out = []
    for declaration in body.split(";"):
        declaration = " ".join(declaration.split())
        if not declaration:
            continue
        if "(" in declaration or "," not in declaration:
            out.append(_tokens(declaration))
            continue
        first, *rest = [part.strip() for part in declaration.split(",")]
        name = _declared_name(first)
        base = first[: first.rfind(name)]
        out.append(_tokens(first))
        out.extend(_tokens(base + part) for part in rest)
    return out


def abi_parts(text):
    """The parts of a public header the append-only rule compares: #defines, structs, other typedefs and exports."""
    code = _without_comments(text)
    defines = {}
    for match in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\([^)]*\))?((?:[^\n]*\\\n)*[^\n]*)$", code, re.M):
        name, parameters, body = match.group(1), match.group(2) or "", match.group(3).replace("\\\n", " ")
        if name != "GRACEUI_H":  # the include guard
            defines[name] = _tokens(parameters + " " + body)
    structs = {}
    for match in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", code, re.S):
        structs[match.group(3)] = _members(match.group(2))
    without_structs = re.sub(r"typedef\s+struct\s+\w+\s*\{.*?\}\s*\w+\s*;", " ", code, flags=re.S)
    without_structs = re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*$", " ", without_structs, flags=re.M)  # no directive
    typedefs = {}
    for match in re.finditer(r"typedef\s+([^;{}]*?);", without_structs, re.S):
        declaration = _tokens(match.group(1))
        typedefs[_declared_name(match.group(1))] = declaration
    exports = {}
    for match in re.finditer(r"\bGRACEUI_API\s+([^;{}#]*?\([^;{}]*?\))\s*;", without_structs, re.S):
        declaration = _tokens(match.group(1))
        exports[re.search(r"([A-Za-z_]\w*)\s*\(", match.group(1)).group(1)] = declaration
    return {"defines": defines, "structs": structs, "typedefs": typedefs, "exports": exports}


def abi_breaks(baseline_text, current_text):
    """What `current_text` changes of `baseline_text` that the append-only rule forbids: a list of reasons."""
    old, new = abi_parts(baseline_text), abi_parts(current_text)
    breaks = []
    for name, body in old["defines"].items():
        if name not in new["defines"]:
            breaks.append(f"#define {name} is gone")
        elif name in GROWING_DEFINES:
            try:
                if int(new["defines"][name].rstrip("uU"), 0) < int(body.rstrip("uU"), 0):
                    breaks.append(f"{name} fell from {body} to {new['defines'][name]}")
            except ValueError:
                breaks.append(f"{name} is not a number")
        elif new["defines"][name] != body:
            breaks.append(f"#define {name} changed from '{body}' to '{new['defines'][name]}'")
    for kind in ("typedefs", "exports"):
        for name, declaration in old[kind].items():
            if new[kind].get(name) != declaration:
                breaks.append(f"{kind[:-1]} {name} changed or is gone: '{declaration}'")
    for name, members in old["structs"].items():
        current = new["structs"].get(name)
        if current is None:
            breaks.append(f"struct {name} is gone")
            continue
        if len(current) < len(members):
            breaks.append(f"struct {name} shrinks: {len(members)} members, now {len(current)}")
        for index, (was, now) in enumerate(zip(members, current)):
            if was == now:
                continue
            if name == "GraceUiApi1" and was.split(" ", 1)[0] == RESERVED_SLOT and \
                    _declared_name(was) == _declared_name(now):
                continue  # a reserved slot given its type, in its place
            breaks.append(f"struct {name}'s member {index} changed from '{was}' to '{now}' (a reordered, removed, "
                          f"inserted or retyped member)")
            break
    return breaks


def check_abi():
    header, baseline = ROOT / ABI_HEADER, ROOT / ABI_BASELINE
    if not baseline.is_file():
        fail(ABI_BASELINE, 0, "ABI 1.0's first header is missing (D132)")
        return
    for reason in abi_breaks(baseline.read_text(encoding="utf-8"), header.read_text(encoding="utf-8")):
        fail(ABI_HEADER, 0, f"breaks ABI 1's append-only rule against {ABI_BASELINE}: {reason}")


def abi_self_test():
    """Check 8 refuses each kind of break and lets each kind of growth through, on edits of today's header."""
    text = (ROOT / ABI_HEADER).read_text(encoding="utf-8")
    log_slot = re.search(r"\n  GraceUiStatus \(\*log\)\([^;]*;", text).group(0)
    publish = "\n  GraceUiReservedSlot publish;"
    poll = "\n  GraceUiReservedSlot poll;"
    minor_one = text.replace("#define GRACEUI_ABI1_MINOR 0u", "#define GRACEUI_ABI1_MINOR 1u")
    # (what, allowed, the baseline, the header compared with it)
    cases = [
        ("the table shrinks", False, text, text.replace(log_slot, "")),
        ("two slots swap", False, text, text.replace(publish + poll, poll + publish)),
        ("a slot retyped", False, text,
         text.replace("GraceUiStatus (*notice)(GraceUiConsumer consumer, const GraceUiNotice*",
                      "GraceUiStatus (*notice)(GraceUiConsumer consumer, const void*")),
        ("a status renumbered", False, text, text.replace("#define GRACEUI_OK 0u", "#define GRACEUI_OK 9u")),
        ("a constant gone", False, text, text.replace("#define GRACEUI_TONE_DANGER 2u", "")),
        ("a struct gone", False, text,
         re.sub(r"typedef struct GraceUiWorldState \{.*?\} GraceUiWorldState;", "", text, flags=re.S)),
        ("a member inserted", False, text,
         text.replace("  uint32_t reason;           /*", "  uint32_t inserted;\n  uint32_t reason;           /*")),
        ("one of several declarators gone", False, text, text.replace("float x0, y0, x1, y1;", "float x0, y0, x1;")),
        ("an export changed", False, text,
         text.replace("GraceUI_RequestApi(uint32_t major);", "GraceUI_RequestApi(uint64_t major);")),
        ("a typedef changed", False, text,
         text.replace("typedef uint64_t GraceUiConsumer;", "typedef uint32_t GraceUiConsumer;")),
        ("the minor falls", False, minor_one, text),
        ("a slot appended", True, text, text.replace(log_slot, log_slot + "\n  GraceUiStatus (*later)(GraceUiConsumer c);")),
        ("a member appended", True, text, text.replace("  uint64_t text_length;\n} GraceUiNotice;",
                                                       "  uint64_t text_length;\n  uint32_t later;\n} GraceUiNotice;")),
        ("a reserved slot typed", True, text, text.replace(poll, "\n  GraceUiStatus (*poll)(GraceUiConsumer c, void* e);")),
        ("the minor rises", True, text, minor_one),
        ("a constant added", True, text, text.replace("#define GRACEUI_TONE_DANGER 2u",
                                                      "#define GRACEUI_TONE_DANGER 2u\n#define GRACEUI_TONE_LATER 3u")),
        ("comments and spacing only", True, text, text.replace("uint32_t size;", "uint32_t   size ;  /* changed */")),
    ]
    wrong = 0
    for name, allowed, baseline, edited in cases:
        if edited == baseline:
            print(f"self-test: '{name}' did not edit the header (has its text moved?)")
            wrong += 1
            continue
        breaks = abi_breaks(baseline, edited)
        if bool(breaks) == allowed:
            print(f"self-test: '{name}' was {'refused' if breaks else 'let through'}: {breaks}")
            wrong += 1
    print("check_safety --abi-self-test: " + ("ok" if not wrong else f"{wrong} wrong"))
    return 0 if not wrong else 1


def check_hook_table():
    hooks = SRC / "grace-host" / "hooks.cpp"
    text = hooks.read_text(encoding="utf-8")
    table = re.search(r"kHookNames\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if table is None:
        fail("src/grace-host/hooks.cpp", 1, "the hook table (kHookNames) is missing")
        return
    names = re.findall(r'"([^"]+)"', table.group(1))
    for name in names:
        if name not in PUBLISHED_HOOKS:
            fail("src/grace-host/hooks.cpp", 1, f"{name} is not in D102's published list")
    if text.count("MH_CreateHook(") != 1:
        fail("src/grace-host/hooks.cpp", 1, "hooks are created somewhere other than the one loop over the table")


# graceui.dll's imports (invariant 16, D170), reviewed: the system's own DLLs a windowed process has loaded, the
# server's sockets and random source, Dear ImGui's Win32 backend's dwmapi, and MinGW's C runtime. No Direct3D DLL:
# CreateDXGIFactory1, D3D12CreateDevice and D3DCompile are bound at run time on the init thread, after the game's window,
# since loading dxgi.dll early can hang Wine's loader (D170).
IMPORTS_ALLOWED = {"kernel32.dll", "user32.dll", "gdi32.dll", "imm32.dll", "ws2_32.dll", "bcrypt.dll", "dwmapi.dll",
                   "msvcrt.dll"}


def pe_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("not a PE32+ (x64) file")
    table = optional + optional_size
    spans = [struct.unpack_from("<IIII", data, table + 40 * i + 8) for i in range(sections)]

    def offset(rva):
        for size, address, raw_size, raw_offset in spans:
            if address <= rva < address + max(size, raw_size):
                return rva - address + raw_offset
        raise ValueError("an address outside every section")

    return optional, offset


def imported_dlls(dll):
    data = pathlib.Path(dll).read_bytes()
    optional, offset = pe_sections(data)
    import_rva = struct.unpack_from("<I", data, optional + 120)[0]
    if import_rva == 0:
        return []
    names = []
    entry = offset(import_rva)
    while True:
        name_rva = struct.unpack_from("<I", data, entry + 12)[0]
        if name_rva == 0:
            return names
        start = offset(name_rva)
        names.append(data[start:data.index(b"\0", start)].decode("ascii"))
        entry += 20


def exported_names(dll):
    data = pathlib.Path(dll).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("not a PE32+ (x64) file")
    export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
    if export_rva == 0:
        return []
    table = optional + optional_size
    spans = [struct.unpack_from("<IIII", data, table + 40 * i + 8) for i in range(sections)]

    def offset(rva):
        for size, address, raw_size, raw_offset in spans:
            if address <= rva < address + max(size, raw_size):
                return rva - address + raw_offset
        raise ValueError("an address outside every section")

    directory = offset(export_rva)
    count, names_rva = struct.unpack_from("<I", data, directory + 24)[0], struct.unpack_from("<I", data, directory + 32)[0]
    names = []
    for i in range(count):
        name_rva = struct.unpack_from("<I", data, offset(names_rva) + 4 * i)[0]
        start = offset(name_rva)
        names.append(data[start:data.index(b"\0", start)].decode("ascii"))
    return names


def check_exports(dll, test_build):
    names = set(exported_names(dll))
    public = {"GraceUI_Version", "GraceUI_RequestApi"}
    test_only = {name for name in names if name.startswith("GraceUI_Test_")}
    others = names - public - test_only
    if not public <= names:
        fail(dll, 0, f"lacks {sorted(public - names)}")
    if others:
        fail(dll, 0, f"exports more than GraceUI's ABI: {sorted(others)}")
    if test_only and not test_build:
        fail(dll, 0, f"a release build with test exports: {sorted(test_only)}")
    if test_build and not test_only:
        fail(dll, 0, "a test build without its test exports")
    print(f"{pathlib.Path(dll).name} exports: {', '.join(sorted(names))}")
    imports = imported_dlls(dll)
    unreviewed = sorted(name for name in imports if name.lower() not in IMPORTS_ALLOWED)
    if unreviewed:
        fail(dll, 0, f"imports DLLs outside the reviewed list (invariant 16, D170): {unreviewed}")
    print(f"{pathlib.Path(dll).name} imports: {', '.join(sorted(imports, key=str.lower))}")


def main(argv):
    if "--abi-self-test" in argv:
        return abi_self_test()
    check_sources()
    check_module_graph()
    check_imgui_core()
    check_pure_modules()
    check_grace_dev()
    check_hook_table()
    check_abi()
    if "--exports" in argv:
        check_exports(argv[argv.index("--exports") + 1], "--test" in argv)
    for failure in failures:
        print(failure)
    print("check_safety: " + ("ok" if not failures else f"{len(failures)} failure(s)"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
