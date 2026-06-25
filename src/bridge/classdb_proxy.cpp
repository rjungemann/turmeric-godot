#include "classdb_proxy.h"
#include "variant_marshal.h"
#include "../turmeric_instance.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
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
        case Variant::COLOR:
        case Variant::RECT2:
        case Variant::TRANSFORM2D:
        case Variant::TRANSFORM3D:
        case Variant::ARRAY:
        case Variant::DICTIONARY:
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

} // namespace godot
