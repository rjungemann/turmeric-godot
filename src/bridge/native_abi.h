// native_abi.h -- the C ABI the AOT path calls the godot-* natives through.
//
// The interpreter reaches a native through libturi's registry: a name maps to
// a `TuriValue f(TuriEnv*, TuriValue*, uint32_t, void*)` and the arguments are
// tagged unions built at run time. Compiled code cannot do that. `tur` emits a
// direct, typed C call -- `godot_vec2(240.0, 0.0)` -- against a prototype the
// staged project declares with `(extern-c ...)`, so every native the AOT path
// touches needs a real exported C symbol with a concrete signature.
//
// This header is that ABI, declared once:
//
//   * TG_NATIVES_{0,1,2,3,4}(X) are X-macro lists, one per arity. Each row
//     names the Turmeric spelling, the C symbol, the interpreter implementation
//     it forwards to, the return type code, and (type, name) for each
//     parameter.
//   * native_abi.cpp expands them into `extern "C"` entry points that marshal
//     the C arguments into a TuriValue[] and call the same implementation the
//     interpreter uses. No logic is duplicated -- an entry point is a
//     calling-convention adapter and nothing else.
//   * aot_natives.cpp expands the same rows into the `(extern-c ...)` text of
//     the staged `tg-godot.tur` module. Prototype and declaration therefore
//     cannot drift: one list generates both.
//
// Type codes:  I = :int (int64_t)   F = :float (double)   B = :bool (bool)
//              C = :cstr (const char *)   V = :void
//
// The mangling `(extern-c godot-vec2 ...)` applies is the "legacy fold" in
// elab_mangle_binding_name: every byte that is not [A-Za-z0-9_] becomes '_'.
// So `godot-num->str` is `godot_num__str` and `godot-rid-valid?` is
// `godot_rid_valid_`. The C symbols below are written out rather than derived,
// so a fold change shows up as a link error and not as silent misbinding.
//
// WHAT IS NOT HERE, and why
//
//   * Variadic natives -- `godot-call`, `godot-signal`, `emit-signal`. The
//     interpreter dispatches on the runtime arg count; `extern-c` has no
//     variadic form (elab_extern_c hardcodes is_variadic = false), and calling
//     a C variadic through a fixed prototype is ABI-invalid on Apple arm64.
//     `godot-call` is replaced for AOT purposes by the arity- and
//     type-specialised `godot-callx-*` family below.
//   * Closure-taking natives -- `godot-connect-typed`. A Turmeric closure is a
//     TuriClosure* in the interpreter and a fat pointer in compiled code;
//     there is no shared representation to hand across.
//   * Natives whose argument type is decided by the runtime tag --
//     `godot-prop-set`, `godot-export`, `godot-array-push`, `godot-dict-set`.
//     Each gets typed `-i` / `-f` / `-b` / `-c` siblings, registered in the
//     interpreter as well so both worlds spell them the same way.

#ifndef TURMERIC_GODOT_BRIDGE_NATIVE_ABI_H
#define TURMERIC_GODOT_BRIDGE_NATIVE_ABI_H

#include <cstddef>
#include <cstdint>

namespace godot {

// One AOT-callable native. `params` is already Turmeric parameter syntax
// ("x : float y : float"), `ret` a bare type name ("float"), so emitting the
// declaration is concatenation and nothing more.
struct TgNativeAbi {
    const char *tur_name;   // "godot-vec2"
    const char *c_name;     // "godot_vec2"
    const char *params;     // "x : float y : float"   ("" for arity 0)
    const char *ret;        // "int"
};

extern const TgNativeAbi TG_NATIVE_ABI[];
extern const size_t      TG_NATIVE_ABI_COUNT;

} // namespace godot

// TUR_NRT_* codes (runtime/globals.h) keyed by the same type letters, so
// init_turi() can register every row from this list instead of a parallel
// hand-written block. The interpreter and the AOT declaration therefore agree
// on the return type by construction.
#define TG_NRT_I TUR_NRT_INT
#define TG_NRT_F TUR_NRT_FLOAT
#define TG_NRT_B TUR_NRT_BOOL
#define TG_NRT_C TUR_NRT_CSTR
#define TG_NRT_V TUR_NRT_VOID

// ---------------------------------------------------------------------------
// The rows.
// ---------------------------------------------------------------------------
//
// X(tur_name, c_name, impl, RET [, T0, n0 [, T1, n1 ...]])

#define TG_NATIVES_0(X)                                                                  \
    X("godot-self",               godot_self,               tg_native_godot_self,               I) \
    X("godot-array-new",          godot_array_new,          tg_native_godot_array_new,          I) \
    X("godot-dict-new",           godot_dict_new,           tg_native_godot_dict_new,           I) \
    X("godot-packed-byte-new",    godot_packed_byte_new,    tg_native_godot_packed_byte_new,    I) \
    X("godot-packed-int32-new",   godot_packed_int32_new,   tg_native_godot_packed_int32_new,   I) \
    X("godot-packed-int64-new",   godot_packed_int64_new,   tg_native_godot_packed_int64_new,   I) \
    X("godot-packed-float32-new", godot_packed_float32_new, tg_native_godot_packed_float32_new, I) \
    X("godot-packed-float64-new", godot_packed_float64_new, tg_native_godot_packed_float64_new, I) \
    X("godot-packed-string-new",  godot_packed_string_new,  tg_native_godot_packed_string_new,  I) \
    X("godot-packed-vec2-new",    godot_packed_vec2_new,    tg_native_godot_packed_vec2_new,    I) \
    X("godot-packed-vec3-new",    godot_packed_vec3_new,    tg_native_godot_packed_vec3_new,    I) \
    X("godot-packed-color-new",   godot_packed_color_new,   tg_native_godot_packed_color_new,   I)

#define TG_NATIVES_1(X)                                                                  \
    X("godot-println",          godot_println,          tg_native_println,                  V, C, "msg")  \
    X("godot-prop-get",         godot_prop_get,         tg_native_prop_get,                 I, C, "name") \
    X("godot-prop-get-i",       godot_prop_get_i,       tg_native_prop_get,                 I, C, "name") \
    X("godot-prop-get-f",       godot_prop_get_f,       tg_native_prop_get,                 F, C, "name") \
    X("godot-prop-get-b",       godot_prop_get_b,       tg_native_prop_get,                 B, C, "name") \
    X("godot-prop-get-c",       godot_prop_get_c,       tg_native_prop_get_c,               C, C, "name") \
    X("godot-singleton",        godot_singleton,        tg_native_godot_singleton,          I, C, "name") \
    X("godot-num->str",         godot_num__str,         tg_native_godot_num_to_str,         C, F, "x")    \
    X("godot-num->str-i",       godot_num__str_i,       tg_native_godot_num_to_str,         C, I, "x")    \
    X("godot-vec2-x",           godot_vec2_x,           tg_native_godot_vec2_x,             F, I, "h")    \
    X("godot-vec2-y",           godot_vec2_y,           tg_native_godot_vec2_y,             F, I, "h")    \
    X("godot-vec3-x",           godot_vec3_x,           tg_native_godot_vec3_x,             F, I, "h")    \
    X("godot-vec3-y",           godot_vec3_y,           tg_native_godot_vec3_y,             F, I, "h")    \
    X("godot-vec3-z",           godot_vec3_z,           tg_native_godot_vec3_z,             F, I, "h")    \
    X("godot-color-r",          godot_color_r,          tg_native_godot_color_r,            F, I, "h")    \
    X("godot-color-g",          godot_color_g,          tg_native_godot_color_g,            F, I, "h")    \
    X("godot-color-b",          godot_color_b,          tg_native_godot_color_b,            F, I, "h")    \
    X("godot-color-a",          godot_color_a,          tg_native_godot_color_a,            F, I, "h")    \
    X("godot-rect2-x",          godot_rect2_x,          tg_native_godot_rect2_x,            F, I, "h")    \
    X("godot-rect2-y",          godot_rect2_y,          tg_native_godot_rect2_y,            F, I, "h")    \
    X("godot-rect2-w",          godot_rect2_w,          tg_native_godot_rect2_w,            F, I, "h")    \
    X("godot-rect2-h",          godot_rect2_h,          tg_native_godot_rect2_h,            F, I, "h")    \
    X("godot-xform2d-origin",   godot_xform2d_origin,   tg_native_godot_xform2d_origin,     I, I, "h")    \
    X("godot-xform2d-rotation", godot_xform2d_rotation, tg_native_godot_xform2d_rotation,   F, I, "h")    \
    X("godot-xform3d-origin",   godot_xform3d_origin,   tg_native_godot_xform3d_origin,     I, I, "h")    \
    X("godot-array-len",        godot_array_len,        tg_native_godot_array_len,          I, I, "h")    \
    X("godot-packed-size",      godot_packed_size,      tg_native_godot_packed_size,        I, I, "h")    \
    X("godot-rid-id",           godot_rid_id,           tg_native_godot_rid_id,             I, I, "h")    \
    X("godot-rid-valid?",       godot_rid_valid_,       tg_native_godot_rid_valid,          B, I, "h")    \
    X("godot-preload",          godot_preload,          tg_native_godot_preload,            I, C, "path")

#define TG_NATIVES_2(X)                                                                  \
    X("godot-vec2",                godot_vec2,                tg_native_godot_vec2,                I, F, "x", F, "y")    \
    X("godot-array-get",           godot_array_get,           tg_native_godot_array_get,           I, I, "h", I, "i")    \
    X("godot-array-get-i",         godot_array_get_i,         tg_native_godot_array_get_i,         I, I, "h", I, "i")    \
    X("godot-array-get-f",         godot_array_get_f,         tg_native_godot_array_get_f,         F, I, "h", I, "i")    \
    X("godot-array-get-b",         godot_array_get_b,         tg_native_godot_array_get_b,         B, I, "h", I, "i")    \
    X("godot-array-get-c",         godot_array_get_c,         tg_native_godot_array_get_c,         C, I, "h", I, "i")    \
    X("godot-array-push-i",        godot_array_push_i,        tg_native_godot_array_push,          V, I, "h", I, "v")    \
    X("godot-array-push-f",        godot_array_push_f,        tg_native_godot_array_push,          V, I, "h", F, "v")    \
    X("godot-array-push-b",        godot_array_push_b,        tg_native_godot_array_push,          V, I, "h", B, "v")    \
    X("godot-array-push-c",        godot_array_push_c,        tg_native_godot_array_push,          V, I, "h", C, "v")    \
    X("godot-dict-has",            godot_dict_has,            tg_native_godot_dict_has,            B, I, "h", C, "k")    \
    X("godot-dict-get",            godot_dict_get,            tg_native_godot_dict_get,            I, I, "h", C, "k")    \
    X("godot-dict-get-i",          godot_dict_get_i,          tg_native_godot_dict_get_i,          I, I, "h", C, "k")    \
    X("godot-dict-get-f",          godot_dict_get_f,          tg_native_godot_dict_get_f,          F, I, "h", C, "k")    \
    X("godot-dict-get-b",          godot_dict_get_b,          tg_native_godot_dict_get_b,          B, I, "h", C, "k")    \
    X("godot-dict-get-c",          godot_dict_get_c,          tg_native_godot_dict_get_c,          C, I, "h", C, "k")    \
    X("godot-prop-set",            godot_prop_set,            tg_native_prop_set,                  V, C, "name", F, "v") \
    X("godot-prop-set-f",          godot_prop_set_f,          tg_native_prop_set,                  V, C, "name", F, "v") \
    X("godot-prop-set-i",          godot_prop_set_i,          tg_native_prop_set,                  V, C, "name", I, "v") \
    X("godot-prop-set-b",          godot_prop_set_b,          tg_native_prop_set,                  V, C, "name", B, "v") \
    X("godot-prop-set-c",          godot_prop_set_c,          tg_native_prop_set,                  V, C, "name", C, "v") \
    X("godot-packed-byte-get",     godot_packed_byte_get,     tg_native_godot_packed_byte_get,     I, I, "h", I, "i")    \
    X("godot-packed-byte-push",    godot_packed_byte_push,    tg_native_godot_packed_byte_push,    V, I, "h", I, "v")    \
    X("godot-packed-int32-get",    godot_packed_int32_get,    tg_native_godot_packed_int32_get,    I, I, "h", I, "i")    \
    X("godot-packed-int32-push",   godot_packed_int32_push,   tg_native_godot_packed_int32_push,   V, I, "h", I, "v")    \
    X("godot-packed-int64-get",    godot_packed_int64_get,    tg_native_godot_packed_int64_get,    I, I, "h", I, "i")    \
    X("godot-packed-int64-push",   godot_packed_int64_push,   tg_native_godot_packed_int64_push,   V, I, "h", I, "v")    \
    X("godot-packed-float32-get",  godot_packed_float32_get,  tg_native_godot_packed_float32_get,  F, I, "h", I, "i")    \
    X("godot-packed-float32-push", godot_packed_float32_push, tg_native_godot_packed_float32_push, V, I, "h", F, "v")    \
    X("godot-packed-float64-get",  godot_packed_float64_get,  tg_native_godot_packed_float64_get,  F, I, "h", I, "i")    \
    X("godot-packed-float64-push", godot_packed_float64_push, tg_native_godot_packed_float64_push, V, I, "h", F, "v")    \
    X("godot-packed-string-get",   godot_packed_string_get,   tg_native_godot_packed_string_get,   C, I, "h", I, "i")    \
    X("godot-packed-string-push",  godot_packed_string_push,  tg_native_godot_packed_string_push,  V, I, "h", C, "v")    \
    X("godot-packed-vec2-get",     godot_packed_vec2_get,     tg_native_godot_packed_vec2_get,     I, I, "h", I, "i")    \
    X("godot-packed-vec2-push",    godot_packed_vec2_push,    tg_native_godot_packed_vec2_push,    V, I, "h", I, "v")    \
    X("godot-packed-vec3-get",     godot_packed_vec3_get,     tg_native_godot_packed_vec3_get,     I, I, "h", I, "i")    \
    X("godot-packed-vec3-push",    godot_packed_vec3_push,    tg_native_godot_packed_vec3_push,    V, I, "h", I, "v")    \
    X("godot-packed-color-get",    godot_packed_color_get,    tg_native_godot_packed_color_get,    I, I, "h", I, "i")    \
    X("godot-packed-color-push",   godot_packed_color_push,   tg_native_godot_packed_color_push,   V, I, "h", I, "v")    \
    X("godot-callx-v",             godot_callx_v,             tg_native_godot_call_v,              V, I, "obj", C, "m") \
    X("godot-callx-i",             godot_callx_i,             tg_native_godot_call,                I, I, "obj", C, "m") \
    X("godot-callx-f",             godot_callx_f,             tg_native_godot_call_f,              F, I, "obj", C, "m") \
    X("godot-callx-b",             godot_callx_b,             tg_native_godot_call_b,              B, I, "obj", C, "m") \
    X("godot-callx-c",             godot_callx_c,             tg_native_godot_call_c,              C, I, "obj", C, "m")

#define TG_NATIVES_3(X)                                                                  \
    X("godot-vec3",          godot_vec3,          tg_native_godot_vec3,          I, F, "x",    F, "y",  F, "z")      \
    X("godot-export",        godot_export,        tg_native_export,              V, C, "name", C, "ty", F, "dflt")   \
    X("godot-export-f",      godot_export_f,      tg_native_export,              V, C, "name", C, "ty", F, "dflt")   \
    X("godot-export-i",      godot_export_i,      tg_native_export,              V, C, "name", C, "ty", I, "dflt")   \
    X("godot-export-b",      godot_export_b,      tg_native_export,              V, C, "name", C, "ty", B, "dflt")   \
    X("godot-export-c",      godot_export_c,      tg_native_export,              V, C, "name", C, "ty", C, "dflt")   \
    X("godot-connect",       godot_connect,       tg_native_godot_connect,       V, I, "obj",  C, "sig", C, "method")\
    X("godot-dict-set-i",    godot_dict_set_i,    tg_native_godot_dict_set,      V, I, "h",    C, "k",  I, "v")      \
    X("godot-dict-set-f",    godot_dict_set_f,    tg_native_godot_dict_set,      V, I, "h",    C, "k",  F, "v")      \
    X("godot-dict-set-b",    godot_dict_set_b,    tg_native_godot_dict_set,      V, I, "h",    C, "k",  B, "v")      \
    X("godot-dict-set-c",    godot_dict_set_c,    tg_native_godot_dict_set,      V, I, "h",    C, "k",  C, "v")      \
    X("godot-call-pack",     godot_call_pack,     tg_native_godot_call_pack,     I, I, "obj",  C, "m",  I, "extras") \
    X("godot-call-pack-v",   godot_call_pack_v,   tg_native_godot_call_pack_v,   V, I, "obj",  C, "m",  I, "extras") \
    X("godot-callx-v-i",     godot_callx_v_i,     tg_native_godot_call_v,        V, I, "obj",  C, "m",  I, "a0")     \
    X("godot-callx-v-f",     godot_callx_v_f,     tg_native_godot_call_v,        V, I, "obj",  C, "m",  F, "a0")     \
    X("godot-callx-v-b",     godot_callx_v_b,     tg_native_godot_call_v,        V, I, "obj",  C, "m",  B, "a0")     \
    X("godot-callx-v-c",     godot_callx_v_c,     tg_native_godot_call_v,        V, I, "obj",  C, "m",  C, "a0")     \
    X("godot-callx-i-i",     godot_callx_i_i,     tg_native_godot_call,          I, I, "obj",  C, "m",  I, "a0")     \
    X("godot-callx-i-f",     godot_callx_i_f,     tg_native_godot_call,          I, I, "obj",  C, "m",  F, "a0")     \
    X("godot-callx-i-b",     godot_callx_i_b,     tg_native_godot_call,          I, I, "obj",  C, "m",  B, "a0")     \
    X("godot-callx-i-c",     godot_callx_i_c,     tg_native_godot_call,          I, I, "obj",  C, "m",  C, "a0")     \
    X("godot-callx-f-i",     godot_callx_f_i,     tg_native_godot_call_f,        F, I, "obj",  C, "m",  I, "a0")     \
    X("godot-callx-f-f",     godot_callx_f_f,     tg_native_godot_call_f,        F, I, "obj",  C, "m",  F, "a0")     \
    X("godot-callx-f-b",     godot_callx_f_b,     tg_native_godot_call_f,        F, I, "obj",  C, "m",  B, "a0")     \
    X("godot-callx-f-c",     godot_callx_f_c,     tg_native_godot_call_f,        F, I, "obj",  C, "m",  C, "a0")     \
    X("godot-callx-b-i",     godot_callx_b_i,     tg_native_godot_call_b,        B, I, "obj",  C, "m",  I, "a0")     \
    X("godot-callx-b-f",     godot_callx_b_f,     tg_native_godot_call_b,        B, I, "obj",  C, "m",  F, "a0")     \
    X("godot-callx-b-b",     godot_callx_b_b,     tg_native_godot_call_b,        B, I, "obj",  C, "m",  B, "a0")     \
    X("godot-callx-b-c",     godot_callx_b_c,     tg_native_godot_call_b,        B, I, "obj",  C, "m",  C, "a0")     \
    X("godot-callx-c-i",     godot_callx_c_i,     tg_native_godot_call_c,        C, I, "obj",  C, "m",  I, "a0")     \
    X("godot-callx-c-f",     godot_callx_c_f,     tg_native_godot_call_c,        C, I, "obj",  C, "m",  F, "a0")     \
    X("godot-callx-c-b",     godot_callx_c_b,     tg_native_godot_call_c,        C, I, "obj",  C, "m",  B, "a0")     \
    X("godot-callx-c-c",     godot_callx_c_c,     tg_native_godot_call_c,        C, I, "obj",  C, "m",  C, "a0")

// The natives that exist ONLY on the interpreter path, with the reason. A
// staged build that trips over one of these produces a bare "unknown function
// or operator", which says nothing about why the name works in the editor and
// not in the AOT build -- aot_natives.cpp turns each row into a note appended
// to the build log.
#define TG_INTERP_ONLY_NATIVES(X)                                                       \
    X("godot-call",   "variadic; use the arity-typed godot-callx-* family, or a curated "\
                      "prelude wrapper such as (node/get-node self path)")               \
    X("godot-call-v", "variadic; use godot-callx-v / godot-callx-v-<i|f|b|c>")           \
    X("godot-call-f", "variadic; use godot-callx-f / godot-callx-f-<i|f|b|c>")           \
    X("godot-call-b", "variadic; use godot-callx-b / godot-callx-b-<i|f|b|c>")           \
    X("godot-call-c", "variadic; use godot-callx-c / godot-callx-c-<i|f|b|c>")           \
    X("godot-signal", "variadic; signal declarations are read by the interpreter pass "  \
                      "and only need to be at top level, where they are dropped")        \
    X("emit-signal",  "variadic; no AOT entry point yet")                                \
    X("godot-connect-typed", "takes a Turmeric closure, which has no shared "            \
                             "representation between interpreter and compiled code")     \
    X("godot-array-push", "argument type is decided by the runtime tag; use "            \
                          "godot-array-push-<i|f|b|c>")                                  \
    X("godot-dict-set",   "argument type is decided by the runtime tag; use "            \
                          "godot-dict-set-<i|f|b|c>")                                    \
    X("timer/one-shot",   "prelude wrapper over godot-connect-typed -- closure argument") \
    X("after",            "prelude wrapper over godot-connect-typed -- closure argument")

#define TG_NATIVES_4(X)                                                                  \
    X("godot-color", godot_color, tg_native_godot_color, I, F, "r", F, "g", F, "b", F, "a") \
    X("godot-rect2", godot_rect2, tg_native_godot_rect2, I, F, "x", F, "y", F, "w", F, "h")

#endif
