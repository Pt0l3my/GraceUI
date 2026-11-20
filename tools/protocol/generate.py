#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/protocol/generate.py: the bridge protocol's types, from src/grace-protocol/protocol.toml (GraceUI DESIGN §6,
D108, D159).

    python3 -I tools/protocol/generate.py           write the C++ types and decoders and the TypeScript types
    python3 -I tools/protocol/generate.py --check   fail if an output is not what the schema generates

Outputs: src/grace-protocol/include/grace/protocol/messages.h, src/grace-protocol/messages.cpp (the C++ types, each
with a decoder that checks every field's type and bounds and an encoder that writes the schema's order) and
web/src/protocol.ts (the same types for the web app). The schema is checked first: every type it names exists, every
string has a bound, every list a bound, no field twice.
"""
import argparse
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SCHEMA = ROOT / "src" / "grace-protocol" / "protocol.toml"
HEADER = ROOT / "src" / "grace-protocol" / "include" / "grace" / "protocol" / "messages.h"
SOURCE = ROOT / "src" / "grace-protocol" / "messages.cpp"
TYPESCRIPT = ROOT / "web" / "src" / "protocol.ts"

SCALARS = {"string", "id", "u32", "u64", "f64", "bool", "json", "object"}
FROM = {"page", "grace", "space", "both"}
CPP_KEYWORDS = {"bool", "default", "int", "float", "double", "char", "new", "delete", "class", "struct", "public",
                "private", "template", "this", "true", "false", "union", "enum", "switch", "case", "min", "max",
                "and", "or", "not", "export", "auto", "register", "typename", "operator"}
FRAME_FROM_PAGE_BOUND = None  # set from the limits: a json field bounded at or past a frame is not measured


class SchemaError(Exception):
    pass


def camel(name):
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[._]", name))


def lower_camel(name):
    text = camel(name)
    return text[:1].lower() + text[1:]


def member(name):
    return name + "_" if name in CPP_KEYWORDS else name


# ---- The schema.

def load(text=None):
    data = tomllib.loads(text if text is not None else SCHEMA.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise SchemaError("protocol: schema is not 1")
    enums = data.get("enums", {})
    types = data.get("types", {})
    for name, enum in enums.items():
        values = enum.get("values", [])
        if not values or len(set(values)) != len(values):
            raise SchemaError(f"enum {name}: no values, or one twice")
        for value in values:
            if not re.fullmatch(r"[a-z][a-z0-9_]*", value):
                raise SchemaError(f"enum {name}: value {value} is not lower_snake_case")

    def check_fields(where, fields):
        seen = set()
        for f in fields:
            name, kind = f.get("name"), f.get("type")
            if not name or not re.fullmatch(r"[a-z][a-z0-9_]*", name):
                raise SchemaError(f"{where}: a field's name is not lower_snake_case: {name}")
            if name in seen:
                raise SchemaError(f"{where}: field {name} twice")
            if name in ("v", "t") or (name == "space" and where.startswith("message")):
                raise SchemaError(f"{where}: field {name} is the envelope's")
            seen.add(name)
            check_type(f"{where}.{name}", kind, f, item=False)

    def check_type(where, kind, f, item):
        if not isinstance(kind, str):
            raise SchemaError(f"{where}: no type")
        if kind.startswith("list."):
            if item:
                raise SchemaError(f"{where}: a list of lists")
            if not isinstance(f.get("max"), int):
                raise SchemaError(f"{where}: a list needs max items")
            inner = kind[5:]
            if inner == "string" and not isinstance(f.get("item_max"), int):
                raise SchemaError(f"{where}: a list of strings needs item_max")
            check_type(where + "[]", inner, f, item=True)
        elif kind.startswith("enum."):
            if kind[5:] not in enums:
                raise SchemaError(f"{where}: no enum named {kind[5:]}")
        elif kind.startswith("type."):
            if kind[5:] not in types:
                raise SchemaError(f"{where}: no type named {kind[5:]}")
        elif kind in SCALARS:
            if kind == "string" and not item and not isinstance(f.get("max"), int):
                raise SchemaError(f"{where}: a string needs max bytes")
            if kind in ("json", "object") and not item and not isinstance(f.get("max"), int):
                raise SchemaError(f"{where}: a json value needs max bytes")
        else:
            raise SchemaError(f"{where}: unknown type {kind}")

    for name, t in types.items():
        check_fields(f"type {name}", t.get("fields", []))
    for name, m in data.get("messages", {}).items():
        if m.get("from") not in FROM:
            raise SchemaError(f"message {name}: from is not one of {', '.join(sorted(FROM))}")
        if m.get("space") not in (True, False, "optional"):
            raise SchemaError(f"message {name}: space is not true, false or \"optional\"")
        check_fields(f"message {name}", m.get("fields", []))
    for name, c in data.get("commands", {}).items():
        if not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+", name):
            raise SchemaError(f"command {name}: not a dotted name")
        check_fields(f"command {name}", c.get("fields", []))
    order = []  # types in an order where each comes after the types it holds
    visiting = set()

    def visit(name):
        if name in order:
            return
        if name in visiting:
            raise SchemaError(f"type {name} holds itself")
        visiting.add(name)
        for f in types[name].get("fields", []):
            kind = f["type"][5:] if f["type"].startswith("list.") else f["type"]
            if kind.startswith("type."):
                visit(kind[5:])
        visiting.discard(name)
        order.append(name)

    for name in types:
        visit(name)
    data["_type_order"] = order
    return data


# ---- A validator, for Python tools (the page lint, the fake host): a value checked against a type of the schema as
# grace-protocol's decoders check it.

ID = re.compile(r"[a-z0-9._/-]{1,64}")
MAX_SAFE = 2 ** 53 - 1


def json_size(value):
    import json
    return len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def validate(data, value, kind, field=None, path=""):
    """The errors of `value` as a `kind` ("type.page", "list.id", "u32"...): a list of "path: what" strings, empty when
    it would decode. `field` is the schema's field entry, for its bounds."""
    field = field or {}
    errors = []

    def err(why):
        errors.append(f"{path or '(top)'}: {why}")

    if kind.startswith("list."):
        if not isinstance(value, list):
            err("not a list")
        elif "max" in field and len(value) > field["max"]:
            err(f"more than {field['max']} items")
        else:
            inner = dict(field)
            inner.pop("max", None)
            if "item_max" in field:
                inner["max"] = field["item_max"]
            for i, item in enumerate(value):
                errors += validate(data, item, kind[5:], inner, f"{path}[{i}]")
        return errors
    if kind.startswith("enum."):
        if value not in data["enums"][kind[5:]]["values"]:
            err(f"not one of {kind[5:]}'s names")
        return errors
    if kind.startswith("type."):
        if not isinstance(value, dict):
            err("not an object")
            return errors
        for f in data["types"][kind[5:]]["fields"]:
            name = f["name"]
            sub = f"{path}.{name}" if path else name
            if name not in value or value[name] is None:
                if not f.get("optional"):
                    errors.append(f"{sub}: missing")
                continue
            errors += validate(data, value[name], f["type"], f, sub)
        return errors
    if kind in ("string", "id"):
        if not isinstance(value, str):
            err("not a string")
        elif kind == "id" and not ID.fullmatch(value):
            err("not an id (1 to 64 of a-z, 0-9, '.', '_', '-' and '/')")
        elif kind == "string" and "max" in field and len(value.encode("utf-8")) > field["max"]:
            err(f"longer than {field['max']} bytes")
    elif kind in ("u32", "u64"):
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            err("not a whole number of 0 or more")
        elif value > (2 ** 32 - 1 if kind == "u32" else MAX_SAFE):
            err("too large")
    elif kind == "f64":
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            err("not a number")
    elif kind == "bool":
        if not isinstance(value, bool):
            err("not true or false")
    elif kind in ("json", "object"):
        if kind == "object" and not isinstance(value, dict):
            err("not an object")
        elif "max" in field and json_size(value) > field["max"]:
            err(f"larger than {field['max']} bytes")
    return errors


# ---- C++.

def cpp_type(kind):
    if kind.startswith("list."):
        return f"std::vector<{cpp_type(kind[5:])}>"
    if kind.startswith("enum.") or kind.startswith("type."):
        return camel(kind[5:])
    return {"string": "std::string", "id": "std::string", "u32": "std::uint32_t", "u64": "std::uint64_t",
            "f64": "double", "bool": "bool", "json": "Json", "object": "Json"}[kind]


def cpp_field(f):
    t = cpp_type(f["type"])
    return f"std::optional<{t}>" if f.get("optional") else t


def reader(kind, f, item, frame_bound):
    """A C++ expression reading `v` into `o` at `p`, for a value of `kind` (a field's own type, or a list's items)."""
    if kind == "string":
        bound = f["item_max"] if item else f["max"]
        return f"read_string(v, {bound}, o, p, e)"
    if kind == "id":
        return "read_id(v, o, p, e)"
    if kind in ("u32", "u64", "f64", "bool"):
        return f"read_{kind}(v, o, p, e)"
    if kind in ("json", "object"):
        bound = f.get("item_max", frame_bound) if item else f["max"]
        size = "kWithinFrame" if bound >= frame_bound else f"{bound}u"  # within the frame's bound: not measured again
        return f"read_json(v, {size}, {'true' if kind == 'object' else 'false'}, o, p, e)"
    if kind.startswith("enum."):
        return f"read_enum(v, o, p, e)"
    if kind.startswith("type."):
        return f"decode(v, o, p, e)"
    raise SchemaError(kind)


def frame_bounded(f, frame_bound):
    return f["type"] in ("json", "object") and f["max"] >= frame_bound


def decode_body(fields, frame_bound, indent="  ", movable=False):
    """A decoder's field reads. With `movable`, a json field bounded only by its frame is moved out of `in`, which the
    decoder then takes by reference: the largest payloads are never copied."""
    lines = []
    for f in fields:
        name, kind = f["name"], f["type"]
        target = f"out.{member(name)}"
        required = "false" if f.get("optional") else "true"
        if movable and frame_bounded(f, frame_bound):
            lines.append(f'{indent}if (Json* v = field_mut(in, "{name}", {required}, path, e)) {{')
            lines.append(f'{indent}  const std::string p = path.empty() ? std::string("{name}") : path + ".{name}";')
            dest = f"{target}.emplace()" if f.get("optional") else target
            lines.append(f"{indent}  if (!take_json(*v, {'true' if kind == 'object' else 'false'}, {dest}, p, e)) {{")
            lines += [f"{indent}    return false;", f"{indent}  }}", f"{indent}}} else if (!e.field.empty()) {{",
                      f"{indent}  return false;", f"{indent}}}"]
            continue
        lines.append(f'{indent}if (const Json* v = field(in, "{name}", {required}, path, e)) {{')
        lines.append(f'{indent}  const std::string p = path.empty() ? std::string("{name}") : path + ".{name}";')
        if f.get("optional"):
            lines.append(f"{indent}  {target}.emplace();")
            dest = f"*{target}"
        else:
            dest = target
        if kind.startswith("list."):
            inner = kind[5:]
            item = reader(inner, f, True, frame_bound)
            lines.append(f"{indent}  if (!read_list(*v, {f['max']}, {dest}, p, e, [&](const Json& v, "
                         f"{cpp_type(inner)}& o, const std::string& p, DecodeError& e) {{ return {item}; }})) {{")
        else:
            expr = reader(kind, f, False, frame_bound).replace("read_", "read_", 1)
            expr = expr.replace("(v,", "(*v,").replace(", o,", f", {dest},")
            lines.append(f"{indent}  if (!{expr}) {{")
        lines.append(f"{indent}    return false;")
        lines.append(f"{indent}  }}")
        lines.append(f"{indent}}} else if (!e.field.empty()) {{")
        lines.append(f"{indent}  return false;")
        lines.append(f"{indent}}}")
    return lines


def writer(kind, value):
    if kind.startswith("list."):
        inner = kind[5:]
        return (f"[&] {{ Json a = Json::array(); for (const auto& x : {value}) {{ a.push_back({writer(inner, 'x')}); }} "
                f"return a; }}()")
    if kind.startswith("enum."):
        return f"Json(to_string({value}))"
    if kind.startswith("type."):
        return f"encode({value})"
    return f"Json({value})"


def text_value(kind, value):
    """A C++ expression for a value as a Json to append: a json field by reference, so it is never copied."""
    if kind in ("json", "object"):
        return value
    return writer(kind, value)


def encode_text_body(fields, indent="      "):
    lines = []
    for f in fields:
        name, kind = f["name"], f["type"]
        value = f"in.{member(name)}"
        if f.get("optional"):
            lines.append(f"{indent}if ({value}) {{")
            inner_indent, value = indent + "  ", "*" + value
        else:
            inner_indent = indent
        if kind.startswith("list."):
            inner = kind[5:]
            lines += [f'{inner_indent}out += ",\\"{name}\\":[";', f"{inner_indent}for (std::size_t i = 0; i < ({value}).size(); ++i) {{",
                      f"{inner_indent}  if (i != 0) {{", f"{inner_indent}    out += ',';", f"{inner_indent}  }}",
                      f"{inner_indent}  append_json(out, {text_value(inner, f'({value})[i]')});", f"{inner_indent}}}",
                      f"{inner_indent}out += ']';"]
        else:
            lines.append(f'{inner_indent}append_field(out, "{name}", {text_value(kind, value)});')
        if f.get("optional"):
            lines.append(f"{indent}}}")
    return lines


def encode_body(fields, indent="  "):
    lines = []
    for f in fields:
        name, kind = f["name"], f["type"]
        value = f"in.{member(name)}"
        if f.get("optional"):
            lines.append(f"{indent}if ({value}) {{")
            lines.append(f'{indent}  out["{name}"] = {writer(kind, "*" + value)};')
            lines.append(f"{indent}}}")
        else:
            lines.append(f'{indent}out["{name}"] = {writer(kind, value)};')
    return lines


def build_cpp(data):
    limits = data["limits"]
    frame_bound = limits["frame_to_page"]
    enums, types = data["enums"], data["types"]
    messages, commands = data["messages"], data["commands"]
    proto = data["protocol"]
    h = ["// SPDX-License-Identifier: MIT",
         "// Generated by tools/protocol/generate.py from src/grace-protocol/protocol.toml: do not edit (GraceUI D108,",
         "// D159). The bridge protocol's types (§6), each with a decoder that checks every field and an encoder that writes",
         "// the schema's order; the web app's web/src/protocol.ts holds the same types.",
         "#pragma once", "", "#include <cstddef>", "#include <cstdint>", "#include <optional>", "#include <string>",
         "#include <string_view>", "#include <variant>", "#include <vector>", "", '#include "grace/protocol/codec.h"', "",
         "namespace grace::protocol {", "",
         f"inline constexpr std::uint32_t kProtocolMajor = {proto['major']};",
         f"inline constexpr std::uint32_t kProtocolMinor = {proto['minor']};",
         f'inline constexpr std::string_view kProtocolVersion = "{proto["major"]}.{proto["minor"]}";', ""]
    for key, value in limits.items():
        h.append(f"inline constexpr std::size_t k{camel(key)} = {value};")
    h.append("")
    for name, enum in enums.items():
        h.append(f"enum class {camel(name)} : std::uint8_t {{")
        h += [f"  {member(v)}," for v in enum["values"]]
        h += ["};", f"std::string_view to_string({camel(name)} value) noexcept;",
              f"bool from_string(std::string_view text, {camel(name)}& out) noexcept;", ""]
    h += ["// Reads an enum's name: false, with the error set, for a value that is not one of its names.",
          "template <class E>", "bool read_enum(const Json& value, E& out, const std::string& path, DecodeError& error) {",
          "  if (!value.is_string() || !from_string(value.get_ref<const std::string&>(), out)) {",
          '    return fail(error, path, "not one of its names");', "  }", "  return true;", "}", ""]
    for name in data["_type_order"]:
        t = types[name]
        h.append(f"// {t.get('doc', name)}.")
        h.append(f"struct {camel(name)} {{")
        h += [f"  {cpp_field(f)} {member(f['name'])}{{}};" for f in t["fields"]]
        h.append(f"  bool operator==(const {camel(name)}&) const = default;")
        h += ["};", f"bool decode(const Json& in, {camel(name)}& out, const std::string& path, DecodeError& e);",
              f"Json encode(const {camel(name)}& in);", ""]
    h += ["// ---- The message kinds (§6.3).", "", "enum class Kind : std::uint8_t {"]
    h += [f"  {member(n)}," for n in messages]
    h += ["};", "std::string_view to_string(Kind kind) noexcept;", "bool from_string(std::string_view text, Kind& out) noexcept;",
          "", "// Who sends a kind: a page (a view), GraceUI, a space, or either side.",
          "enum class From : std::uint8_t { page, grace, space, both };", "From from_of(Kind kind) noexcept;",
          "// Whether a kind's frames carry their space: 0 never, 1 always, 2 when they are a space's.",
          "int space_of(Kind kind) noexcept;", ""]
    for name, m in messages.items():
        h.append(f"// {m.get('doc', name)}.")
        h.append(f"struct {camel(name)} {{")
        if m["space"] is True:
            h.append("  std::string space{};")
        elif m["space"] == "optional":
            h.append("  std::optional<std::string> space{};")
        h += [f"  {cpp_field(f)} {member(f['name'])}{{}};" for f in m["fields"]]
        h.append(f"  bool operator==(const {camel(name)}&) const = default;")
        h.append("};")
    h += ["", "using Message = std::variant<" + ", ".join(camel(n) for n in messages) + ">;", "",
          "// A message's fields from a frame's object (its v and t already read): false, with the error set, for a field",
          "// that is missing, of the wrong type or past its bound. Fields the schema does not name are ignored (§6.4).",
          "// Its fields bounded only by the frame (a state's body, a command's args) are moved out of `in`, never copied.",
          "bool decode_message(Kind kind, Json& in, Message& out, DecodeError& e);",
          "// A message as a frame's object: v, t and space first, then its fields in the schema's order.",
          "Json encode_message(const Message& message);",
          "// The same, written straight to text: no field is copied into a JSON value first.",
          "void encode_text(const Message& message, std::string& out);",
          "Kind kind_of(const Message& message) noexcept;", "",
          "// ---- The framework's commands (§6.6) and their arguments.", "", "enum class Command : std::uint8_t {"]
    h += [f"  {member(n.replace('.', '_'))}," for n in commands]
    h += ["};", "std::string_view to_string(Command command) noexcept;",
          "bool from_string(std::string_view text, Command& out) noexcept;", ""]
    for name, c in commands.items():
        h.append(f"// {name}: {c.get('doc', '')}.")
        h.append(f"struct {camel(name)}Args {{")
        h += [f"  {cpp_field(f)} {member(f['name'])}{{}};" for f in c["fields"]]
        h.append(f"  bool operator==(const {camel(name)}Args&) const = default;")
        h.append("};")
    h += ["", "using CommandArgs = std::variant<" + ", ".join(f"{camel(n)}Args" for n in commands) + ">;", "",
          "bool decode_args(Command command, const Json& in, CommandArgs& out, DecodeError& e);",
          "Json encode_args(const CommandArgs& args);", "", "}  // namespace grace::protocol", ""]

    c = ["// SPDX-License-Identifier: MIT",
         "// Generated by tools/protocol/generate.py from src/grace-protocol/protocol.toml: do not edit (GraceUI D108, D159).",
         '#include "grace/protocol/messages.h"', "", "#include <array>", "", "namespace grace::protocol {", ""]

    def enum_functions(type_name, values, names=None):
        names = names or values
        out = [f"std::string_view to_string({type_name} value) noexcept {{",
               f"  static constexpr std::array<std::string_view, {len(values)}> kNames = {{"
               + ", ".join(f'"{n}"' for n in names) + "};",
               "  const auto index = static_cast<std::size_t>(value);",
               '  return index < kNames.size() ? kNames[index] : std::string_view("");', "}", "",
               f"bool from_string(std::string_view text, {type_name}& out) noexcept {{",
               f"  static constexpr std::array<std::string_view, {len(values)}> kNames = {{"
               + ", ".join(f'"{n}"' for n in names) + "};",
               "  for (std::size_t i = 0; i < kNames.size(); ++i) {", "    if (kNames[i] == text) {",
               f"      out = static_cast<{type_name}>(i);", "      return true;", "    }", "  }", "  return false;", "}", ""]
        return out

    for name, enum in enums.items():
        c += enum_functions(camel(name), enum["values"])
    for name in data["_type_order"]:
        t = types[name]
        c.append(f"bool decode(const Json& in, {camel(name)}& out, const std::string& path, DecodeError& e) {{")
        c.append('  if (!in.is_object()) {')
        c.append('    return fail(e, path, "not an object");')
        c.append("  }")
        c += decode_body(t["fields"], frame_bound)
        c += ["  return true;", "}", "", f"Json encode(const {camel(name)}& in) {{", "  Json out = Json::object();"]
        c += encode_body(t["fields"])
        c += ["  return out;", "}", ""]
    c += enum_functions("Kind", list(messages))
    c += ["From from_of(Kind kind) noexcept {", "  switch (kind) {"]
    for name, m in messages.items():
        c.append(f"    case Kind::{member(name)}: return From::{m['from']};")
    c += ["  }", "  return From::both;", "}", "", "int space_of(Kind kind) noexcept {", "  switch (kind) {"]
    for name, m in messages.items():
        c.append(f"    case Kind::{member(name)}: return {1 if m['space'] is True else 2 if m['space'] == 'optional' else 0};")
    c += ["  }", "  return 0;", "}", ""]
    for name, m in messages.items():
        c.append(f"static bool decode_{name}(Json& in, {camel(name)}& out, DecodeError& e) {{")
        c.append("  const std::string path;")
        if m["space"]:
            required = "true" if m["space"] is True else "false"
            c.append(f'  if (const Json* v = field(in, "space", {required}, path, e)) {{')
            if m["space"] is True:
                c.append('    if (!read_id(*v, out.space, "space", e)) {')
            else:
                c.append("    out.space.emplace();")
                c.append('    if (!read_id(*v, *out.space, "space", e)) {')
            c += ["      return false;", "    }", "  } else if (!e.field.empty()) {", "    return false;", "  }"]
        c += decode_body(m["fields"], frame_bound, movable=True)
        if not m["fields"] and not m["space"]:
            c += ["  (void)in;", "  (void)out;", "  (void)e;"]
        c += ["  return true;", "}", ""]
    c += ["bool decode_message(Kind kind, Json& in, Message& out, DecodeError& e) {", "  switch (kind) {"]
    for name in messages:
        c += [f"    case Kind::{member(name)}: {{", f"      {camel(name)} m;",
              f"      if (!decode_{name}(in, m, e)) {{", "        return false;", "      }",
              "      out = std::move(m);", "      return true;", "    }"]
    c += ["  }", '  return fail(e, "t", "not a kind");', "}", "", "Json encode_message(const Message& message) {",
          "  return std::visit([](const auto& in) -> Json {", "    using T = std::decay_t<decltype(in)>;",
          "    Json out = Json::object();", f"    out[\"v\"] = kProtocolMajor;"]
    first = True
    for name, m in messages.items():
        c.append(f"    {'if' if first else '} else if'} constexpr (std::is_same_v<T, {camel(name)}>) {{")
        first = False
        c.append(f'      out["t"] = "{name}";')
        if m["space"] is True:
            c.append('      out["space"] = in.space;')
        elif m["space"] == "optional":
            c += ["      if (in.space) {", '        out["space"] = *in.space;', "      }"]
        c += encode_body(m["fields"], indent="      ")
    c += ["    }", "    return out;", "  }, message);", "}", "",
          "Kind kind_of(const Message& message) noexcept { return static_cast<Kind>(message.index()); }", ""]
    c += ["void encode_text(const Message& message, std::string& out) {", "  out.clear();",
          "  std::visit([&](const auto& in) {", "    using T = std::decay_t<decltype(in)>;",
          '    out += "{\\"v\\":";', "    out += std::to_string(kProtocolMajor);"]
    first = True
    for name, m in messages.items():
        c.append(f"    {'if' if first else '} else if'} constexpr (std::is_same_v<T, {camel(name)}>) {{")
        first = False
        c.append(f'      out += ",\\"t\\":\\"{name}\\"";')
        if m["space"] is True:
            c.append('      append_field(out, "space", Json(in.space));')
        elif m["space"] == "optional":
            c += ["      if (in.space) {", '        append_field(out, "space", Json(*in.space));', "      }"]
        body = encode_text_body(m["fields"])
        c += body if body else ["      (void)in;"]
    c += ["    }", "    out += '}';", "  }, message);", "}", ""]
    c += enum_functions("Command", list(commands))
    for name, cmd in commands.items():
        fn = name.replace(".", "_")
        c.append(f"static bool decode_{fn}(const Json& in, {camel(name)}Args& out, DecodeError& e) {{")
        c += ['  const std::string path("args");', '  if (!in.is_object()) {', '    return fail(e, path, "not an object");', "  }"]
        body = decode_body(cmd["fields"], frame_bound)
        body = [line.replace('path.empty() ? std::string("', 'path.empty() ? std::string("') for line in body]
        c += body
        if not cmd["fields"]:
            c += ["  (void)out;", "  (void)e;"]
        c += ["  return true;", "}", ""]
    c += ["bool decode_args(Command command, const Json& in, CommandArgs& out, DecodeError& e) {", "  switch (command) {"]
    for name in commands:
        fn = name.replace(".", "_")
        c += [f"    case Command::{member(fn)}: {{", f"      {camel(name)}Args a;", f"      if (!decode_{fn}(in, a, e)) {{",
              "        return false;", "      }", "      out = std::move(a);", "      return true;", "    }"]
    c += ["  }", '  return fail(e, "name", "not a command");', "}", "", "Json encode_args(const CommandArgs& args) {",
          "  return std::visit([](const auto& in) -> Json {", "    using T = std::decay_t<decltype(in)>;",
          "    Json out = Json::object();"]
    first = True
    for name, cmd in commands.items():
        c.append(f"    {'if' if first else '} else if'} constexpr (std::is_same_v<T, {camel(name)}Args>) {{")
        first = False
        c += encode_body(cmd["fields"], indent="      ") or ["      (void)in;"]
    c += ["    }", "    return out;", "  }, args);", "}", "", "}  // namespace grace::protocol", ""]
    return "\n".join(h), "\n".join(c)


# ---- TypeScript.

def ts_type(kind):
    if kind.startswith("list."):
        inner = ts_type(kind[5:])
        return f"{inner}[]" if re.fullmatch(r"\w+", inner) else f"({inner})[]"
    if kind.startswith("enum.") or kind.startswith("type."):
        return camel(kind[5:])
    return {"string": "string", "id": "string", "u32": "number", "u64": "number", "f64": "number", "bool": "boolean",
            "json": "Json", "object": "JsonObject"}[kind]


def ts_fields(fields, indent="  "):
    return [f'{indent}{f["name"]}{"?" if f.get("optional") else ""}: {ts_type(f["type"])};' for f in fields]


def build_ts(data):
    proto, limits = data["protocol"], data["limits"]
    out = ["// SPDX-License-Identifier: MIT",
           "// Generated by tools/protocol/generate.py from src/grace-protocol/protocol.toml: do not edit (GraceUI D108,",
           "// D159). The bridge protocol's types for the web app, the same as grace-protocol's C++ types.", "",
           f"export const PROTOCOL = {{ major: {proto['major']}, minor: {proto['minor']} }} as const;", "",
           "export const LIMITS = {"]
    out += [f"  {lower_camel(k)}: {v}," for k, v in limits.items()]
    out += ["} as const;", "", "export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };",
            "export type JsonObject = { [key: string]: Json };", ""]
    for name, enum in data["enums"].items():
        out.append(f"export type {camel(name)} = " + " | ".join(f'"{v}"' for v in enum["values"]) + ";")
        out.append(f"export const {name.upper()}S: readonly {camel(name)}[] = [" +
                   ", ".join(f'"{v}"' for v in enum["values"]) + "];")
        out.append("")
    for name in data["_type_order"]:
        t = data["types"][name]
        out.append(f"/** {t.get('doc', name)}. */")
        out.append(f"export interface {camel(name)} {{")
        out += ts_fields(t["fields"])
        out += ["}", ""]
    for name, m in data["messages"].items():
        out.append(f"/** {m.get('doc', name)}. */")
        out.append(f"export interface {camel(name)} {{")
        out += [f"  v: {proto['major']};", f'  t: "{name}";']
        if m["space"] is True:
            out.append("  space: string;")
        elif m["space"] == "optional":
            out.append("  space?: string;")
        out += ts_fields(m["fields"])
        out += ["}", ""]
    names = list(data["messages"])
    out.append("export type Frame = " + " | ".join(camel(n) for n in names) + ";")
    out.append("/** What a page sends. */")
    out.append("export type FrameFromPage = " + " | ".join(
        camel(n) for n, m in data["messages"].items() if m["from"] in ("page", "both")) + ";")
    out.append("/** What a page receives, from GraceUI or a space. */")
    out.append("export type FrameToPage = " + " | ".join(
        camel(n) for n, m in data["messages"].items() if m["from"] != "page") + ";")
    out.append("export type Kind = Frame[\"t\"];")
    out.append("")
    for name, cmd in data["commands"].items():
        out.append(f"/** {name}: {cmd.get('doc', '')}. */")
        out.append(f"export interface {camel(name)}Args {{")
        out += ts_fields(cmd["fields"])
        out += ["}", ""]
    out.append("/** The framework's commands, by name, and the args each takes. */")
    out.append("export interface CommandArgs {")
    out += [f'  "{name}": {camel(name)}Args;' for name in data["commands"]]
    out += ["}", "export type CommandName = keyof CommandArgs;", ""]
    return "\n".join(out)


def outputs(data):
    header, source = build_cpp(data)
    return {HEADER: header, SOURCE: source, TYPESCRIPT: build_ts(data)}


def run(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if an output is not what the schema generates")
    args = parser.parse_args(argv)
    try:
        data = load()
    except (SchemaError, KeyError, tomllib.TOMLDecodeError) as error:
        print(f"protocol: {error}", file=sys.stderr)
        return 1
    built = outputs(data)
    if args.check:
        stale = [p for p, text in built.items() if not p.is_file() or p.read_text(encoding="utf-8") != text]
        for path in stale:
            print(f"protocol: {path.relative_to(ROOT).as_posix()} is not what protocol.toml generates: run "
                  "python3 -I tools/protocol/generate.py", file=sys.stderr)
        if not stale:
            print("protocol: ok")
        return 1 if stale else 0
    for path, text in built.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print("protocol: wrote " + ", ".join(p.relative_to(ROOT).as_posix() for p in built))
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
