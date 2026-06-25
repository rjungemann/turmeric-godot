#include "prelude.h"

namespace godot {

// As of libturi 78329855e (turmeric/docs/archive/untyped-native-registration-
// blocks-curated-facades.md) the elaborator consults a typed-native registry,
// so accessor wrappers that declare honest non-:int return types now
// type-check. The prelude is correspondingly larger: vec2/vec3/color/rect2
// accessors all return :float, and we add aliases for the void-returning
// builders too.
//
// What still NEEDS direct godot-* calls instead of a wrapper: anything that
// goes through (godot-call ...), since godot-call's return type is dynamic
// per method name. Those wrappers either stay :int (passing the handle
// through) or live in the generated extension_api.json facade.
const char *TG_PRELUDE_SOURCE = R"TURMERIC(
;; turmeric-godot baked-in prelude (G3.b, post-typed-natives upgrade).
;; Curated short-form facade over the godot-* natives.

;; --- Opaque handle types ----------------------------------------------------
;; Newtypes over :int so the typechecker can distinguish an Object handle
;; from an arena-handle from a raw int. Conversion is by ascription:
;;   (:: h :int)         unwrap to raw
;;   (:: i :NodeHandle)  wrap a raw int
;; Most prelude wrappers still take/return :int today so generated-facade
;; calls compose; the newtypes are available for user scripts that want
;; stricter signatures.
(defopaque NodeHandle :int)
(defopaque Vec2Handle :int)
(defopaque Vec3Handle :int)
(defopaque ColorHandle :int)
(defopaque Rect2Handle :int)

(defn nh->int [h : NodeHandle] : int (:: h :int))
(defn int->nh [i : int] : NodeHandle (:: i :NodeHandle))
(defn vec2h->int [h : Vec2Handle] : int (:: h :int))
(defn int->vec2h [i : int] : Vec2Handle (:: i :Vec2Handle))

;; --- Builders ---------------------------------------------------------------
(defn node/vec2 [x : float y : float] : int (godot-vec2 x y))
(defn node/vec3 [x : float y : float z : float] : int (godot-vec3 x y z))
(defn node/color [r : float g : float b : float a : float] : int
  (godot-color r g b a))
(defn node/rect2 [x : float y : float w : float h : float] : int
  (godot-rect2 x y w h))

;; --- Component accessors (typed now that natives are typed) ----------------
(defn node/vec2-x [v : int] : float (godot-vec2-x v))
(defn node/vec2-y [v : int] : float (godot-vec2-y v))
(defn node/vec3-x [v : int] : float (godot-vec3-x v))
(defn node/vec3-y [v : int] : float (godot-vec3-y v))
(defn node/vec3-z [v : int] : float (godot-vec3-z v))
(defn node/color-r [c : int] : float (godot-color-r c))
(defn node/color-g [c : int] : float (godot-color-g c))
(defn node/color-b [c : int] : float (godot-color-b c))
(defn node/color-a [c : int] : float (godot-color-a c))
(defn node/rect2-x [r : int] : float (godot-rect2-x r))
(defn node/rect2-y [r : int] : float (godot-rect2-y r))
(defn node/rect2-w [r : int] : float (godot-rect2-w r))
(defn node/rect2-h [r : int] : float (godot-rect2-h r))

;; --- Self -------------------------------------------------------------------
(defn node/self [] : int (godot-self))

;; --- Engine singletons -----------------------------------------------------
;; (singleton "Input") -> :int Object handle. Pair with godot-call (or
;; the generated input/... wrappers) to invoke methods on it.
(defn singleton [name : cstr] : int (godot-singleton name))

;; Convenience: Input is the most-touched singleton in gameplay code.
(defn input [] : int (godot-singleton "Input"))
(defn input/is-action-pressed? [action : cstr] : bool
  (godot-call-b (godot-singleton "Input") "is_action_pressed" action))
(defn input/is-action-just-pressed? [action : cstr] : bool
  (godot-call-b (godot-singleton "Input") "is_action_just_pressed" action))

;; --- Node2D position / scale -- pos is an arena vec2 handle ----------------
(defn node/set-position [self : int pos : int]
  (godot-call self "set_position" pos))
(defn node/get-position [self : int] : int
  (godot-call self "get_position"))
(defn node/set-scale [self : int scale : int]
  (godot-call self "set_scale" scale))

;; --- CanvasItem modulate -- c is an arena color handle ---------------------
(defn node/set-modulate [self : int c : int]
  (godot-call self "set_modulate" c))
(defn node/get-modulate [self : int] : int
  (godot-call self "get_modulate"))

;; --- Node traversal --------------------------------------------------------
;; Returns 0 (null) when the path doesn't resolve. Compare with (= h 0)
;; because godot-call's :int return can't be narrowed to :bool statically.
(defn node/get-node [self : int path : cstr] : int
  (godot-call self "get_node" path))
(defn node/queue-free [self : int]
  (godot-call self "queue_free"))

;; Honest :cstr-returning wrappers via godot-call-c. The cstr is valid
;; for the rest of the current outer cb_call frame (string arena
;; lifetime); copy if you need to outlive the method.
(defn node/get-name [self : int] : cstr
  (godot-call-c self "get_name"))
(defn node/get-class [self : int] : cstr
  (godot-call-c self "get_class"))

;; --- Dictionary helpers -- godot-dict-has now returns :bool, so the
;;     usual (if (dict-has? d k) ...) idiom works directly. ------------------
(defn dict-has? [d : int key : cstr] : bool (godot-dict-has d key))
)TURMERIC";

} // namespace godot
