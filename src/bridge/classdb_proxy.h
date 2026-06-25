#ifndef TURMERIC_GODOT_BRIDGE_CLASSDB_PROXY_H
#define TURMERIC_GODOT_BRIDGE_CLASSDB_PROXY_H

extern "C" {
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

// (godot-self) -> :int handle to the current TurmericInstance's Godot owner.
TuriValue tg_native_godot_self(TuriEnv *env, TuriValue *args, uint32_t n, void *ud);

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

} // namespace godot

#endif
