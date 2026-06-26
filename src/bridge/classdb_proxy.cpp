#include "classdb_proxy.h"
#include "variant_marshal.h"
#include "../turmeric_instance.h"

extern "C" {
#include "turi/env.h"
}

#include <atomic>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace godot {

// --- Self -------------------------------------------------------------------

TuriValue tg_native_godot_connect(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 3 ||
        args[0].tag != TURI_INT ||
        args[1].tag != TURI_CSTR || !args[1].as_cstr ||
        args[2].tag != TURI_CSTR || !args[2].as_cstr) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect SOURCE SIGNAL METHOD) takes "
            "(:int handle, :cstr, :cstr)");
        return turi_nil();
    }
    if (variant_arena_is_handle(args[0].as_int)) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect) SOURCE must be an Object handle, not an arena value");
        return turi_nil();
    }
    Object *source = (Object *)(intptr_t)args[0].as_int;
    if (!source) {
        UtilityFunctions::printerr("turmeric-godot: (godot-connect) SOURCE is null");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->owner) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect) called outside an instance method; "
            "no `self` to bind the callable to");
        return turi_nil();
    }
    Callable cb(self->owner, StringName(args[2].as_cstr));
    int err = source->connect(StringName(args[1].as_cstr), cb);
    if (err != 0 /* OK */) {
        UtilityFunctions::printerr(
            String("turmeric-godot: (godot-connect '") + String(args[1].as_cstr) +
            String("') failed with error ") + String::num_int64((int64_t)err));
    }
    return turi_nil();
}

TuriValue tg_native_godot_connect_typed(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)ud;
    // (godot-connect-typed SOURCE-OBJ SIGNAL-NAME HANDLER-CLOSURE)
    if (n != 3 ||
        args[0].tag != TURI_INT ||
        args[1].tag != TURI_CSTR || !args[1].as_cstr ||
        args[2].tag != TURI_CLOSURE || !args[2].as_closure) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect-typed SOURCE SIGNAL HANDLER) takes "
            "(:int handle, :cstr, closure)");
        return turi_nil();
    }
    if (variant_arena_is_handle(args[0].as_int)) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect-typed) SOURCE must be an Object "
            "handle, not an arena value");
        return turi_nil();
    }
    Object *source = (Object *)(intptr_t)args[0].as_int;
    if (!source) {
        UtilityFunctions::printerr("turmeric-godot: (godot-connect-typed) SOURCE is null");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->owner) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect-typed) called outside an instance "
            "method; no `self` to bind the callable to");
        return turi_nil();
    }
    if (!env) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect-typed) called without an env (internal bug)");
        return turi_nil();
    }
    // Synthesise a unique method name. The closure is bound into the script
    // env under this name; cb_call routes by name lookup, so Godot's Callable
    // targeting this StringName on self->owner dispatches the closure.
    //
    // The counter is process-global; collisions across script reloads would
    // only matter if the env survived the reload, which it does not.
    static std::atomic<uint64_t> s_counter{0};
    const uint64_t id = s_counter.fetch_add(1, std::memory_order_relaxed);
    char synth[64];
    std::snprintf(synth, sizeof(synth), "__tg_sig_%llu", (unsigned long long)id);
    // turi_env_set stores the name pointer borrowed; copy into the env's
    // value pool so the binding key outlives this stack frame.
    char *synth_pooled = turi_val_strdup(env, synth);
    if (!synth_pooled) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-connect-typed) OOM allocating synth name");
        return turi_nil();
    }
    turi_env_set(env, synth_pooled, args[2]);

    Callable cb(self->owner, StringName(synth_pooled));
    int err = source->connect(StringName(args[1].as_cstr), cb);
    if (err != 0 /* OK */) {
        UtilityFunctions::printerr(
            String("turmeric-godot: (godot-connect-typed '") + String(args[1].as_cstr) +
            String("') failed with error ") + String::num_int64((int64_t)err));
    }
    return turi_nil();
}

TuriValue tg_native_godot_num_to_str(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    char buf[64];
    int len = 0;
    if (n != 1) {
        buf[0] = '\0';
    } else if (args[0].tag == TURI_FLOAT) {
        len = std::snprintf(buf, sizeof(buf), "%g", args[0].as_float);
    } else if (args[0].tag == TURI_INT) {
        len = std::snprintf(buf, sizeof(buf), "%lld", (long long)args[0].as_int);
    } else if (args[0].tag == TURI_BOOL) {
        len = std::snprintf(buf, sizeof(buf), "%s", args[0].as_bool ? "true" : "false");
    } else {
        buf[0] = '\0';
    }
    if (len < 0) len = 0;
    return turi_cstr(string_arena_push(buf, (size_t)len));
}

TuriValue tg_native_godot_singleton(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-singleton NAME) expects a single :cstr arg");
        return turi_int(0);
    }
    Engine *eng = Engine::get_singleton();
    if (!eng) return turi_int(0);
    Object *sing = eng->get_singleton(StringName(args[0].as_cstr));
    if (!sing) {
        UtilityFunctions::printerr(
            String("turmeric-godot: (godot-singleton) no singleton named '") +
            String(args[0].as_cstr) + String("'"));
        return turi_int(0);
    }
    return turi_int((int64_t)(intptr_t)sing);
}

TuriValue tg_native_godot_self(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)ud;
    if (n != 0) {
        UtilityFunctions::printerr("turmeric-godot: (godot-self) takes no arguments");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->owner) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-self) called outside an instance method");
        return turi_nil();
    }
    return turi_int((int64_t)(intptr_t)self->owner);
}

// --- Call -------------------------------------------------------------------

// Marshal one TuriValue into a Variant for the callv arg list. Handles the
// arena-tagged-int case (vec2/vec3/...) by substituting the live Variant.
static Variant tg_arg_to_variant(TuriValue tv, const char *method_for_err, uint32_t i) {
    switch (tv.tag) {
        case TURI_NIL:   return Variant();
        case TURI_BOOL:  return Variant((bool)tv.as_bool);
        case TURI_FLOAT: return Variant((double)tv.as_float);
        case TURI_CSTR:  return Variant(String(tv.as_cstr ? tv.as_cstr : ""));
        case TURI_INT: {
            const int64_t raw = tv.as_int;
            if (variant_arena_is_handle(raw)) {
                const Variant *vp = variant_arena_lookup(raw);
                if (!vp) {
                    UtilityFunctions::printerr(
                        String("turmeric-godot: (godot-call '") + String(method_for_err) +
                        String("') arg ") + String::num_int64((int64_t)i) +
                        String(" is a stale arena handle"));
                    return Variant();
                }
                return *vp;
            }
            return Variant((int64_t)raw);
        }
        default:
            UtilityFunctions::printerr(
                String("turmeric-godot: (godot-call '") + String(method_for_err) +
                String("') arg ") + String::num_int64((int64_t)i) +
                String(" has unsupported tag for v1 marshalling; passing nil"));
            return Variant();
    }
}

// Marshal a method return into a TuriValue.
//   - Primitives (NIL/BOOL/INT/FLOAT)        -> direct.
//   - STRING / STRING_NAME / NODE_PATH       -> string arena, returns cstr.
//   - VECTOR2 / VECTOR3                      -> variant arena, returns tagged :int.
//   - OBJECT                                 -> plain :int Object handle
//                                               (untagged; compatible with the
//                                               OBJ arg to godot-call).
//   - everything else                        -> printerr + nil.
static TuriValue tg_result_to_turi(const Variant &v) {
    switch (v.get_type()) {
        case Variant::NIL:
        case Variant::BOOL:
        case Variant::INT:
        case Variant::FLOAT:
            return variant_to_turi_primitive(v, nullptr);
        case Variant::STRING:
        case Variant::STRING_NAME:
        case Variant::NODE_PATH: {
            String s = v;
            CharString cs = s.utf8();
            return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
        }
        case Variant::VECTOR2:
        case Variant::VECTOR3:
        case Variant::COLOR:
        case Variant::RECT2:
        case Variant::TRANSFORM2D:
        case Variant::TRANSFORM3D:
        case Variant::ARRAY:
        case Variant::DICTIONARY:
        // T3.D -- PackedXxxArray + RID land in the same arena. The
        // refcount-shared Packed* containers and the engine-owned RIDs
        // are usable from Turmeric only through their per-type natives
        // (godot-packed-* / godot-rid-*); marshalling here just keeps
        // their identity intact across godot-call boundaries.
        case Variant::PACKED_BYTE_ARRAY:
        case Variant::PACKED_INT32_ARRAY:
        case Variant::PACKED_INT64_ARRAY:
        case Variant::PACKED_FLOAT32_ARRAY:
        case Variant::PACKED_FLOAT64_ARRAY:
        case Variant::PACKED_STRING_ARRAY:
        case Variant::PACKED_VECTOR2_ARRAY:
        case Variant::PACKED_VECTOR3_ARRAY:
        case Variant::PACKED_COLOR_ARRAY:
        case Variant::RID:
            return turi_int(variant_arena_push(v));
        case Variant::OBJECT: {
            Object *o = (Object *)v;
            return turi_int((int64_t)(intptr_t)o);
        }
        default:
            UtilityFunctions::printerr(
                String("turmeric-godot: (godot-call) return type ") +
                String::num_int64((int64_t)v.get_type()) +
                String(" not yet marshalled; returning nil"));
            return turi_nil();
    }
}

// Shared dispatch core. Returns the raw Variant; the caller marshals to
// the right TuriValue tag. Returns a NIL Variant + sets *ok=false on
// argument-shape errors.
static Variant tg_call_dispatch(TuriValue *args, uint32_t n, bool *ok) {
    *ok = false;
    if (n < 2) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call OBJ METHOD args...) needs at least OBJ and METHOD");
        return Variant();
    }
    if (args[0].tag != TURI_INT || variant_arena_is_handle(args[0].as_int)) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call) OBJ must be an :int Object handle (see godot-self)");
        return Variant();
    }
    if (args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call) METHOD must be a :cstr");
        return Variant();
    }
    Object *obj = (Object *)(intptr_t)args[0].as_int;
    if (!obj) {
        UtilityFunctions::printerr("turmeric-godot: (godot-call) OBJ is a null handle");
        return Variant();
    }
    StringName method(args[1].as_cstr);

    const uint32_t marshal_count = n - 2;
    Array call_args;
    for (uint32_t i = 0; i < marshal_count; i++) {
        call_args.push_back(tg_arg_to_variant(args[2 + i], args[1].as_cstr, i));
    }
    *ok = true;
    return obj->callv(method, call_args);
}

TuriValue tg_native_godot_call(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    Variant result = tg_call_dispatch(args, n, &ok);
    if (!ok) return turi_nil();
    return tg_result_to_turi(result);
}

// Typed variants -- Codegen v2 picks the right one per JSON return type so
// the generated wrapper can declare an honest return type. Each variant is
// registered with turi_register_default_native_typed; the runtime behavior
// is otherwise the same as godot-call except for the post-call coercion.

TuriValue tg_native_godot_call_v(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    (void)tg_call_dispatch(args, n, &ok);
    // void: discard the result regardless of what the method returned.
    return turi_nil();
}

TuriValue tg_native_godot_call_f(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    Variant result = tg_call_dispatch(args, n, &ok);
    if (!ok) return turi_float(0.0);
    // Coerce: if Godot returned an int (some methods declared float in the
    // JSON actually return integral values), widen it. Same logic as the
    // existing primitive marshaller for FLOAT.
    if (result.get_type() == Variant::FLOAT) return turi_float((double)result);
    if (result.get_type() == Variant::INT)   return turi_float((double)(int64_t)result);
    return turi_float(0.0);
}

TuriValue tg_native_godot_call_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    Variant result = tg_call_dispatch(args, n, &ok);
    if (!ok) return turi_bool(false);
    return turi_bool((bool)result);
}

TuriValue tg_native_godot_call_c(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    Variant result = tg_call_dispatch(args, n, &ok);
    if (!ok) return turi_cstr(string_arena_push("", 0));
    // STRING / STRING_NAME / NODE_PATH all stringify via the same path the
    // dynamic marshaller uses in tg_result_to_turi. Anything else gets an
    // empty cstr so the caller still sees a valid (possibly empty) string.
    const Variant::Type t = result.get_type();
    if (t == Variant::STRING || t == Variant::STRING_NAME || t == Variant::NODE_PATH) {
        String s = result;
        CharString cs = s.utf8();
        return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
    }
    return turi_cstr(string_arena_push("", 0));
}

// --- Vector builders --------------------------------------------------------

static bool tg_arg_as_double(TuriValue v, double *out) {
    if (v.tag == TURI_FLOAT) { *out = v.as_float; return true; }
    if (v.tag == TURI_INT)   { *out = (double)v.as_int; return true; }
    return false;
}

TuriValue tg_native_godot_vec2(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double x = 0.0, y = 0.0;
    if (n != 2 || !tg_arg_as_double(args[0], &x) || !tg_arg_as_double(args[1], &y)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-vec2 x y) needs two numbers");
        return turi_nil();
    }
    return turi_int(variant_arena_push(Variant(Vector2((real_t)x, (real_t)y))));
}

TuriValue tg_native_godot_vec3(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double x = 0.0, y = 0.0, z = 0.0;
    if (n != 3 || !tg_arg_as_double(args[0], &x) ||
        !tg_arg_as_double(args[1], &y) || !tg_arg_as_double(args[2], &z)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-vec3 x y z) needs three numbers");
        return turi_nil();
    }
    return turi_int(variant_arena_push(Variant(Vector3((real_t)x, (real_t)y, (real_t)z))));
}

// --- Vector component access ------------------------------------------------

static const Variant *tg_handle_arg(TuriValue v, const char *who) {
    if (v.tag != TURI_INT || !variant_arena_is_handle(v.as_int)) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" expects a vector arena handle"));
        return nullptr;
    }
    const Variant *vp = variant_arena_lookup(v.as_int);
    if (!vp) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" passed a stale arena handle"));
    }
    return vp;
}

TuriValue tg_native_godot_vec2_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-vec2-x v) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-vec2-x)");
    if (!vp || vp->get_type() != Variant::VECTOR2) return turi_nil();
    return turi_float((double)((Vector2)*vp).x);
}

TuriValue tg_native_godot_vec2_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-vec2-y v) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-vec2-y)");
    if (!vp || vp->get_type() != Variant::VECTOR2) return turi_nil();
    return turi_float((double)((Vector2)*vp).y);
}

TuriValue tg_native_godot_vec3_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-vec3-x v) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-vec3-x)");
    if (!vp || vp->get_type() != Variant::VECTOR3) return turi_nil();
    return turi_float((double)((Vector3)*vp).x);
}

TuriValue tg_native_godot_vec3_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-vec3-y v) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-vec3-y)");
    if (!vp || vp->get_type() != Variant::VECTOR3) return turi_nil();
    return turi_float((double)((Vector3)*vp).y);
}

TuriValue tg_native_godot_vec3_z(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-vec3-z v) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-vec3-z)");
    if (!vp || vp->get_type() != Variant::VECTOR3) return turi_nil();
    return turi_float((double)((Vector3)*vp).z);
}

// --- Color ------------------------------------------------------------------

TuriValue tg_native_godot_color(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    if (n != 4 || !tg_arg_as_double(args[0], &r) || !tg_arg_as_double(args[1], &g) ||
        !tg_arg_as_double(args[2], &b) || !tg_arg_as_double(args[3], &a)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-color r g b a) needs four numbers");
        return turi_nil();
    }
    return turi_int(variant_arena_push(Variant(Color((float)r, (float)g, (float)b, (float)a))));
}

static TuriValue tg_color_component(TuriValue h, const char *who, int idx) {
    const Variant *vp = tg_handle_arg(h, who);
    if (!vp || vp->get_type() != Variant::COLOR) return turi_nil();
    Color c = (Color)*vp;
    switch (idx) {
        case 0: return turi_float((double)c.r);
        case 1: return turi_float((double)c.g);
        case 2: return turi_float((double)c.b);
        default: return turi_float((double)c.a);
    }
}

TuriValue tg_native_godot_color_r(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-color-r c) takes 1 arg"); return turi_nil(); }
    return tg_color_component(args[0], "(godot-color-r)", 0);
}
TuriValue tg_native_godot_color_g(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-color-g c) takes 1 arg"); return turi_nil(); }
    return tg_color_component(args[0], "(godot-color-g)", 1);
}
TuriValue tg_native_godot_color_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-color-b c) takes 1 arg"); return turi_nil(); }
    return tg_color_component(args[0], "(godot-color-b)", 2);
}
TuriValue tg_native_godot_color_a(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-color-a c) takes 1 arg"); return turi_nil(); }
    return tg_color_component(args[0], "(godot-color-a)", 3);
}

// --- Rect2 ------------------------------------------------------------------

TuriValue tg_native_godot_rect2(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    if (n != 4 || !tg_arg_as_double(args[0], &x) || !tg_arg_as_double(args[1], &y) ||
        !tg_arg_as_double(args[2], &w) || !tg_arg_as_double(args[3], &h)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-rect2 x y w h) needs four numbers");
        return turi_nil();
    }
    return turi_int(variant_arena_push(
        Variant(Rect2((real_t)x, (real_t)y, (real_t)w, (real_t)h))));
}

static TuriValue tg_rect2_component(TuriValue h, const char *who, int idx) {
    const Variant *vp = tg_handle_arg(h, who);
    if (!vp || vp->get_type() != Variant::RECT2) return turi_nil();
    Rect2 r = (Rect2)*vp;
    switch (idx) {
        case 0: return turi_float((double)r.position.x);
        case 1: return turi_float((double)r.position.y);
        case 2: return turi_float((double)r.size.x);
        default: return turi_float((double)r.size.y);
    }
}

TuriValue tg_native_godot_rect2_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-rect2-x r) takes 1 arg"); return turi_nil(); }
    return tg_rect2_component(args[0], "(godot-rect2-x)", 0);
}
TuriValue tg_native_godot_rect2_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-rect2-y r) takes 1 arg"); return turi_nil(); }
    return tg_rect2_component(args[0], "(godot-rect2-y)", 1);
}
TuriValue tg_native_godot_rect2_w(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-rect2-w r) takes 1 arg"); return turi_nil(); }
    return tg_rect2_component(args[0], "(godot-rect2-w)", 2);
}
TuriValue tg_native_godot_rect2_h(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-rect2-h r) takes 1 arg"); return turi_nil(); }
    return tg_rect2_component(args[0], "(godot-rect2-h)", 3);
}

// --- Transform2D / Transform3D ---------------------------------------------

TuriValue tg_native_godot_xform2d_origin(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-xform2d-origin t) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-xform2d-origin)");
    if (!vp || vp->get_type() != Variant::TRANSFORM2D) return turi_nil();
    Transform2D t = (Transform2D)*vp;
    return turi_int(variant_arena_push(Variant(Vector2(t.get_origin()))));
}

TuriValue tg_native_godot_xform2d_rotation(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-xform2d-rotation t) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-xform2d-rotation)");
    if (!vp || vp->get_type() != Variant::TRANSFORM2D) return turi_nil();
    Transform2D t = (Transform2D)*vp;
    return turi_float((double)t.get_rotation());
}

TuriValue tg_native_godot_xform3d_origin(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-xform3d-origin t) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-xform3d-origin)");
    if (!vp || vp->get_type() != Variant::TRANSFORM3D) return turi_nil();
    Transform3D t = (Transform3D)*vp;
    return turi_int(variant_arena_push(Variant(Vector3(t.origin))));
}

// --- Array ------------------------------------------------------------------

TuriValue tg_native_godot_array_len(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) { UtilityFunctions::printerr("turmeric-godot: (godot-array-len a) takes 1 arg"); return turi_nil(); }
    const Variant *vp = tg_handle_arg(args[0], "(godot-array-len)");
    if (!vp || vp->get_type() != Variant::ARRAY) return turi_int(0);
    return turi_int((int64_t)((Array)*vp).size());
}

TuriValue tg_native_godot_array_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr("turmeric-godot: (godot-array-get a i) takes (handle, :int)");
        return turi_nil();
    }
    const Variant *vp = tg_handle_arg(args[0], "(godot-array-get)");
    if (!vp || vp->get_type() != Variant::ARRAY) return turi_nil();
    Array a = (Array)*vp;
    const int64_t i = args[1].as_int;
    if (i < 0 || i >= (int64_t)a.size()) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-array-get) index ") +
                                   String::num_int64(i) + String(" out of range [0, ") +
                                   String::num_int64((int64_t)a.size()) + String(")"));
        return turi_nil();
    }
    return tg_result_to_turi(a[(int)i]);
}

// --- Dictionary -------------------------------------------------------------

TuriValue tg_native_godot_dict_has(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-dict-has d key) takes (handle, :cstr)");
        return turi_bool(false);
    }
    const Variant *vp = tg_handle_arg(args[0], "(godot-dict-has)");
    if (!vp || vp->get_type() != Variant::DICTIONARY) return turi_bool(false);
    Dictionary d = (Dictionary)*vp;
    return turi_bool(d.has(String(args[1].as_cstr)));
}

TuriValue tg_native_godot_dict_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-dict-get d key) takes (handle, :cstr)");
        return turi_nil();
    }
    const Variant *vp = tg_handle_arg(args[0], "(godot-dict-get)");
    if (!vp || vp->get_type() != Variant::DICTIONARY) return turi_nil();
    Dictionary d = (Dictionary)*vp;
    String key(args[1].as_cstr);
    if (!d.has(key)) return turi_nil();
    return tg_result_to_turi(d[key]);
}

// --- T3.C: builders + mutators + typed accessors ---------------------------
//
// The arena already round-trips Dictionary/Array on godot-call return paths
// (tg_result_to_turi -> variant_arena_push) and on godot-call arg paths
// (tg_arg_to_variant -> arena lookup), so this section is only about giving
// Turmeric callers a way to *construct* and *read fields out of* the handle.

TuriValue tg_native_godot_array_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)ud;
    if (n != 0) {
        UtilityFunctions::printerr("turmeric-godot: (godot-array-new) takes no args");
        return turi_nil();
    }
    return turi_int(variant_arena_push(Variant(Array())));
}

TuriValue tg_native_godot_array_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2) {
        UtilityFunctions::printerr("turmeric-godot: (godot-array-push h v) takes (handle, value)");
        return turi_nil();
    }
    const Variant *vp = tg_handle_arg(args[0], "(godot-array-push)");
    if (!vp || vp->get_type() != Variant::ARRAY) return turi_nil();
    // Godot Array is refcounted; mutating through a copied handle mutates
    // the shared backing store (same as Dictionary below).
    Array a = (Array)*vp;
    a.push_back(tg_arg_to_variant(args[1], "godot-array-push", 1));
    return turi_nil();
}

// Shared body for the typed array reads. Returns nullptr (and emits a
// printerr) on shape/bounds failure; otherwise *out gets the underlying
// element Variant.
static bool tg_array_elem(TuriValue *args, uint32_t n, const char *who, Variant *out) {
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" takes (handle, :int)"));
        return false;
    }
    const Variant *vp = tg_handle_arg(args[0], who);
    if (!vp || vp->get_type() != Variant::ARRAY) return false;
    Array a = (Array)*vp;
    const int64_t i = args[1].as_int;
    if (i < 0 || i >= (int64_t)a.size()) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" index ") + String::num_int64(i) +
                                   String(" out of range [0, ") +
                                   String::num_int64((int64_t)a.size()) + String(")"));
        return false;
    }
    *out = a[(int)i];
    return true;
}

TuriValue tg_native_godot_array_get_i(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_array_elem(args, n, "(godot-array-get-i)", &v)) return turi_int(0);
    if (v.get_type() == Variant::INT)   return turi_int((int64_t)v);
    if (v.get_type() == Variant::FLOAT) return turi_int((int64_t)(double)v);
    if (v.get_type() == Variant::BOOL)  return turi_int((bool)v ? 1 : 0);
    return turi_int(0);
}

TuriValue tg_native_godot_array_get_f(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_array_elem(args, n, "(godot-array-get-f)", &v)) return turi_float(0.0);
    if (v.get_type() == Variant::FLOAT) return turi_float((double)v);
    if (v.get_type() == Variant::INT)   return turi_float((double)(int64_t)v);
    return turi_float(0.0);
}

TuriValue tg_native_godot_array_get_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_array_elem(args, n, "(godot-array-get-b)", &v)) return turi_bool(false);
    return turi_bool((bool)v);
}

TuriValue tg_native_godot_array_get_c(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_array_elem(args, n, "(godot-array-get-c)", &v))
        return turi_cstr(string_arena_push("", 0));
    const Variant::Type t = v.get_type();
    if (t == Variant::STRING || t == Variant::STRING_NAME || t == Variant::NODE_PATH) {
        String s = v;
        CharString cs = s.utf8();
        return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
    }
    return turi_cstr(string_arena_push("", 0));
}

TuriValue tg_native_godot_dict_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)ud;
    if (n != 0) {
        UtilityFunctions::printerr("turmeric-godot: (godot-dict-new) takes no args");
        return turi_nil();
    }
    return turi_int(variant_arena_push(Variant(Dictionary())));
}

TuriValue tg_native_godot_dict_set(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 3 || args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-dict-set d key v) takes (handle, :cstr, value)");
        return turi_nil();
    }
    const Variant *vp = tg_handle_arg(args[0], "(godot-dict-set)");
    if (!vp || vp->get_type() != Variant::DICTIONARY) return turi_nil();
    Dictionary d = (Dictionary)*vp;
    d[String(args[1].as_cstr)] = tg_arg_to_variant(args[2], "godot-dict-set", 2);
    return turi_nil();
}

// Shared body for typed dict reads. Returns false when the key is missing
// or the handle is the wrong shape.
static bool tg_dict_elem(TuriValue *args, uint32_t n, const char *who, Variant *out) {
    if (n != 2 || args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" takes (handle, :cstr)"));
        return false;
    }
    const Variant *vp = tg_handle_arg(args[0], who);
    if (!vp || vp->get_type() != Variant::DICTIONARY) return false;
    Dictionary d = (Dictionary)*vp;
    String key(args[1].as_cstr);
    if (!d.has(key)) return false;
    *out = d[key];
    return true;
}

TuriValue tg_native_godot_dict_get_i(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_dict_elem(args, n, "(godot-dict-get-i)", &v)) return turi_int(0);
    if (v.get_type() == Variant::INT)   return turi_int((int64_t)v);
    if (v.get_type() == Variant::FLOAT) return turi_int((int64_t)(double)v);
    if (v.get_type() == Variant::BOOL)  return turi_int((bool)v ? 1 : 0);
    return turi_int(0);
}

TuriValue tg_native_godot_dict_get_f(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_dict_elem(args, n, "(godot-dict-get-f)", &v)) return turi_float(0.0);
    if (v.get_type() == Variant::FLOAT) return turi_float((double)v);
    if (v.get_type() == Variant::INT)   return turi_float((double)(int64_t)v);
    return turi_float(0.0);
}

TuriValue tg_native_godot_dict_get_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_dict_elem(args, n, "(godot-dict-get-b)", &v)) return turi_bool(false);
    return turi_bool((bool)v);
}

TuriValue tg_native_godot_dict_get_c(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    Variant v;
    if (!tg_dict_elem(args, n, "(godot-dict-get-c)", &v))
        return turi_cstr(string_arena_push("", 0));
    const Variant::Type t = v.get_type();
    if (t == Variant::STRING || t == Variant::STRING_NAME || t == Variant::NODE_PATH) {
        String s = v;
        CharString cs = s.utf8();
        return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
    }
    return turi_cstr(string_arena_push("", 0));
}

// --- T3.D: PackedXxxArray + RID -------------------------------------------
//
// All PackedXxx and RID values live in the same arena. Packed* are
// refcount-shared on the godot-cpp side, so a copied handle mutates the
// shared backing store (same model the Dictionary/Array natives rely on).
// RID is small and value-typed; we expose it read-only.

namespace {

// Common arg-shape: (handle :int) -- returns the live Variant or null.
const Variant *tg_packed_handle(TuriValue v, const char *who) {
    return tg_handle_arg(v, who);
}

// Common arg-shape: (handle :int idx :int) -- returns false + printerr if
// the shape is wrong, the handle is wrong type, or the index is OOB.
bool tg_packed_get_args(TuriValue *args, uint32_t n, Variant::Type want,
                        const char *who, const Variant **out_vp, int64_t *out_i) {
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" takes (handle, :int)"));
        return false;
    }
    const Variant *vp = tg_packed_handle(args[0], who);
    if (!vp || vp->get_type() != want) return false;
    *out_vp = vp;
    *out_i  = args[1].as_int;
    return true;
}

// Sized lookup: same as above but also enforces 0 <= i < size.
template <typename Arr>
bool tg_packed_index_check(const Arr &a, int64_t i, const char *who) {
    if (i < 0 || i >= (int64_t)a.size()) {
        UtilityFunctions::printerr(String("turmeric-godot: ") + String(who) +
                                   String(" index ") + String::num_int64(i) +
                                   String(" out of range [0, ") +
                                   String::num_int64((int64_t)a.size()) + String(")"));
        return false;
    }
    return true;
}

} // namespace

TuriValue tg_native_godot_packed_size(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-size H) takes 1 arg");
        return turi_int(0);
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-size)");
    if (!vp) return turi_int(0);
    switch (vp->get_type()) {
        case Variant::PACKED_BYTE_ARRAY:    return turi_int((int64_t)((PackedByteArray)*vp).size());
        case Variant::PACKED_INT32_ARRAY:   return turi_int((int64_t)((PackedInt32Array)*vp).size());
        case Variant::PACKED_INT64_ARRAY:   return turi_int((int64_t)((PackedInt64Array)*vp).size());
        case Variant::PACKED_FLOAT32_ARRAY: return turi_int((int64_t)((PackedFloat32Array)*vp).size());
        case Variant::PACKED_FLOAT64_ARRAY: return turi_int((int64_t)((PackedFloat64Array)*vp).size());
        case Variant::PACKED_STRING_ARRAY:  return turi_int((int64_t)((PackedStringArray)*vp).size());
        case Variant::PACKED_VECTOR2_ARRAY: return turi_int((int64_t)((PackedVector2Array)*vp).size());
        case Variant::PACKED_VECTOR3_ARRAY: return turi_int((int64_t)((PackedVector3Array)*vp).size());
        case Variant::PACKED_COLOR_ARRAY:   return turi_int((int64_t)((PackedColorArray)*vp).size());
        default:
            UtilityFunctions::printerr(
                "turmeric-godot: (godot-packed-size) handle is not a Packed*Array");
            return turi_int(0);
    }
}

// ---- byte (uint8) ---------------------------------------------------------
TuriValue tg_native_godot_packed_byte_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedByteArray())));
}
TuriValue tg_native_godot_packed_byte_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_BYTE_ARRAY, "(godot-packed-byte-get)", &vp, &i))
        return turi_int(0);
    PackedByteArray a = (PackedByteArray)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-byte-get)")) return turi_int(0);
    return turi_int((int64_t)a[(int)i]);
}
TuriValue tg_native_godot_packed_byte_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-byte-push h v) takes (handle, :int)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-byte-push)");
    if (!vp || vp->get_type() != Variant::PACKED_BYTE_ARRAY) return turi_nil();
    PackedByteArray a = (PackedByteArray)*vp;
    a.push_back((uint8_t)(args[1].as_int & 0xff));
    return turi_nil();
}

// ---- int32 ----------------------------------------------------------------
TuriValue tg_native_godot_packed_int32_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedInt32Array())));
}
TuriValue tg_native_godot_packed_int32_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_INT32_ARRAY, "(godot-packed-int32-get)", &vp, &i))
        return turi_int(0);
    PackedInt32Array a = (PackedInt32Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-int32-get)")) return turi_int(0);
    return turi_int((int64_t)a[(int)i]);
}
TuriValue tg_native_godot_packed_int32_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-int32-push h v) takes (handle, :int)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-int32-push)");
    if (!vp || vp->get_type() != Variant::PACKED_INT32_ARRAY) return turi_nil();
    PackedInt32Array a = (PackedInt32Array)*vp;
    a.push_back((int32_t)args[1].as_int);
    return turi_nil();
}

// ---- int64 ----------------------------------------------------------------
TuriValue tg_native_godot_packed_int64_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedInt64Array())));
}
TuriValue tg_native_godot_packed_int64_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_INT64_ARRAY, "(godot-packed-int64-get)", &vp, &i))
        return turi_int(0);
    PackedInt64Array a = (PackedInt64Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-int64-get)")) return turi_int(0);
    return turi_int((int64_t)a[(int)i]);
}
TuriValue tg_native_godot_packed_int64_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_INT) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-int64-push h v) takes (handle, :int)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-int64-push)");
    if (!vp || vp->get_type() != Variant::PACKED_INT64_ARRAY) return turi_nil();
    PackedInt64Array a = (PackedInt64Array)*vp;
    a.push_back((int64_t)args[1].as_int);
    return turi_nil();
}

// ---- float32 --------------------------------------------------------------
TuriValue tg_native_godot_packed_float32_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedFloat32Array())));
}
TuriValue tg_native_godot_packed_float32_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_FLOAT32_ARRAY,
                            "(godot-packed-float32-get)", &vp, &i))
        return turi_float(0.0);
    PackedFloat32Array a = (PackedFloat32Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-float32-get)")) return turi_float(0.0);
    return turi_float((double)a[(int)i]);
}
TuriValue tg_native_godot_packed_float32_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double f = 0.0;
    if (n != 2 || !tg_arg_as_double(args[1], &f)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-float32-push h v) takes (handle, :float|:int)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-float32-push)");
    if (!vp || vp->get_type() != Variant::PACKED_FLOAT32_ARRAY) return turi_nil();
    PackedFloat32Array a = (PackedFloat32Array)*vp;
    a.push_back((float)f);
    return turi_nil();
}

// ---- float64 --------------------------------------------------------------
TuriValue tg_native_godot_packed_float64_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedFloat64Array())));
}
TuriValue tg_native_godot_packed_float64_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_FLOAT64_ARRAY,
                            "(godot-packed-float64-get)", &vp, &i))
        return turi_float(0.0);
    PackedFloat64Array a = (PackedFloat64Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-float64-get)")) return turi_float(0.0);
    return turi_float((double)a[(int)i]);
}
TuriValue tg_native_godot_packed_float64_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    double f = 0.0;
    if (n != 2 || !tg_arg_as_double(args[1], &f)) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-float64-push h v) takes (handle, :float|:int)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-float64-push)");
    if (!vp || vp->get_type() != Variant::PACKED_FLOAT64_ARRAY) return turi_nil();
    PackedFloat64Array a = (PackedFloat64Array)*vp;
    a.push_back((double)f);
    return turi_nil();
}

// ---- string ---------------------------------------------------------------
TuriValue tg_native_godot_packed_string_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedStringArray())));
}
TuriValue tg_native_godot_packed_string_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_STRING_ARRAY,
                            "(godot-packed-string-get)", &vp, &i))
        return turi_cstr(string_arena_push("", 0));
    PackedStringArray a = (PackedStringArray)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-string-get)"))
        return turi_cstr(string_arena_push("", 0));
    String s = a[(int)i];
    CharString cs = s.utf8();
    return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
}
TuriValue tg_native_godot_packed_string_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-string-push h v) takes (handle, :cstr)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-string-push)");
    if (!vp || vp->get_type() != Variant::PACKED_STRING_ARRAY) return turi_nil();
    PackedStringArray a = (PackedStringArray)*vp;
    a.push_back(String(args[1].as_cstr));
    return turi_nil();
}

// ---- vec2 / vec3 / color (arena-handle element types) --------------------
// get returns a *new* arena handle to the element value; push expects an
// arena handle the caller already has from godot-vec2 / godot-vec3 / godot-color.

TuriValue tg_native_godot_packed_vec2_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedVector2Array())));
}
TuriValue tg_native_godot_packed_vec2_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_VECTOR2_ARRAY,
                            "(godot-packed-vec2-get)", &vp, &i))
        return turi_nil();
    PackedVector2Array a = (PackedVector2Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-vec2-get)")) return turi_nil();
    return turi_int(variant_arena_push(Variant(a[(int)i])));
}
TuriValue tg_native_godot_packed_vec2_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-vec2-push h v) takes (handle, Vec2Handle)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-vec2-push)");
    if (!vp || vp->get_type() != Variant::PACKED_VECTOR2_ARRAY) return turi_nil();
    const Variant *elem = tg_handle_arg(args[1], "(godot-packed-vec2-push) v");
    if (!elem || elem->get_type() != Variant::VECTOR2) return turi_nil();
    PackedVector2Array a = (PackedVector2Array)*vp;
    a.push_back((Vector2)*elem);
    return turi_nil();
}

TuriValue tg_native_godot_packed_vec3_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedVector3Array())));
}
TuriValue tg_native_godot_packed_vec3_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_VECTOR3_ARRAY,
                            "(godot-packed-vec3-get)", &vp, &i))
        return turi_nil();
    PackedVector3Array a = (PackedVector3Array)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-vec3-get)")) return turi_nil();
    return turi_int(variant_arena_push(Variant(a[(int)i])));
}
TuriValue tg_native_godot_packed_vec3_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-vec3-push h v) takes (handle, Vec3Handle)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-vec3-push)");
    if (!vp || vp->get_type() != Variant::PACKED_VECTOR3_ARRAY) return turi_nil();
    const Variant *elem = tg_handle_arg(args[1], "(godot-packed-vec3-push) v");
    if (!elem || elem->get_type() != Variant::VECTOR3) return turi_nil();
    PackedVector3Array a = (PackedVector3Array)*vp;
    a.push_back((Vector3)*elem);
    return turi_nil();
}

TuriValue tg_native_godot_packed_color_new(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)args; (void)n; (void)ud;
    return turi_int(variant_arena_push(Variant(PackedColorArray())));
}
TuriValue tg_native_godot_packed_color_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    const Variant *vp; int64_t i;
    if (!tg_packed_get_args(args, n, Variant::PACKED_COLOR_ARRAY,
                            "(godot-packed-color-get)", &vp, &i))
        return turi_nil();
    PackedColorArray a = (PackedColorArray)*vp;
    if (!tg_packed_index_check(a, i, "(godot-packed-color-get)")) return turi_nil();
    return turi_int(variant_arena_push(Variant(a[(int)i])));
}
TuriValue tg_native_godot_packed_color_push(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2) {
        UtilityFunctions::printerr("turmeric-godot: (godot-packed-color-push h v) takes (handle, ColorHandle)");
        return turi_nil();
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-packed-color-push)");
    if (!vp || vp->get_type() != Variant::PACKED_COLOR_ARRAY) return turi_nil();
    const Variant *elem = tg_handle_arg(args[1], "(godot-packed-color-push) v");
    if (!elem || elem->get_type() != Variant::COLOR) return turi_nil();
    PackedColorArray a = (PackedColorArray)*vp;
    a.push_back((Color)*elem);
    return turi_nil();
}

// ---- RID ------------------------------------------------------------------
TuriValue tg_native_godot_rid_id(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) {
        UtilityFunctions::printerr("turmeric-godot: (godot-rid-id H) takes 1 arg");
        return turi_int(0);
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-rid-id)");
    if (!vp || vp->get_type() != Variant::RID) return turi_int(0);
    RID r = (RID)*vp;
    return turi_int((int64_t)r.get_id());
}

TuriValue tg_native_godot_rid_valid(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) {
        UtilityFunctions::printerr("turmeric-godot: (godot-rid-valid? H) takes 1 arg");
        return turi_bool(false);
    }
    const Variant *vp = tg_packed_handle(args[0], "(godot-rid-valid?)");
    if (!vp || vp->get_type() != Variant::RID) return turi_bool(false);
    RID r = (RID)*vp;
    return turi_bool(r.is_valid());
}

// --- T3.E: vararg dispatch via callv with a trailing ArrayHandle ----------

// Shared body: validate args, build the spread Variant Array, callv.
// Returns the raw Variant result; sets *ok=false on shape errors.
static Variant tg_call_pack_dispatch(TuriValue *args, uint32_t n, bool *ok) {
    *ok = false;
    if (n < 3) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call-pack OBJ METHOD fixed... EXTRAS) "
            "needs OBJ, METHOD, and a trailing ArrayHandle (use (array-new) for none)");
        return Variant();
    }
    if (args[0].tag != TURI_INT || variant_arena_is_handle(args[0].as_int)) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call-pack) OBJ must be an :int Object handle");
        return Variant();
    }
    if (args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call-pack) METHOD must be a :cstr");
        return Variant();
    }
    Object *obj = (Object *)(intptr_t)args[0].as_int;
    if (!obj) {
        UtilityFunctions::printerr("turmeric-godot: (godot-call-pack) OBJ is a null handle");
        return Variant();
    }
    // Trailing ArrayHandle (arena-tagged :int holding a Variant::ARRAY).
    const Variant *tail_vp = nullptr;
    if (args[n - 1].tag != TURI_INT || !variant_arena_is_handle(args[n - 1].as_int) ||
        (tail_vp = variant_arena_lookup(args[n - 1].as_int)) == nullptr ||
        tail_vp->get_type() != Variant::ARRAY) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call-pack) trailing EXTRAS must be an ArrayHandle");
        return Variant();
    }
    Array tail = (Array)*tail_vp;

    Array call_args;
    // Fixed args: indices [2 .. n-1) -- everything between METHOD and EXTRAS.
    for (uint32_t i = 2; i + 1 < n; i++) {
        call_args.push_back(tg_arg_to_variant(args[i], args[1].as_cstr, i));
    }
    // Spread the variadic tail. Tail elements are already Variant; no
    // re-marshalling needed.
    for (int i = 0; i < tail.size(); i++) {
        call_args.push_back(tail[i]);
    }
    *ok = true;
    return obj->callv(StringName(args[1].as_cstr), call_args);
}

TuriValue tg_native_godot_call_pack(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    Variant result = tg_call_pack_dispatch(args, n, &ok);
    if (!ok) return turi_nil();
    return tg_result_to_turi(result);
}

TuriValue tg_native_godot_call_pack_v(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    bool ok = false;
    (void)tg_call_pack_dispatch(args, n, &ok);
    return turi_nil();
}

// --- T4.D: preload --------------------------------------------------------

namespace {
// Process-lifetime cache keyed by absolute resource path. We hold a Ref<>
// to keep the resource alive past the script's life span -- preload's
// whole point is "loaded once, kept around." A WeakRef-style policy is a
// later concern; the GDScript precedent is also strong-ref.
//
// thread_local is wrong here (Godot's main thread is the only Variant
// thread anyway, and a process-lifetime cache should be process-wide);
// not thread_local so two scripts that preload the same path share the
// load.
std::unordered_map<std::string, Ref<Resource>> g_preload_cache;
} // namespace

TuriValue tg_native_godot_preload(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        return turi_error("(godot-preload PATH) takes one :cstr res:// path");
    }
    std::string key(args[0].as_cstr);
    auto it = g_preload_cache.find(key);
    if (it != g_preload_cache.end() && it->second.is_valid()) {
        Object *o = it->second.ptr();
        return turi_int((int64_t)(intptr_t)o);
    }
    String path(args[0].as_cstr);
    ResourceLoader *rl = ResourceLoader::get_singleton();
    if (!rl) {
        return turi_error("(godot-preload) ResourceLoader singleton missing");
    }
    if (!rl->exists(path)) {
        // The plan's compile-time check: a top-level (preload "...") in a
        // script source fails fast at TurmericScript::_reload time, before
        // any gameplay code runs.
        std::string msg = "preload: missing resource '" + key + "'";
        return turi_error(msg.c_str());
    }
    Ref<Resource> res = rl->load(path);
    if (res.is_null()) {
        std::string msg = "preload: load failed for '" + key + "'";
        return turi_error(msg.c_str());
    }
    g_preload_cache[key] = res;
    Object *o = res.ptr();
    return turi_int((int64_t)(intptr_t)o);
}

} // namespace godot
