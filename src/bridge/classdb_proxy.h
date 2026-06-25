#ifndef TURMERIC_GODOT_BRIDGE_CLASSDB_PROXY_H
#define TURMERIC_GODOT_BRIDGE_CLASSDB_PROXY_H

extern "C" {
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

// (godot-self) -> :int handle to the current TurmericInstance's Godot owner.
TuriValue tg_native_godot_self(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-singleton "Name") -> :int Object handle to the named engine
// singleton (Input, Engine, OS, RenderingServer, ...). Returns 0 for
// an unknown singleton. Pair with godot-call to invoke methods on it:
//   (godot-call (godot-singleton "Input") "is_action_pressed" "ui_up")
TuriValue tg_native_godot_singleton(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-num->str X) -> :cstr formatted via the string arena. Accepts :int
// or :float; anything else returns "". Lifetime is the per-frame string
// arena (same as godot-call-c and the other cstr returns).
TuriValue tg_native_godot_num_to_str(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-connect SOURCE-OBJ SIGNAL-NAME METHOD-NAME) -> :void
// Subscribes the current instance's `METHOD-NAME` defn to SOURCE-OBJ's
// signal. METHOD-NAME is the bare Turmeric defn name (no namespace);
// dispatch goes through cb_call exactly like _ready / _process.
TuriValue tg_native_godot_connect(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-call OBJ METHOD args...) -> primitive or arena-handle result.
//
// OBJ: :int Object handle (from godot-self).
// METHOD: :cstr.
// args: primitive TuriValues (int/float/bool/cstr/nil) OR :int arena
// handles produced by (godot-vec2 ...) / (godot-vec3 ...).
//
// Result: TURI_NIL for void; primitive for primitive returns; tagged :int
// arena handle for Vector2/Vector3 (and other arena-eligible types as they
// are added).
TuriValue tg_native_godot_call(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// Typed variants -- same dispatch, registered with a fixed return-type
// signature so generated wrappers can declare honest return types.
// godot-call-v ignores the result (void); godot-call-f forces float;
// godot-call-b forces bool.
TuriValue tg_native_godot_call_v(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_call_f(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_call_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_call_c(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-vec2 x y) -> :int arena handle wrapping Vector2(x, y).
TuriValue tg_native_godot_vec2(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-vec3 x y z) -> :int arena handle wrapping Vector3(x, y, z).
TuriValue tg_native_godot_vec3(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-vec2-x H) / (godot-vec2-y H) / (godot-vec3-x H) ... -> :float
// component access for arena-handle vectors.
TuriValue tg_native_godot_vec2_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_vec2_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_vec3_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_vec3_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_vec3_z(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-color r g b a) -> :int arena handle wrapping Color(r, g, b, a).
TuriValue tg_native_godot_color(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_color_r(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_color_g(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_color_b(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_color_a(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-rect2 x y w h) -> :int arena handle wrapping Rect2(x, y, w, h).
TuriValue tg_native_godot_rect2(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_rect2_x(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_rect2_y(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_rect2_w(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_rect2_h(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// Transform2D / Transform3D -- accessors only for v1; full matrix
// construction is deferred until a real demo needs it. Embedders that need
// to *set* a transform today construct it via Godot's own helpers and
// hand the resulting handle back through godot-call.
TuriValue tg_native_godot_xform2d_origin(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_xform2d_rotation(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_xform3d_origin(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-array-len h) -> :int  (length of Array arena handle, or 0 for nil)
// (godot-array-get h i) -> marshalled element (primitive or arena handle)
TuriValue tg_native_godot_array_len(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_array_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

// (godot-dict-has h key) -> :bool  (key: :cstr)
// (godot-dict-get h key) -> marshalled element (nil if missing)
TuriValue tg_native_godot_dict_has(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);
TuriValue tg_native_godot_dict_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

} // namespace godot

#endif
