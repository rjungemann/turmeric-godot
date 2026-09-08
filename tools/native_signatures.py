"""Signatures for the godot-* natives the GDExtension registers.

AUTO-GENERATED SKELETON -- the FIXED table below is derived; the TODO table
is not, and must be filled in by hand.

Why this file exists. The natives have no declarative signature anywhere:
return types live in the TUR_NRT_* registrations in turmeric_language.cpp,
parameter types exist only as runtime tag checks inside each C++ body
(recoverable for 10 of 89), and typed signatures exist a third time in the
prelude's Turmeric wrappers. Nothing keeps the three in step -- which is the
same shape as the defect fixed in d715fb6, where 29 natives were registered
untyped and were therefore invisible to the elaborator.

The AOT path needs ONE source of truth: it stages a script into a transient
project compiled by standalone `tur`, which has never heard of any godot-*
name, so the staged build dies at the first one. See
docs/reported/godot-aot-staged-build-lacks-godot-natives.md in the turmeric
repo. Generating the extern-c declarations, the C entry points and the
registrations from this table is what makes them incapable of drifting.
"""

# name -> (return, [param types], provenance)
#
# Derived by reading call ARGUMENTS in the prelude and the generated facade,
# because the prelude coerces explicitly on the way in --
#   (defn node/vec2-x [v : Vec2Handle] : float (godot-vec2-x (:: v :int)))
# states the native's parameter type even where the wrapper's own parameter
# is a defopaque over it.
FIXED = {
    "godot-array-get-b":         (":bool", [':int', ':int'],                  "prelude"),
    "godot-array-get-c":         (":cstr", [':int', ':int'],                  "prelude"),
    "godot-array-get-f":         (":float", [':int', ':int'],                  "prelude"),
    "godot-array-get-i":         (":int", [':int', ':int'],                  "prelude"),
    "godot-array-len":           (":int", [':int'],                          "prelude"),
    "godot-array-new":           (":int", [],                                "prelude"),
    "godot-array-push":          (":void", [':int', ':int'],                  "prelude"),
    "godot-color":               (":int", [':float', ':float', ':float', ':float'], "prelude"),
    "godot-color-a":             (":float", [':int'],                          "prelude"),
    "godot-color-b":             (":float", [':int'],                          "prelude"),
    "godot-color-g":             (":float", [':int'],                          "prelude"),
    "godot-color-r":             (":float", [':int'],                          "prelude"),
    "godot-connect-typed":       (":void", None,                              "None"),
    "godot-dict-get-b":          (":bool", [':int', ':cstr'],                 "prelude"),
    "godot-dict-get-c":          (":cstr", [':int', ':cstr'],                 "prelude"),
    "godot-dict-get-f":          (":float", [':int', ':cstr'],                 "prelude"),
    "godot-dict-get-i":          (":int", [':int', ':cstr'],                 "prelude"),
    "godot-dict-has":            (":bool", [':int', ':cstr'],                 "prelude"),
    "godot-dict-new":            (":int", [],                                "prelude"),
    "godot-dict-set":            (":void", [':int', ':cstr', ':int'],         "prelude"),
    "godot-export":              (":void", None,                              "None"),
    "godot-packed-byte-get":     (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-byte-new":     (":int", [],                                "prelude"),
    "godot-packed-byte-push":    (":void", [':int', ':int'],                  "prelude"),
    "godot-packed-color-get":    (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-color-new":    (":int", [],                                "prelude"),
    "godot-packed-color-push":   (":void", [':int', ':int'],                  "prelude"),
    "godot-packed-float32-get":  (":float", [':int', ':int'],                  "prelude"),
    "godot-packed-float32-new":  (":int", [],                                "prelude"),
    "godot-packed-float32-push": (":void", [':int', ':float'],                "prelude"),
    "godot-packed-float64-get":  (":float", [':int', ':int'],                  "prelude"),
    "godot-packed-float64-new":  (":int", [],                                "prelude"),
    "godot-packed-float64-push": (":void", [':int', ':float'],                "prelude"),
    "godot-packed-int32-get":    (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-int32-new":    (":int", [],                                "prelude"),
    "godot-packed-int32-push":   (":void", [':int', ':int'],                  "prelude"),
    "godot-packed-int64-get":    (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-int64-new":    (":int", [],                                "prelude"),
    "godot-packed-int64-push":   (":void", [':int', ':int'],                  "prelude"),
    "godot-packed-size":         (":int", [':int'],                          "prelude"),
    "godot-packed-string-get":   (":cstr", [':int', ':int'],                  "prelude"),
    "godot-packed-string-new":   (":int", [],                                "prelude"),
    "godot-packed-string-push":  (":void", [':int', ':cstr'],                 "prelude"),
    "godot-packed-vec2-get":     (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-vec2-new":     (":int", [],                                "prelude"),
    "godot-packed-vec2-push":    (":void", [':int', ':int'],                  "prelude"),
    "godot-packed-vec3-get":     (":int", [':int', ':int'],                  "prelude"),
    "godot-packed-vec3-new":     (":int", [],                                "prelude"),
    "godot-packed-vec3-push":    (":void", [':int', ':int'],                  "prelude"),
    "godot-preload":             (":int", [':cstr'],                         "prelude"),
    "godot-println":             (":void", [':cstr'],                         "prelude"),
    "godot-rect2":               (":int", [':float', ':float', ':float', ':float'], "prelude"),
    "godot-rect2-h":             (":float", [':int'],                          "prelude"),
    "godot-rect2-w":             (":float", [':int'],                          "prelude"),
    "godot-rect2-x":             (":float", [':int'],                          "prelude"),
    "godot-rect2-y":             (":float", [':int'],                          "prelude"),
    "godot-rid-id":              (":int", [':int'],                          "prelude"),
    "godot-rid-valid?":          (":bool", [':int'],                          "prelude"),
    "godot-self":                (":int", [],                                "prelude"),
    "godot-singleton":           (":int", [':cstr'],                         "prelude"),
    "godot-vec2":                (":int", [':float', ':float'],              "prelude"),
    "godot-vec2-x":              (":float", [':int'],                          "prelude"),
    "godot-vec2-y":              (":float", [':int'],                          "prelude"),
    "godot-vec3":                (":int", [':float', ':float', ':float'],    "prelude"),
    "godot-vec3-x":              (":float", [':int'],                          "prelude"),
    "godot-vec3-y":              (":float", [':int'],                          "prelude"),
    "godot-vec3-z":              (":float", [':int'],                          "prelude"),
}

# VARIADIC -- arity varies by call site, so a single extern-c declaration
# cannot describe them, and these are the MOST used names in the whole
# surface (godot-call-v alone has over a thousand call sites).
#
# Two ways out, neither free:
#   1. emit one monomorphic entry point per (name, arity) actually used --
#      mechanical, and about 42 entry points across these eight;
#   2. marshal into a pack and call one fixed-arity entry point taking
#      (count, array), which is what godot-call-pack already does.
# (2) is the smaller surface and matches the existing design. Decide before
# generating anything for these.
VARIADIC = {
    "godot-call":            (":int", [1, 2, 3, 4, 5, 6, 7, 8, 9]),
    "godot-call-b":          (":bool", [2, 3, 4, 5, 7]),
    "godot-call-c":          (":cstr", [2, 3, 4, 6]),
    "godot-call-f":          (":float", [2, 3, 4]),
    "godot-call-pack":       (":int", [3, 4, 5]),
    "godot-call-pack-v":     (":void", [5, 6]),
    "godot-call-v":          (":void", [2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15]),
    "godot-signal":          (":void", [1, 2, 4]),
}

# TODO -- no Turmeric call site anywhere, so the signature exists ONLY in the
# C++ body's runtime checks. Fill these in by reading turmeric_language.cpp;
# the impl function is named so the body is one grep away. Left empty rather
# than guessed: a wrong type here is a silent ABI mismatch in generated
# extern-c, which the compiler cannot catch and the linker will not either.
TODO = {
    "emit-signal":             (":void", None, "tg_native_emit_signal"),
    "godot-array-get":         (":int", None, "tg_native_godot_array_get"),
    "godot-connect":           (":void", None, "tg_native_godot_connect"),
    "godot-dict-get":          (":int", None, "tg_native_godot_dict_get"),
    "godot-num->str":          (":cstr", None, "tg_native_godot_num_to_str"),
    "godot-prop-get":          (":int", None, "tg_native_prop_get"),
    "godot-prop-get-b":        (":bool", None, "tg_native_prop_get"),
    "godot-prop-get-c":        (":cstr", None, "tg_native_prop_get_c"),
    "godot-prop-get-f":        (":float", None, "tg_native_prop_get"),
    "godot-prop-get-i":        (":int", None, "tg_native_prop_get"),
    "godot-prop-set":          (":void", None, "tg_native_prop_set"),
    "godot-xform2d-origin":    (":int", None, "tg_native_godot_xform2d_origin"),
    "godot-xform2d-rotation":  (":float", None, "tg_native_godot_xform2d_rotation"),
    "godot-xform3d-origin":    (":int", None, "tg_native_godot_xform3d_origin"),
}

ALL_NAMES = sorted(list(FIXED) + list(VARIADIC) + list(TODO))


def c_name(native_name):
    """The C symbol `extern-c` emits: turmeric mangles `-` to `_`.

    Confirmed against the compiler rather than assumed --
        (extern-c godot-export [name :cstr ty :cstr dflt :float] :void)
    emits
        extern void godot_export(const char *, const char *, double);
    """
    return native_name.replace("-", "_").replace(">", "_").replace("?", "_")
