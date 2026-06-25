#ifndef TURMERIC_GODOT_BRIDGE_PRELUDE_H
#define TURMERIC_GODOT_BRIDGE_PRELUDE_H

namespace godot {

// G3.b MVP -- a small, baked-in Turmeric prelude that defines the
// `node/...` curated facade as thin wrappers over the godot-* natives
// (godot-call, godot-self, godot-vec2/3, godot-color, ...).
//
// Every TurmericScript evaluates this string in its TuriEnv before the
// user's source, so scripts can write
//
//   (node/set-position self (node/vec2 100.0 50.0))
//
// instead of
//
//   (godot-call self "set_position" (godot-vec2 100.0 50.0))
//
// This is intentionally not a full spice tree (no build.tur, no import
// resolution); the plan's spice-based facade is the eventual home, but
// shipping the surface as a baked-in prelude proves it today without
// touching the module-resolution machinery.
extern const char *TG_PRELUDE_SOURCE;

} // namespace godot

#endif
