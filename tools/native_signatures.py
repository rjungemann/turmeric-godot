"""Signatures for the godot-* natives the GDExtension registers.

AUTO-GENERATED. Regenerate rather than editing by hand.

Why this file exists. The natives have no declarative signature anywhere.
The information is spread across three encodings and none is complete:

  - return types      TUR_NRT_* in the registrations (all 89)
  - parameter types   runtime tag checks inside each C++ body --
                      `if (n != 3)`, `args[0].tag != TURI_CSTR`
  - typed signatures  a third time, in the prelude's Turmeric wrappers

Nothing keeps the three in step. That is the shape of the defect fixed in
d715fb6, where 29 natives were registered untyped and were therefore
invisible to the elaborator: one surface, three encodings, no check.

The AOT path needs ONE source of truth. It stages a script into a transient
project compiled by standalone `tur`, which has never heard of any godot-*
name, so the staged build dies at the first one -- see
docs/reported/godot-aot-staged-build-lacks-godot-natives.md in the turmeric
repo. Generating the extern-c declarations, the C entry points and the
registrations from this table is what stops them drifting again.
"""

# name -> (return, [params], provenance)
#
# `prelude` / `facade` rows are derived by reading call ARGUMENTS, because the
# prelude coerces explicitly on the way in --
#   (defn node/vec2-x [v : Vec2Handle] : float (godot-vec2-x (:: v :int)))
# states the native's parameter type even where the wrapper's own parameter is
# a defopaque over it.
#
# `c++ body` rows have no Turmeric caller at all and were read out of the
# implementation. Slots the code does not tag-check are still measured, not
# guessed: they pass through tg_handle_arg, which requires TURI_INT plus a live
# arena handle (classdb_proxy.cpp:422).
FIXED = {
    "godot-array-get":           (":int", [':int', ':int'],                "c++ body"),
    "godot-array-get-b":         (":bool", [':int', ':int'],                "prelude"),
    "godot-array-get-c":         (":cstr", [':int', ':int'],                "prelude"),
    "godot-array-get-f":         (":float", [':int', ':int'],                "prelude"),
    "godot-array-get-i":         (":int", [':int', ':int'],                "prelude"),
    "godot-array-len":           (":int", [':int'],                        "prelude"),
    "godot-array-new":           (":int", [],                              "prelude"),
    "godot-array-push":          (":void", [':int', ':int'],                "prelude"),
    "godot-color":               (":int", [':float', ':float', ':float', ':float'], "prelude"),
    "godot-color-a":             (":float", [':int'],                        "prelude"),
    "godot-color-b":             (":float", [':int'],                        "prelude"),
    "godot-color-g":             (":float", [':int'],                        "prelude"),
    "godot-color-r":             (":float", [':int'],                        "prelude"),
    "godot-connect":             (":void", [':int', ':cstr', ':cstr'],      "c++ body"),
    "godot-dict-get":            (":int", [':int', ':cstr'],               "c++ body"),
    "godot-dict-get-b":          (":bool", [':int', ':cstr'],               "prelude"),
    "godot-dict-get-c":          (":cstr", [':int', ':cstr'],               "prelude"),
    "godot-dict-get-f":          (":float", [':int', ':cstr'],               "prelude"),
    "godot-dict-get-i":          (":int", [':int', ':cstr'],               "prelude"),
    "godot-dict-has":            (":bool", [':int', ':cstr'],               "prelude"),
    "godot-dict-new":            (":int", [],                              "prelude"),
    "godot-dict-set":            (":void", [':int', ':cstr', ':int'],       "prelude"),
    "godot-packed-byte-get":     (":int", [':int', ':int'],                "prelude"),
    "godot-packed-byte-new":     (":int", [],                              "prelude"),
    "godot-packed-byte-push":    (":void", [':int', ':int'],                "prelude"),
    "godot-packed-color-get":    (":int", [':int', ':int'],                "prelude"),
    "godot-packed-color-new":    (":int", [],                              "prelude"),
    "godot-packed-color-push":   (":void", [':int', ':int'],                "prelude"),
    "godot-packed-float32-get":  (":float", [':int', ':int'],                "prelude"),
    "godot-packed-float32-new":  (":int", [],                              "prelude"),
    "godot-packed-float32-push": (":void", [':int', ':float'],              "prelude"),
    "godot-packed-float64-get":  (":float", [':int', ':int'],                "prelude"),
    "godot-packed-float64-new":  (":int", [],                              "prelude"),
    "godot-packed-float64-push": (":void", [':int', ':float'],              "prelude"),
    "godot-packed-int32-get":    (":int", [':int', ':int'],                "prelude"),
    "godot-packed-int32-new":    (":int", [],                              "prelude"),
    "godot-packed-int32-push":   (":void", [':int', ':int'],                "prelude"),
    "godot-packed-int64-get":    (":int", [':int', ':int'],                "prelude"),
    "godot-packed-int64-new":    (":int", [],                              "prelude"),
    "godot-packed-int64-push":   (":void", [':int', ':int'],                "prelude"),
    "godot-packed-size":         (":int", [':int'],                        "prelude"),
    "godot-packed-string-get":   (":cstr", [':int', ':int'],                "prelude"),
    "godot-packed-string-new":   (":int", [],                              "prelude"),
    "godot-packed-string-push":  (":void", [':int', ':cstr'],               "prelude"),
    "godot-packed-vec2-get":     (":int", [':int', ':int'],                "prelude"),
    "godot-packed-vec2-new":     (":int", [],                              "prelude"),
    "godot-packed-vec2-push":    (":void", [':int', ':int'],                "prelude"),
    "godot-packed-vec3-get":     (":int", [':int', ':int'],                "prelude"),
    "godot-packed-vec3-new":     (":int", [],                              "prelude"),
    "godot-packed-vec3-push":    (":void", [':int', ':int'],                "prelude"),
    "godot-preload":             (":int", [':cstr'],                       "prelude"),
    "godot-println":             (":void", [':cstr'],                       "prelude"),
    "godot-prop-get":            (":int", [':cstr'],                       "c++ body"),
    "godot-prop-get-b":          (":bool", [':cstr'],                       "c++ body"),
    "godot-prop-get-c":          (":cstr", [':cstr'],                       "c++ body"),
    "godot-prop-get-f":          (":float", [':cstr'],                       "c++ body"),
    "godot-prop-get-i":          (":int", [':cstr'],                       "c++ body"),
    "godot-rect2":               (":int", [':float', ':float', ':float', ':float'], "prelude"),
    "godot-rect2-h":             (":float", [':int'],                        "prelude"),
    "godot-rect2-w":             (":float", [':int'],                        "prelude"),
    "godot-rect2-x":             (":float", [':int'],                        "prelude"),
    "godot-rect2-y":             (":float", [':int'],                        "prelude"),
    "godot-rid-id":              (":int", [':int'],                        "prelude"),
    "godot-rid-valid?":          (":bool", [':int'],                        "prelude"),
    "godot-self":                (":int", [],                              "prelude"),
    "godot-singleton":           (":int", [':cstr'],                       "prelude"),
    "godot-vec2":                (":int", [':float', ':float'],            "prelude"),
    "godot-vec2-x":              (":float", [':int'],                        "prelude"),
    "godot-vec2-y":              (":float", [':int'],                        "prelude"),
    "godot-vec3":                (":int", [':float', ':float', ':float'],  "prelude"),
    "godot-vec3-x":              (":float", [':int'],                        "prelude"),
    "godot-vec3-y":              (":float", [':int'],                        "prelude"),
    "godot-vec3-z":              (":float", [':int'],                        "prelude"),
    "godot-xform2d-origin":      (":int", [':int'],                        "c++ body"),
    "godot-xform2d-rotation":    (":float", [':int'],                        "c++ body"),
    "godot-xform3d-origin":      (":int", [':int'],                        "c++ body"),
}

# Arity varies by call site, so ONE extern-c declaration cannot describe them.
# These are also the most-used names in the surface: godot-call-v has 1116 call
# sites across 13 distinct arities, godot-call 658 across 9. So the hard part of
# the AOT defect is not the 89 declarations -- it is that the busiest part of the
# API has no fixed shape.
#
# Two ways out, neither free:
#   1. one monomorphic entry point per (name, arity) actually used -- mechanical,
#      about 42 entry points across these nine;
#   2. marshal into a pack and call one fixed-arity entry point taking
#      (count, array), which is what godot-call-pack already does.
# (2) is the smaller surface and matches the existing design. Decide before
# generating anything for these.
VARIADIC = {
    "emit-signal":           (":void", "min 1, first :cstr"),
    "godot-call":            (":int", "[1, 2, 3, 4, 5, 6, 7, 8, 9]"),
    "godot-call-b":          (":bool", "[2, 3, 4, 5, 7]"),
    "godot-call-c":          (":cstr", "[2, 3, 4, 6]"),
    "godot-call-f":          (":float", "[2, 3, 4]"),
    "godot-call-pack":       (":int", "[3, 4, 5]"),
    "godot-call-pack-v":     (":void", "[5, 6]"),
    "godot-call-v":          (":void", "[2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15]"),
    "godot-signal":          (":void", "[1, 2, 4]"),
}

# A single slot accepts more than one tag, so these have no single extern-c
# signature either -- same problem as VARIADIC, different cause. Both are
# registered TUR_NRT_INT with a `// dynamic` comment at the call site.
POLYMORPHIC = {
    "godot-export":        (":void", [":cstr", ":cstr", "dynamic"], "arg 2's type is decided by the VALUE of arg 1"),
    "godot-num->str":      (":cstr", [":bool|:float|:int"],     "arg 0 accepts three tags"),
    "godot-prop-set":      (":void", [":cstr", "dynamic"],      "arg 1 is the property value"),
}

# Takes a Turmeric closure -- a code pointer plus a captured environment, so it
# cannot cross an extern-c boundary as a scalar the way an arena handle can.
# Wants a real function type on the Turmeric side rather than an :int stand-in
# (CLAUDE.md), and a thunk+env pair on the C side. Exactly one native does this.
CLOSURE = {
    "godot-connect-typed":     (":void", [":int", ":cstr", "closure"],      "arg 2 is a Turmeric closure"),
}

# Every native is accounted for in one of the three tables above.
UNRESOLVED = {}

ALL_NAMES = sorted(list(FIXED) + list(VARIADIC) + list(POLYMORPHIC) + list(CLOSURE) + list(UNRESOLVED))


def c_name(native_name):
    """The C symbol `extern-c` emits.

    Verified against the compiler rather than assumed, including the awkward
    cases, which are real names:
        godot-export     -> godot_export
        godot-num->str   -> godot_num__str
        godot-rid-valid? -> godot_rid_valid_
    """
    return native_name.replace("-", "_").replace(">", "_").replace("?", "_")
