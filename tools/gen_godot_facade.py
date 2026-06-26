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
    # G3 base (19) -- 2D gameplay surface paddle-pong-tur exercised.
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
    # T3.A additions (~34) -- what a 2D action game actually needs.
    "AudioStream",
    "AudioStreamPlayer",
    "AudioStreamPlayer2D",
    "Camera2D",
    "CharacterBody2D",
    "Engine",
    "Environment",
    "FileAccess",
    "FileSystemDock",
    "GPUParticles2D",
    "Image",
    "ImageTexture",
    "InputEvent",
    "InputEventKey",
    "InputEventMouseButton",
    "Light2D",
    "MeshInstance2D",
    "NavigationAgent2D",
    "NavigationRegion2D",
    "OS",
    "PackedScene",
    "PhysicsServer2D",
    "RandomNumberGenerator",
    "Resource",
    "ResourceLoader",
    "Shader",
    "ShaderMaterial",
    "Skeleton2D",
    "TileMap",
    "TileMapLayer",
    "Texture2D",
    "Tween",
    "Window",
    "WorldBoundaryShape2D",
])

# Method names provided by the curated prelude on the Node hierarchy.
# We skip these to avoid two competing definitions of the same name.
PRELUDE_NODE_METHODS = {
    "set_position", "get_position", "set_modulate", "get_modulate",
    "set_scale", "get_node", "queue_free",
    "get_name", "get_class",
}

# T3.B -- per-class method names already hand-wired in the curated
# prelude under the singleton namespace (e.g. (input/is-action-pressed)).
# Generator skips these names per class to avoid a duplicate (defn).
PRELUDE_SINGLETON_METHODS = {
    "Input":  {"is_action_pressed", "is_action_just_pressed"},
    "Engine": {"get_frames_drawn", "get_process_fps"},
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

# T2.A -- arena handle types declared in bridge/prelude.cpp. JSON method
# args / returns that name one of these get the matching defopaque
# instead of falling through to bare :int. The call body still demotes
# back to :int at the godot-call boundary (arg-type-checking of the
# typed-native registry is deferred); the user-facing signature is the
# honest one.
ARENA_TYPES = {
    "Vector2":     "Vec2Handle",
    "Vector3":     "Vec3Handle",
    "Color":       "ColorHandle",
    "Rect2":       "Rect2Handle",
    "Transform2D": "Transform2DHandle",
    "Transform3D": "Transform3DHandle",
    "Array":       "ArrayHandle",
    "Dictionary":  "DictHandle",
    # T3.D -- PackedXxxArray + RID land in the same arena. Each gets a
    # distinct prelude defopaque so a (defn ... [a : PackedByteHandle b :
    # PackedInt32Handle] ...) wrapper rejects mixing the two by type.
    "PackedByteArray":    "PackedByteHandle",
    "PackedInt32Array":   "PackedInt32Handle",
    "PackedInt64Array":   "PackedInt64Handle",
    "PackedFloat32Array": "PackedFloat32Handle",
    "PackedFloat64Array": "PackedFloat64Handle",
    "PackedStringArray":  "PackedStringHandle",
    "PackedVector2Array": "PackedVec2Handle",
    "PackedVector3Array": "PackedVec3Handle",
    "PackedColorArray":   "PackedColorHandle",
    "RID":                "RidHandle",
}


def param_type(json_type: str) -> str:
    if json_type in SCALAR_TYPES:
        return SCALAR_TYPES[json_type]
    # G6.2 follow-up -- allow-listed class type names get the matching
    # <Class>Handle defopaque. JSON sometimes prefixes class types with
    # "typedarray::" / "enum::" / "bitfield::"; those don't denote a
    # specific Object handle and stay :int.
    if json_type in ALLOWLIST:
        return f":{handle_name(json_type)}"
    # T2.A -- arena Variant types get their Handle defopaque too.
    if json_type in ARENA_TYPES:
        return f":{ARENA_TYPES[json_type]}"
    # Everything else (PackedXxxArray, RID, NodePath-as-resource path,
    # enum-prefixed, typedarray::, etc.) stays :int for now -- that's
    # Tier 3 territory.
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


def gen_wrapper(class_name: str, method: dict,
                is_singleton_class: bool = False) -> str | None:
    """Returns a Turmeric defn for `method`, or None if it's skipped.

    T3.B -- when `method.is_static` is true, OR the owning class is a
    Godot singleton (Engine, Input, OS, ...), the wrapper is emitted in
    "singleton form": no `self` parameter, body uses
    `(godot-singleton "ClassName")` as the receiver. Same call shape,
    same return-type marshalling -- only the receiver changes.
    """
    mname = method["name"]
    if mname.startswith("_"):
        return None
    if method.get("is_virtual"):
        return None
    if method.get("is_vararg"):
        return None
    is_static_call = bool(method.get("is_static")) or is_singleton_class
    # Avoid prelude collisions on Node and its subclasses (they all
    # inherit Node's methods; we generate per-class wrappers but the
    # prelude already covers the curated names).
    if mname in PRELUDE_NODE_METHODS:
        return None
    if mname in PRELUDE_SINGLETON_METHODS.get(class_name, ()):
        return None

    cls = class_prefix(class_name)
    wrap = f"{cls}/{kebab(mname)}"
    args = method.get("arguments", [])

    # G6.2 follow-up -- self carries the class's defopaque handle.
    # T3.B -- static/singleton calls drop self entirely and target the
    # singleton instance directly.
    if is_static_call:
        param_list = []
        self_expr = f'(godot-singleton "{class_name}")'
    else:
        self_type = f":{handle_name(class_name)}"
        param_list = [f"self : {self_type.lstrip(':')}"]
        self_expr = f"(:: self :int)"
    # The godot-call body still wants raw :int handles, so emit ascriptions
    # in the call args for any argument whose declared param type is a
    # handle defopaque (any non-:int type at this point).
    call_args = []
    for a in args:
        n = safe_arg_name(a["name"])
        t = param_type(a["type"])
        param_list.append(f"{n} : {t.lstrip(':')}")
        if t.lstrip(":") not in ("int", "float", "bool", "cstr"):
            call_args.append(f"(:: {n} :int)")
        else:
            call_args.append(n)

    # Codegen v2: pick a typed godot-call variant per JSON return type so
    # the wrapper carries an honest signature the elaborator can check.
    #   void                              -> godot-call-v (no return annotation)
    #   float                             -> godot-call-f : float
    #   bool                              -> godot-call-b : bool
    #   String / StringName / NodePath    -> godot-call-c : cstr
    #   class-typed Object return         -> godot-call wrapped in (:: ... :CHandle)
    #   other                             -> godot-call  : int
    return_wrap = None  # (prefix, suffix) when an ascription wraps the body
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
        elif rt in ALLOWLIST:
            hname = handle_name(rt)
            call_native, ret_anno = "godot-call", f" : {hname}"
            return_wrap = (f"(:: ", f" :{hname})")
        elif rt in ARENA_TYPES:
            hname = ARENA_TYPES[rt]
            call_native, ret_anno = "godot-call", f" : {hname}"
            return_wrap = (f"(:: ", f" :{hname})")
        else:
            call_native, ret_anno = "godot-call", " : int"

    params_src    = " ".join(param_list)
    call_args_src = " ".join(call_args)
    call_expr = (f'({call_native} {self_expr} "{mname}" {call_args_src})'
                 if call_args_src
                 else f'({call_native} {self_expr} "{mname}")')
    body = (f"{return_wrap[0]}{call_expr}{return_wrap[1]}"
            if return_wrap else call_expr)

    return f"(defn {wrap} [{params_src}]{ret_anno}\n  {body})"


# G6.2 -- handle types the prelude already owns. Codegen must NOT redeclare
# these (defopaque does not allow redefinition). Arena types live here too
# (Vec2/Vec3/Color/Rect2) so the codegen leaves them alone.
PRELUDE_HANDLE_NAMES = {
    "NodeHandle",
    "Vec2Handle", "Vec3Handle", "ColorHandle", "Rect2Handle",
    # T2.A -- arena handles G6 deferred; declared in prelude.cpp now so
    # the generator's per-class defopaque pass must not redeclare them.
    "Transform2DHandle", "Transform3DHandle", "ArrayHandle", "DictHandle",
    # T3.D -- PackedXxxArray + RID arena handles, declared in prelude.cpp.
    "PackedByteHandle", "PackedInt32Handle", "PackedInt64Handle",
    "PackedFloat32Handle", "PackedFloat64Handle", "PackedStringHandle",
    "PackedVec2Handle", "PackedVec3Handle", "PackedColorHandle",
    "RidHandle",
}


def handle_name(class_name: str) -> str:
    return f"{class_name}Handle"


def nearest_allowlisted_ancestor(class_name: str, classes_by_name: dict) -> str | None:
    """Walk `inherits` from class_name; return the first ancestor in ALLOWLIST,
    or None if the chain runs out without hitting one."""
    c = classes_by_name.get(class_name)
    if not c:
        return None
    parent = c.get("inherits")
    while parent:
        if parent in ALLOWLIST:
            return parent
        p = classes_by_name.get(parent)
        if not p:
            return None
        parent = p.get("inherits")
    return None


def gen_handle_section(classes_by_name: dict) -> list[str]:
    """G6.2 -- emit (defopaque <C>Handle :int) per allow-listed class plus
    one up-coercion helper to the nearest allow-listed ancestor handle.

    The prelude already declares NodeHandle and the arena handles
    (Vec2Handle, etc.); skip those to avoid a defopaque redefinition.
    Down-coercion stays explicit at the use site (`(:: h :Sprite2DHandle)`)
    -- a runtime-checked variant is out of scope for v1."""
    lines = [
        ";; ---- G6.2 class-hierarchy defopaque handles ----",
        ";; One (defopaque <Class>Handle :int) per allow-listed class.",
        ";; Up-coercion to the nearest allow-listed ancestor is provided as",
        ";; <class>-handle-><ancestor>-handle. NodeHandle and the arena",
        ";; handles (Vec2Handle, ...) come from the hand-written prelude.",
    ]
    for cname in ALLOWLIST:
        hname = handle_name(cname)
        if hname not in PRELUDE_HANDLE_NAMES:
            lines.append(f"(defopaque {hname} :int)")
    lines.append("")
    for cname in ALLOWLIST:
        anc = nearest_allowlisted_ancestor(cname, classes_by_name)
        if not anc:
            continue
        ch = handle_name(cname)
        ah = handle_name(anc)
        if ch == ah:
            continue
        fn = f"{class_prefix(cname)}-handle->{class_prefix(anc)}-handle"
        lines.append(
            f"(defn {fn} [h : {ch}] : {ah}\n"
            f"  (:: h :{ah}))"
        )
    lines.append("")
    # T2.C -- runtime-checked downcasts. One try-as-<class> per allow-listed
    # class; consults Object::is_class via the curated `is-class?` prelude.
    # On a successful check the input handle is ascribed to the target's
    # <Class>Handle; on failure the result is a wrapped-0 sentinel that
    # the caller compares with `(= (:: result :int) 0)`. Pairing this with
    # the existing up-coercion helpers gives a checked widening + checked
    # narrowing pair, with no Option<T> ergonomic burden on the user.
    lines.append(";; ---- T2.C runtime-checked downcasts ----")
    lines.append(";; (try-as-<class> h) :: int -> <Class>Handle")
    lines.append(";;   Returns the ascribed handle when h IS-A <Class>, or")
    lines.append(";;   (:: 0 :<Class>Handle) -- compare with (= (:: r :int) 0).")
    for cname in ALLOWLIST:
        hname = handle_name(cname)
        fn = f"try-as-{class_prefix(cname)}"
        lines.append(
            f"(defn {fn} [h : int] : {hname}\n"
            f"  (if (is-class? h \"{cname}\")\n"
            f"    (:: h :{hname})\n"
            f"    (:: 0 :{hname})))"
        )
    lines.append("")
    return lines


def gen_signal_wrapper(class_name: str, signal: dict) -> str:
    """G6.1 -- emit a typed (classname/on-SIGNAL) wrapper.

    The handler parameter is typed `(fn [argtypes...] void)`, so a
    wrong-arity or wrong-type closure at the call site is `TUR-E0001`
    at elaboration time instead of a silent runtime no-op.
    """
    sname = signal["name"]
    cls   = class_prefix(class_name)
    wrap  = f"{cls}/on-{kebab(sname)}"
    args  = signal.get("arguments", [])

    # Build the (fn [argT1 argT2 ...] void) type expression. Strip the leading
    # ':' that param_type returns -- inside `(fn [...] void)` arg types are
    # bare type names, not annotated parameter slots.
    arg_types = [param_type(a["type"]).lstrip(":") for a in args]
    fn_type   = f"(fn [{' '.join(arg_types)}] void)" if arg_types else "(fn [] void)"

    self_h = handle_name(class_name)
    return (f"(defn {wrap} [self : {self_h} handler : {fn_type}] : void\n"
            f'  (godot-connect-typed (:: self :int) "{sname}" handler))')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    api_path = os.path.join(repo, "godot-cpp", "gdextension", "extension_api.json")
    out_path = os.path.join(repo, "src", "bridge", "generated_facade.cpp")

    with open(api_path, "r") as f:
        api = json.load(f)

    classes_by_name = {c["name"]: c for c in api["classes"]}
    # T3.B -- name set of Godot singletons (Engine, Input, OS, ...). For
    # these, every instance method is emitted in singleton form (no self).
    singleton_class_names = {s.get("type") or s["name"]
                             for s in api.get("singletons", [])}

    lines = []
    lines.append(";; AUTO-GENERATED by tools/gen_godot_facade.py -- DO NOT EDIT.")
    lines.append(";; Source of truth: godot-cpp/gdextension/extension_api.json.")
    lines.append(f";; Allowlist: {', '.join(ALLOWLIST)}")
    lines.append("")

    # G6.2 -- defopaque handles + up-coercion chain. Emitted before the
    # per-class wrappers so the wrapper bodies (which still pass :int today)
    # can be opt-in upgraded by user scripts.
    lines.extend(gen_handle_section(classes_by_name))

    method_count = 0
    signal_count = 0
    for cname in ALLOWLIST:
        c = classes_by_name.get(cname)
        if not c:
            print(f"WARN: {cname} not found in extension_api.json", file=sys.stderr)
            continue
        is_singleton_class = cname in singleton_class_names
        wrappers = []
        for m in c.get("methods", []):
            w = gen_wrapper(cname, m, is_singleton_class=is_singleton_class)
            if w:
                wrappers.append(w)
        # G6.1 -- one (class/on-SIGNAL) wrapper per signal declared at this
        # class. JSON records signals at their declaring class only; subclass
        # consumers up-cast (see G6.2's coercion helpers).
        signal_wrappers = [gen_signal_wrapper(cname, s) for s in c.get("signals", [])]
        if not wrappers and not signal_wrappers:
            continue
        lines.append(f";; ---- {cname} "
                     f"({len(wrappers)} methods, "
                     f"{len(signal_wrappers)} signals) ----")
        lines.extend(wrappers)
        lines.extend(signal_wrappers)
        lines.append("")
        method_count += len(wrappers)
        signal_count += len(signal_wrappers)

    source = "\n".join(lines)

    # Bake into a C++ raw string literal. R"TG_GEN( ... )TG_GEN" gives us
    # a delimiter that won't collide with the Turmeric source.
    header = (
        "// AUTO-GENERATED by tools/gen_godot_facade.py -- DO NOT EDIT.\n"
        "// Regenerate with: python3 tools/gen_godot_facade.py\n"
        "//\n"
        f"// {len(ALLOWLIST)} classes, {method_count} method wrappers, "
        f"{signal_count} signal wrappers.\n"
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

    print(f"wrote {out_path}: {len(ALLOWLIST)} classes, "
          f"{method_count} methods, {signal_count} signals")


if __name__ == "__main__":
    main()
