#ifndef TURMERIC_GODOT_BRIDGE_VARIANT_MARSHAL_H
#define TURMERIC_GODOT_BRIDGE_VARIANT_MARSHAL_H

#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>

extern "C" {
#include "turi/value.h"
}

namespace godot {

// --- Primitive marshalling --------------------------------------------------
//
// The TuriValue tagged union has no slot for a string view that outlives the
// call, so any STRING marshalled into Turmeric must keep its utf8 buffer
// alive for the duration of `turi_call`. Callers pass a `CharString *` that
// they own; the helper writes the utf8 buffer there and the returned
// TuriValue points into it.
TuriValue variant_to_turi_primitive(const Variant &v, CharString *str_owner_out);
Variant   turi_to_variant_primitive(TuriValue v);

// --- Variant arena ----------------------------------------------------------
//
// Some Godot Variant types (Vector2, Vector3, Rect2, Color, ...) don't fit a
// single 64-bit slot, so we can't pass them through a bare TuriValue. The
// arena stores Variants thread-locally and hands back a tagged :int handle:
//
//   high bit (1ULL << 62) set                -> arena handle
//   low 62 bits                              -> index into the per-frame slot table
//
// Scripts get a handle from a builder native -- (godot-vec2 x y),
// (godot-vec3 x y z) -- and pass it straight into (godot-call ...). The
// proxy on the way in checks the high bit, looks up the slot, and uses the
// real Variant. Method results that don't fit a primitive (e.g.
// get_position returns Vector2) get pushed into the arena too, and the
// handle is what flows back to script.
//
// Lifetime: each outer cb_call brackets one "frame" -- variant_arena_enter()
// at entry, variant_arena_leave() at exit, which pops everything pushed at
// or above the saved depth. Nested cb_call frames don't see each other's
// handles. A handle leaked outside the call that produced it dereferences
// to nil with a printerr the next time it's used.

constexpr uint64_t TG_ARENA_TAG = (uint64_t)1 << 62;

// Push a Variant onto the current arena frame and return the tagged handle.
int64_t variant_arena_push(const Variant &v);

// Return a pointer to the live Variant for a tagged handle, or nullptr if
// the handle is malformed / out of range / belongs to a popped frame.
const Variant *variant_arena_lookup(int64_t tagged_handle);

// True iff the low bits look like an arena handle (tag bit set).
inline bool variant_arena_is_handle(int64_t v) {
    return (((uint64_t)v) & TG_ARENA_TAG) != 0;
}

// Frame bracket -- call enter() before turi_call, leave(token) after.
// Nesting safe. Pops the variant arena AND the string-return arena (below)
// back to the saved depths.
struct VariantArenaFrame {
    size_t saved_variant_depth;
    size_t saved_string_depth;
};
VariantArenaFrame variant_arena_enter();
void              variant_arena_leave(VariantArenaFrame frame);

// --- String return arena ----------------------------------------------------
//
// TURI_CSTR is a borrowed `const char*`, so any String/StringName we return
// from a Godot call needs its utf8 bytes to outlive the marshal helper.
// The string arena holds owned copies and lives the same lifetime as the
// variant arena (popped together by variant_arena_leave). Returned cstr
// pointers are valid for the rest of the current outer cb_call frame and
// must not be retained across method calls -- the next variant_arena_enter
// of the same frame is fine; a later frame's pop will free them.
const char *string_arena_push(const char *utf8, size_t len);

} // namespace godot

#endif
