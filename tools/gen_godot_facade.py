#!/usr/bin/env python3
"""
G3.c -- generate a Turmeric facade from godot-cpp's extension_api.json.

Reads:   godot-cpp/gdextension/extension_api.json
Writes:  src/bridge/generated_facade.cpp
         (a C++ source defining TG_GENERATED_FACADE_SOURCE as a string literal)

For each method on each allow-listed class, emits a defn of the form:

    (defn classname/method-name [self : int arg1 : type1 ...] : int
      (godot-call self "method_name" arg1 ...))

Constraints:

* Return type is always `: int` (or omitted for void). The elaborator
  currently types every interpreter-mode native as :int-returning -- see
  the turmeric repo's docs/reported/untyped-native-registration-blocks-
  curated-facades.md. Generated wrappers respect that until the typed
  native registration API lands.

* Argument types follow JSON when scalar (int / float / bool / String).
  Aggregate / class / Variant args become :int (arena handles or Object
  handles, depending on the runtime value).

* Skipped: virtual, static, vararg, and underscore-prefixed methods.
  Virtuals are meant for override, not call; statics need a singleton,
  not an instance handle; varargs aren't expressible cleanly until godot-
  call's variadic surface gets first-class arg packing; underscore names
  are private.

* Method-name collisions with the prelude's `node/...` facade are skipped
  on Node (the prelude wins), so users get the curated form first.

To grow coverage: add a class to ALLOWLIST. The codegen handles the rest.
"""

import json
import os
import sys
import textwrap

# Classes whose methods we generate wrappers for. Chosen to cover the
# common 2D gameplay surface so the demo's "(node2d/set-position ...)"
# style works without hand-writing each wrapper. Growing this list is
# expected; keep it sorted.
ALLOWLIST = sorted([
    "AnimationPlayer",
    "Area2D",
    "CanvasItem",
    "CanvasLayer",
    "CollisionShape2D",
    "Control",
    "Input",
    "Label",
    "Node",
    "Node2D",
    "Object",
    "PhysicsBody2D",
    "RichTextLabel",
    "RigidBody2D",
    "SceneTree",
    "Sprite2D",
    "StaticBody2D",
    "Timer",
    "Viewport",
])

# Method names provided by the curated prelude on the Node hierarchy.
# We skip these to avoid two competing definitions of the same name.
PRELUDE_NODE_METHODS = {
    "set_position", "get_position", "set_modulate", "get_modulate",
    "set_scale", "get_node", "queue_free",
    "get_name", "get_class",
}


def kebab(s: str) -> str:
    return s.replace("_", "-")


def class_prefix(name: str) -> str:
    # PascalCase -> lowercase; numbers stay glued. We do NOT insert
    # kebabs inside PascalCase chunks (Node2D stays "node2d"); a fuller
    # kebab transform is doable later if the tighter form proves hard
    # to read at the call site.
    return name.lower()


# Map a JSON type string to a Turmeric param type. Aggregates / classes /
# Variant fall back to :int (arena or Object handles depending on shape).
SCALAR_TYPES = {
    "int":        ":int",
    "float":      ":float",
    "bool":       ":bool",
    "String":     ":cstr",
    "StringName": ":cstr",
    "NodePath":   ":cstr",
}


def param_type(json_type: str) -> str:
    if json_type in SCALAR_TYPES:
        return SCALAR_TYPES[json_type]
    # Everything else (Vector2/3, Color, Rect2, Transform2D/3D, Array,
    # Dictionary, Object, class names, enums, bitfields, typedarrays,
    # PackedXArray, Variant) flows as an :int handle. The runtime
    # marshaller is what actually distinguishes them.
    return ":int"


# Turmeric identifier sanity: argument names from JSON occasionally
# collide with reserved words. Map the few we've seen.
RESERVED_REWRITES = {
    "do":   "do_",
    "if":   "if_",
    "let":  "let_",
    "fn":   "fn_",
    "def":  "def_",
    "defn": "defn_",
    "true": "true_",
    "false":"false_",
    "self": "self_",  # self is the implicit OBJ handle
}


def safe_arg_name(name: str) -> str:
    n = kebab(name)
    return RESERVED_REWRITES.get(n, n)


def gen_wrapper(class_name: str, method: dict) -> str | None:
    """Returns a Turmeric defn for `method`, or None if it's skipped."""
    mname = method["name"]
    if mname.startswith("_"):
        return None
    if method.get("is_virtual"):
        return None
    if method.get("is_static"):
        return None
    if method.get("is_vararg"):
        return None
    # Avoid prelude collisions on Node and its subclasses (they all
    # inherit Node's methods; we generate per-class wrappers but the
    # prelude already covers the curated names).
    if mname in PRELUDE_NODE_METHODS:
        return None

    cls = class_prefix(class_name)
    wrap = f"{cls}/{kebab(mname)}"
    args = method.get("arguments", [])

    param_list = ["self : int"]
    call_args = []
    for a in args:
        n = safe_arg_name(a["name"])
        t = param_type(a["type"])
        param_list.append(f"{n} : {t.lstrip(':')}")
        call_args.append(n)

    # Codegen v2: pick a typed godot-call variant per JSON return type so
    # the wrapper carries an honest signature the elaborator can check.
    #   void                              -> godot-call-v (no return annotation)
    #   float                             -> godot-call-f : float
    #   bool                              -> godot-call-b : bool
    #   String / StringName / NodePath    -> godot-call-c : cstr
    #   other                             -> godot-call  : int (handles,
    #                                                aggregates, Object,
    #                                                Variant -- arena
    #                                                handles or primitives
    #                                                recognized at the use
    #                                                site)
    if "return_value" not in method:
        call_native = "godot-call-v"
        ret_anno    = ""
    else:
        rt = method["return_value"].get("type", "")
        if rt == "float":
            call_native, ret_anno = "godot-call-f", " : float"
        elif rt == "bool":
            call_native, ret_anno = "godot-call-b", " : bool"
        elif rt in ("String", "StringName", "NodePath"):
            call_native, ret_anno = "godot-call-c", " : cstr"
        else:
            call_native, ret_anno = "godot-call", " : int"

    params_src    = " ".join(param_list)
    call_args_src = " ".join(call_args)
    body = (f'({call_native} self "{mname}" {call_args_src})'
            if call_args_src
            else f'({call_native} self "{mname}")')

    return f"(defn {wrap} [{params_src}]{ret_anno}\n  {body})"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    api_path = os.path.join(repo, "godot-cpp", "gdextension", "extension_api.json")
    out_path = os.path.join(repo, "src", "bridge", "generated_facade.cpp")

    with open(api_path, "r") as f:
        api = json.load(f)

    classes_by_name = {c["name"]: c for c in api["classes"]}

    lines = []
    lines.append(";; AUTO-GENERATED by tools/gen_godot_facade.py -- DO NOT EDIT.")
    lines.append(";; Source of truth: godot-cpp/gdextension/extension_api.json.")
    lines.append(f";; Allowlist: {', '.join(ALLOWLIST)}")
    lines.append("")

    method_count = 0
    for cname in ALLOWLIST:
        c = classes_by_name.get(cname)
        if not c:
            print(f"WARN: {cname} not found in extension_api.json", file=sys.stderr)
            continue
        wrappers = []
        for m in c.get("methods", []):
            w = gen_wrapper(cname, m)
            if w:
                wrappers.append(w)
        if not wrappers:
            continue
        lines.append(f";; ---- {cname} ({len(wrappers)} methods) ----")
        lines.extend(wrappers)
        lines.append("")
        method_count += len(wrappers)

    source = "\n".join(lines)

    # Bake into a C++ raw string literal. R"TG_GEN( ... )TG_GEN" gives us
    # a delimiter that won't collide with the Turmeric source.
    header = (
        "// AUTO-GENERATED by tools/gen_godot_facade.py -- DO NOT EDIT.\n"
        "// Regenerate with: python3 tools/gen_godot_facade.py\n"
        "//\n"
        f"// {len(ALLOWLIST)} classes, {method_count} wrappers.\n"
        "//\n"
        "// The string is evaluated in every TurmericScript's TuriEnv\n"
        "// after the hand-written prelude. See bridge/prelude.{h,cpp}.\n\n"
        '#include "generated_facade.h"\n\n'
        "namespace godot {\n\n"
        "const char *TG_GENERATED_FACADE_SOURCE = R\"TG_GEN(\n"
    )
    footer = '\n)TG_GEN\";\n\n} // namespace godot\n'

    with open(out_path, "w") as f:
        f.write(header)
        f.write(source)
        f.write(footer)

    print(f"wrote {out_path}: {len(ALLOWLIST)} classes, {method_count} wrappers")


if __name__ == "__main__":
    main()
