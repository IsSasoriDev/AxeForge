#!/usr/bin/env python3
"""Check that the miner's JSON API still matches docs/openapi.yaml.

Other programs drive AxeForge miners through this API (AxeMobile, for one), so a
key renamed or dropped in the firmware breaks them without any build failing.
This script reads the handlers in main/ and compares the keys they send and read
with the spec.

  python3 scripts/api_contract.py              # source check, as CI runs it
  python3 scripts/api_contract.py --host IP    # also check a live miner (GET only)

Source mode follows how the handlers build their replies with cJSON: every
cJSON_Create*/Add*ToObject/AddItemTo* call on a variable, including helpers such
as price::json() in other files. It fails on
  - a key the firmware sends that the spec doesn't list,
  - a key the spec lists that the firmware no longer sends,
  - a type that differs (number, string, boolean, object, array),
  - an object the spec leaves untyped on a checked path,
  - a POST key the handler reads that the request schema doesn't list, or the reverse.
Exit status 1 on any failure.
"""
import argparse
import json
import os
import re
import sys
import urllib.request

try:
    import yaml
except ImportError:
    sys.exit("api_contract: needs PyYAML (pip install pyyaml, or apt install python3-yaml)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "docs", "openapi.yaml")

# What the check covers: (method, path, file, function). A GET entry compares the
# 200 reply; a POST entry compares the keys the handler reads with requestBody.
CHECKED = [
    ("get", "/api/status", "main/net/web.cpp", "status_get"),
    ("get", "/api/settings", "main/net/web.cpp", "settings_get"),
    ("post", "/api/settings", "main/net/web.cpp", "settings_post"),
    ("get", "/api/pools", "main/net/web.cpp", "pools_get"),
    ("get", "/api/swarm", "main/net/swarm.cpp", "to_json"),
    ("get", "/api/otp", "main/net/web.cpp", "otp_get"),
    ("post", "/api/otp", "main/net/web.cpp", "otp_post"),
]
# The endpoints live mode fetches.
LIVE = ["/api/status", "/api/settings", "/api/pools", "/api/swarm", "/api/otp"]


# ---------------------------------------------------------------- C++ source

def strip_comments(src):
    """Drop // and /* */ comments, leaving string and char literals alone."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


_files = {}


def source(rel):
    if rel not in _files:
        with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
            _files[rel] = strip_comments(f.read())
    return _files[rel]


def function_body(rel, name):
    src = source(rel)
    m = re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name), src)
    if not m:
        raise LookupError("%s: no function %s()" % (rel, name))
    depth, i = 0, m.end() - 1
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1:j]
    raise LookupError("%s: %s() never closes" % (rel, name))


def find_producer(call):
    """'price::json' -> (file, body) of `cJSON *json()` inside `namespace price`."""
    ns, _, fn = call.rpartition("::")
    for d in ("main/core", "main/net", "main/ui", "main/hw", "main"):
        full = os.path.join(ROOT, d)
        if not os.path.isdir(full):
            continue
        for f in sorted(os.listdir(full)):
            if not f.endswith(".cpp"):
                continue
            rel = d + "/" + f
            src = source(rel)
            if ns and not re.search(r"\bnamespace\s+%s\b" % re.escape(ns), src):
                continue
            if re.search(r"\b(cJSON|char)\s*\*\s*%s\s*\(\s*\)\s*\{" % re.escape(fn), src):
                return rel, function_body(rel, fn)
    return None


class Node:
    def __init__(self, kind, type_=None, where=""):
        self.kind = kind          # object, array, leaf, opaque
        self.type = type_         # leaf: number, string, boolean
        self.props = {}
        self.items = None
        self.where = where

    def merge(self, other):
        if other is None:
            return self
        if self.kind == "opaque":
            return other
        for k, v in other.props.items():
            self.props[k] = self.props[k].merge(v) if k in self.props else v
        if other.items is not None:
            self.items = other.items if self.items is None else self.items.merge(other.items)
        return self


LEAF = {"Number": "number", "String": "string", "Bool": "boolean", "True": "boolean",
        "False": "boolean", "Null": "null", "Raw": "raw"}

STMT = re.compile(
    r"(?:cJSON\s*\*\s*)?(?P<var>\w+)\s*=\s*cJSON_Create(?P<create>Object|Array)\s*\(\s*\)"
    r"|(?:cJSON\s*\*\s*)?(?P<cvar>\w+)\s*=\s*cJSON_Add(?P<ctype>Object|Array)ToObject\s*\(\s*(?P<cparent>\w+)\s*,\s*\"(?P<ckey>\w+)\"\s*\)"
    r"|cJSON_Add(?P<ltype>Number|String|Bool|True|False|Null|Raw)ToObject\s*\(\s*(?P<lparent>\w+)\s*,\s*\"(?P<lkey>\w+)\""
    r"|cJSON_AddItemToObject\s*\(\s*(?P<iparent>\w+)\s*,\s*\"(?P<ikey>\w+)\"\s*,\s*(?P<ival>[^;]+?)\)\s*;"
    r"|cJSON_AddItemToArray\s*\(\s*(?P<aparent>\w+)\s*,\s*(?P<aval>[^;]+?)\)\s*;"
    r"|(?:cJSON\s*\*\s*)?(?P<pvar>\w+)\s*=\s*(?P<pcall>\w+::\w+)\s*\(\s*\)"
    r"|(?:send_json\s*\(\s*\w+\s*,\s*|cJSON_Print(?:Unformatted)?\s*\(\s*|return\s+(?!send_json\b))(?P<root>\w+)\b"
    r"|\b(?P<hfn>\w+)\s*\(\s*(?P<harg>\w+)\s*[,)]"
)


def value_node(expr, env, where):
    expr = expr.strip()
    if re.fullmatch(r"\w+", expr):
        return env.get(expr)
    m = re.match(r"cJSON_Create(Number|String|Bool|True|False|Null)\s*\(", expr)
    if m:
        return Node("leaf", LEAF[m.group(1)], where)
    m = re.match(r"([\w:]+::\w+)\s*\(\s*\)$", expr)
    if m:
        return produce(m.group(1), where)
    m = re.match(r"json_parse\s*\(\s*(\w+)\s*\)", expr)
    if m and m.group(1) in env:
        return env[m.group(1)]
    return Node("opaque", where=where + ": " + expr)


def helpers(rel):
    """static void f(cJSON *param, ...) in this file: helpers that add to a node they are passed."""
    return {m.group(1): m.group(2) for m in
            re.finditer(r"\bstatic\s+void\s+(\w+)\s*\(\s*cJSON\s*\*\s*(\w+)\s*[,)]", source(rel))}


def build(body, where, env=None, rel=None, depth=0):
    """Walk a function body and return the tree of the cJSON it sends."""
    env = dict(env or {})
    root = None
    helper = helpers(rel) if rel and depth < 4 else {}
    # char *x = settings::pools_to_json(); ... json_parse(x)
    for m in re.finditer(r"char\s*\*\s*(\w+)\s*=\s*([\w:]+)\s*\(\s*\)", body):
        env[m.group(1)] = produce(m.group(2), where)
    for m in STMT.finditer(body):
        g = m.groupdict()
        if g["var"]:
            env[g["var"]] = Node("object" if g["create"] == "Object" else "array", where=where)
        elif g["cvar"]:
            parent = env.get(g["cparent"])
            child = Node("object" if g["ctype"] == "Object" else "array", where=where)
            env[g["cvar"]] = child
            if parent is not None and parent.kind == "object":
                parent.props[g["ckey"]] = parent.props[g["ckey"]].merge(child) if g["ckey"] in parent.props else child
        elif g["ltype"]:
            parent = env.get(g["lparent"])
            if parent is not None and parent.kind == "object":
                parent.props.setdefault(g["lkey"], Node("leaf", LEAF[g["ltype"]], where))
        elif g["iparent"]:
            parent = env.get(g["iparent"])
            if parent is not None and parent.kind == "object":
                child = value_node(g["ival"], env, where)
                if child is not None:
                    parent.props[g["ikey"]] = parent.props[g["ikey"]].merge(child) if g["ikey"] in parent.props else child
        elif g["aparent"]:
            parent = env.get(g["aparent"])
            if parent is not None and parent.kind == "array":
                child = value_node(g["aval"], env, where)
                if child is not None:
                    parent.items = child if parent.items is None else parent.items.merge(child)
        elif g["pvar"]:
            p = produce(g["pcall"], where)
            if p is not None:
                env[g["pvar"]] = p
        elif g["hfn"]:
            if g["hfn"] in helper and g["harg"] in env:
                build(function_body(rel, g["hfn"]), "%s %s()" % (rel, g["hfn"]),
                      {helper[g["hfn"]]: env[g["harg"]]}, rel, depth + 1)
        elif g["root"] and g["root"] in env and env[g["root"]].kind in ("object", "array"):
            root = env[g["root"]] if root is None else root.merge(env[g["root"]])
    return root


_produced = {}


def produce(call, where):
    if call in _produced:
        return _produced[call]
    found = find_producer(call)
    if not found:
        return Node("opaque", where=where + ": " + call + "()")
    rel, body = found
    _produced[call] = Node("opaque", where=rel)  # guards recursion
    node = build(body, "%s %s()" % (rel, call), rel=rel) or Node("opaque", where=rel)
    _produced[call] = node
    return node


def read_keys(body):
    """Keys a POST handler reads from its parsed body (any helper(j, "key", ...))."""
    keys = set(re.findall(r"\b\w+\s*\(\s*j\s*,\s*\"(\w+)\"", body))
    keys |= set(re.findall(r"cJSON_GetObjectItem(?:CaseSensitive)?\s*\(\s*j\s*,\s*\"(\w+)\"", body))
    return keys


# ---------------------------------------------------------------- the spec

def resolve(spec, s):
    seen = 0
    while isinstance(s, dict) and "$ref" in s and seen < 20:
        ref = s["$ref"].lstrip("#/").split("/")
        t = spec
        for part in ref:
            t = t[part]
        s, seen = t, seen + 1
    if isinstance(s, dict) and "allOf" in s:
        out = {"type": "object", "properties": {}}
        for part in s["allOf"]:
            p = resolve(spec, part)
            out["properties"].update(p.get("properties", {}))
            if p.get("additionalProperties") is not None:
                out["additionalProperties"] = p["additionalProperties"]
        return out
    return s or {}


def spec_kind(s):
    t = s.get("type")
    if t is None:
        if "properties" in s:
            return "object"
        if "items" in s:
            return "array"
    return t


def compatible(code_type, spec_type):
    if spec_type is None:
        return True
    if code_type == "number":
        return spec_type in ("number", "integer")
    return code_type == spec_type


def compare(spec, code, s, path, errors):
    s = resolve(spec, s)
    kind = spec_kind(s)
    if code.kind == "opaque":
        return
    if code.kind == "leaf":
        if kind in ("object", "array") or not compatible(code.type, kind):
            errors.append("%s: the firmware sends a %s, the spec says %s" % (path, code.type, kind))
        return
    if code.kind != kind:
        errors.append("%s: the firmware sends an %s, the spec says %s" % (path, code.kind, kind))
        return
    if code.kind == "array":
        if code.items is not None and code.items.kind != "opaque":
            if "items" not in s:
                errors.append("%s[]: the spec doesn't describe the items" % path)
            else:
                compare(spec, code.items, s["items"], path + "[]", errors)
        return
    props = s.get("properties")
    if props is None:
        if s.get("additionalProperties") is not None:
            return  # a map keyed by data (worker names and so on)
        errors.append("%s: the spec leaves this object untyped; the firmware sends %s"
                      % (path or "(reply)", ", ".join(sorted(code.props))))
        return
    for k in sorted(set(code.props) - set(props)):
        errors.append("%s.%s: sent by the firmware (%s), missing from the spec"
                      % (path, k, code.props[k].where) if path else
                      "%s: sent by the firmware (%s), missing from the spec" % (k, code.props[k].where))
    for k in sorted(set(props) - set(code.props)):
        if resolve(spec, props[k]).get("x-only"):
            continue  # documented as present only on some boards or builds
        errors.append("%s: in the spec, no longer sent by the firmware" % (path + "." + k if path else k))
    for k in sorted(set(props) & set(code.props)):
        compare(spec, code.props[k], props[k], path + "." + k if path else k, errors)


def reply_schema(spec, method, path):
    op = spec["paths"][path][method]
    r = op.get("responses", {}).get("200") or op.get("responses", {}).get(200)
    r = resolve(spec, r or {})
    return resolve(spec, r.get("content", {}).get("application/json", {}).get("schema", {}))


def request_schema(spec, path):
    op = spec["paths"][path]["post"]
    body = resolve(spec, op.get("requestBody", {}))
    return resolve(spec, body.get("content", {}).get("application/json", {}).get("schema", {}))



# ---------------------------------------------------------------- live mode

def live_node(v, where):
    if isinstance(v, dict):
        n = Node("object", where=where)
        for k, x in v.items():
            n.props[k] = live_node(x, where)
        return n
    if isinstance(v, list):
        n = Node("array", where=where)
        for x in v:
            c = live_node(x, where)
            n.items = c if n.items is None else n.items.merge(c)
        return n
    if isinstance(v, bool):
        return Node("leaf", "boolean", where)
    if isinstance(v, (int, float)):
        return Node("leaf", "number", where)
    if v is None:
        return Node("leaf", "null", where)
    return Node("leaf", "string", where)


def live(spec, host, errors):
    for path in LIVE:
        url = "http://%s%s" % (host, path)
        try:
            with urllib.request.urlopen(url, timeout=10) as r:
                data = json.load(r)
        except Exception as e:  # noqa: BLE001 - report and carry on
            errors.append("%s: %s" % (url, e))
            continue
        before = len(errors)
        compare_live(spec, live_node(data, host + path), reply_schema(spec, "get", path), host + path, errors)
        if len(errors) == before:
            print("live %s%s: ok" % (host, path))


def compare_live(spec, code, s, path, errors):
    # A live reply only holds what this board sends right now, so only flag keys
    # the spec lacks and types that differ, not keys the spec has that are absent.
    s = resolve(spec, s)
    kind = spec_kind(s)
    if code.kind == "leaf":
        if code.type != "null" and (kind in ("object", "array") or not compatible(code.type, kind)):
            errors.append("%s: the miner sent a %s, the spec says %s" % (path, code.type, kind))
        return
    if code.kind != kind:
        errors.append("%s: the miner sent an %s, the spec says %s" % (path, code.kind, kind))
        return
    if code.kind == "array":
        if code.items is not None and "items" in s:
            compare_live(spec, code.items, s["items"], path + "[]", errors)
        return
    props = s.get("properties")
    if props is None:
        return
    for k in sorted(code.props):
        if k not in props:
            errors.append("%s.%s: sent by the miner, missing from the spec" % (path, k))
        else:
            compare_live(spec, code.props[k], props[k], path + "." + k, errors)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", action="append", default=[], help="also check a live miner (GET only); repeatable")
    a = ap.parse_args()

    with open(SPEC, encoding="utf-8") as f:
        spec = yaml.safe_load(f)
    errors = []

    for method, path, rel, fn in CHECKED:
        before = len(errors)
        where = "%s %s" % (method.upper(), path)
        try:
            body = function_body(rel, fn)
        except LookupError as e:
            errors.append("%s: %s" % (where, e))
            continue
        if method == "get":
            code = build(body, "%s %s()" % (rel, fn), rel=rel)
            if code is None:
                errors.append("%s: found no reply built in %s()" % (where, fn))
                continue
            sub = []
            compare(spec, code, reply_schema(spec, method, path), "", sub)
            errors += ["%s: %s" % (where, e) for e in sub]
        else:
            req = request_schema(spec, path)
            props = set((req.get("properties") or {}).keys())
            keys = read_keys(body)
            for k in sorted(keys - props):
                errors.append("%s: the handler reads \"%s\", the request schema doesn't list it" % (where, k))
            for k in sorted(props - keys):
                errors.append("%s: the request schema lists \"%s\", the handler doesn't read it" % (where, k))
        if len(errors) == before:
            print("source %s: ok" % where)

    for h in a.host:
        live(spec, h, errors)

    for e in errors:
        print("FAIL " + e)
    if errors:
        print("%d problem(s). Update docs/openapi.yaml with the change, or the firmware if the change was a mistake." % len(errors))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
