#include "classdb_proxy.h"
#include "variant_marshal.h"
#include "../turmeric_instance.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace godot {

// --- Self -------------------------------------------------------------------

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

TuriValue tg_native_godot_call(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n < 2) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call OBJ METHOD args...) needs at least OBJ and METHOD");
        return turi_nil();
    }
    if (args[0].tag != TURI_INT || variant_arena_is_handle(args[0].as_int)) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call) OBJ must be an :int Object handle (see godot-self)");
        return turi_nil();
    }
    if (args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr(
            "turmeric-godot: (godot-call) METHOD must be a :cstr");
        return turi_nil();
    }
    Object *obj = (Object *)(intptr_t)args[0].as_int;
    if (!obj) {
        UtilityFunctions::printerr("turmeric-godot: (godot-call) OBJ is a null handle");
        return turi_nil();
    }
    StringName method(args[1].as_cstr);

    const uint32_t marshal_count = n - 2;
    Array call_args;
    for (uint32_t i = 0; i < marshal_count; i++) {
        call_args.push_back(tg_arg_to_variant(args[2 + i], args[1].as_cstr, i));
    }

    Variant result = obj->callv(method, call_args);
    return tg_result_to_turi(result);
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

} // namespace godot
