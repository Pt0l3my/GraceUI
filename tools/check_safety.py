#!/usr/bin/env python3
"""GraceUI's checks of D110 and invariant 16, until CI runs them (§2.9 asks for a grep and a clang-tidy rule; this is
the grep). Run it with python3 -I from anywhere:

    python3 -I tools/check_safety.py                      # the source checks
    python3 -I tools/check_safety.py --exports DLL        # and a release build's export table: the two exports only
    python3 -I tools/check_safety.py --exports DLL --test # a test build's: those two and GraceUI_Test_* only

The source checks, over src/ (our own code; third-party code is fetched into the build tree and not looked at):
  1. reinterpret_cast, const_cast and std::bit_cast appear only in grace-host's abi, hooks and draw files and the DLL's
     abi files (D110), each with a "// SAFETY:" comment on its statement or in the three lines before it; a C file's
     pointer casts only in those files too.
  2. Each of those files that holds raw pointers from callers, casts or hook code carries a SAFETY comment.
  3. MinHook is included and called only in grace-host's hooks files, and so is any other way of hooking a window
     (SetWindowLongPtr, SetWindowsHookEx), and Dear ImGui's backends only in grace-host (invariant 16); only
     grace-host's CMakeLists.txt links MinHook or Dear ImGui (the module graph).
  4. Only the DLL's abi files export anything.
  5. hooks.cpp's hook table names only functions of D102's published list, and is the only place MinHook makes a hook.
  6. No project that EldenNet's O47 and O48 exclude is named in a build file (D106).
  7. The pure modules (grace-input) include no Windows header, no Dear ImGui and no MinHook, and link nothing (G15 (a),
     D124): their unit tests build on Linux, which proves the first, and this check names the line.
Exit status 0 when every check passes; each failure is printed with its file and line.
"""
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

# D110: the files where raw pointers from callers, casts and hook code may live.
ALLOWED = re.compile(r"^src/(grace-host/(abi|hooks|draw)[^/]*|graceui/abi[^/]*)\.(cpp|c|h)$")
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

# The pure modules: no Win32, no Dear ImGui, no MinHook (D124).
PURE_MODULES = ("src/grace-input/",)
IMPURE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](windows|winuser|windef|winbase|imm|dinput|xinput|d3d1\d|dxgi[0-9_]*|imgui[a-z_]*|MinHook)'
    r'\.h[">]', re.IGNORECASE)

failures = []


def fail(path, line, message):
    failures.append(f"{path}:{line}: {message}")


def own_sources():
    for path in sorted(SRC.rglob("*")):
        if path.suffix in (".cpp", ".c", ".h") and path.is_file():
            yield path, path.relative_to(ROOT).as_posix()


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
    for path in sorted(ROOT.rglob("CMakeLists.txt")):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith("build"):
            continue
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            code = line.split("#", 1)[0]
            if re.search(r"\bgu_(minhook|imgui)\b", code) and "target_link_libraries" in text:
                if rel != "src/grace-host/CMakeLists.txt" and re.search(r"target_link_libraries", code):
                    fail(rel, number, "a module other than grace-host links MinHook or Dear ImGui (invariant 16)")
    for path in [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").glob("*.cmake")),
                 *sorted(SRC.rglob("CMakeLists.txt")), *sorted((ROOT / "tools").rglob("CMakeLists.txt"))]:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if EXCLUDED_PROJECTS.search(line):
                fail(path.relative_to(ROOT).as_posix(), number, "names a project EldenNet's O47 or O48 excludes")


def check_pure_modules():
    for module in PURE_MODULES:
        folder = ROOT / module
        for path in sorted(folder.rglob("*")):
            rel = path.relative_to(ROOT).as_posix()
            if path.suffix in (".cpp", ".c", ".h"):
                for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                    if IMPURE_INCLUDE.search(line):
                        fail(rel, number, "a pure module includes Win32, Dear ImGui or MinHook (D124)")
            elif path.name == "CMakeLists.txt":
                for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                    code = line.split("#", 1)[0]
                    if re.search(r"target_link_libraries\s*\(\s*grace_input\b", code):
                        fail(rel, number, "a pure module links a library (D124)")


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


def main(argv):
    check_sources()
    check_module_graph()
    check_pure_modules()
    check_hook_table()
    if "--exports" in argv:
        check_exports(argv[argv.index("--exports") + 1], "--test" in argv)
    for failure in failures:
        print(failure)
    print("check_safety: " + ("ok" if not failures else f"{len(failures)} failure(s)"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
