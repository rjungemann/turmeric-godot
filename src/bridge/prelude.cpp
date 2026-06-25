#include "prelude.h"

namespace godot {

// Scope deliberately small. Two things constrain it:
//
//   1. libturi natives are currently registered without explicit type
//      signatures, so the elaborator treats every godot-* native as
//      returning :int. Wrapping (godot-vec2-x v) in a defn declared
//      `: float` produces TUR-E0707 (return-type mismatch). Until libturi
//      grows typed-native registration, accessors stay un-wrapped -- users
//      call the godot-* native directly when they need the float out.
//
//   2. The full ~30-type spice facade lives in spice/src/godot/ in the
//      plan; this baked-in prelude is the MVP that proves the surface
//      without standing up the spice machinery. Grow entries one per
//      script-side need; promote to the spice tree later.
//
// What IS safe to wrap today: void-returning setters and opaque-handle
// passers (functions whose declared and elaborated types agree because
// nothing reaches into the dynamic tag).
const char *TG_PRELUDE_SOURCE = R"TURMERIC(
;; turmeric-godot baked-in prelude (G3.b MVP).
;; A thin `node/...` facade over godot-call. Only wraps methods whose
;; declared type matches the elaborator's view of the underlying native;
;; for accessors that return float / cstr (vec2-x, get-name, get-rotation,
;; ...), call the godot-* native directly until libturi grows typed natives.

;; --- Self -------------------------------------------------------------------
(defn node/self [] : int (godot-self))

;; --- Node2D position --------------------------------------------------------
;; pos is an arena handle (vec2). Returned by node/get-position.
(defn node/set-position [self : int pos : int]
  (godot-call self "set_position" pos))
(defn node/get-position [self : int] : int
  (godot-call self "get_position"))

;; --- CanvasItem modulate ----------------------------------------------------
;; c is an arena handle (color). Returned by node/get-modulate.
(defn node/set-modulate [self : int c : int]
  (godot-call self "set_modulate" c))
(defn node/get-modulate [self : int] : int
  (godot-call self "get_modulate"))

;; --- Node traversal ---------------------------------------------------------
;; Returns 0 (null) when the path doesn't resolve. Compare with (= h 0).
(defn node/get-node [self : int path : cstr] : int
  (godot-call self "get_node" path))
(defn node/queue-free [self : int]
  (godot-call self "queue_free"))
)TURMERIC";

} // namespace godot
