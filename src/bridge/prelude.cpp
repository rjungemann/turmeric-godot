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
;; T2.A -- arena handles G6 deferred. Same defopaque-wrapped-:int shape
;; as the existing four; the runtime value is still an arena tag in the
;; low 62 bits + the high arena-handle bit. Promoting them lets the
;; generator's arena-typed mapping (T2.A.3) emit honest signatures
;; instead of falling through to bare :int.
(defopaque Transform2DHandle :int)
(defopaque Transform3DHandle :int)
(defopaque ArrayHandle       :int)
(defopaque DictHandle        :int)
;; T3.D -- PackedXxxArray + RID share the same arena. Distinct defopaques
;; so a (defn ... [b : PackedByteHandle i : PackedInt32Handle] ...) rejects
;; cross-assignment at elaboration time; the natives still consume them via
;; (:: h :int).
(defopaque PackedByteHandle    :int)
(defopaque PackedInt32Handle   :int)
(defopaque PackedInt64Handle   :int)
(defopaque PackedFloat32Handle :int)
(defopaque PackedFloat64Handle :int)
(defopaque PackedStringHandle  :int)
(defopaque PackedVec2Handle    :int)
(defopaque PackedVec3Handle    :int)
(defopaque PackedColorHandle   :int)
(defopaque RidHandle           :int)

(defn nh->int [h : NodeHandle] : int (:: h :int))
(defn int->nh [i : int] : NodeHandle (:: i :NodeHandle))
(defn vec2h->int [h : Vec2Handle] : int (:: h :int))
(defn int->vec2h [i : int] : Vec2Handle (:: i :Vec2Handle))

;; --- Builders ---------------------------------------------------------------
;; T2.A -- builders now return the arena Handle newtype rather than bare
;; :int. The untyped natives return :int (registry default); the ascription
;; back to :Vec2Handle / etc. is the no-op cast that wraps the runtime
;; arena tag in a nominally-distinct type at the type level.
(defn node/vec2 [x : float y : float] : Vec2Handle
  (:: (godot-vec2 x y) :Vec2Handle))
(defn node/vec3 [x : float y : float z : float] : Vec3Handle
  (:: (godot-vec3 x y z) :Vec3Handle))
(defn node/color [r : float g : float b : float a : float] : ColorHandle
  (:: (godot-color r g b a) :ColorHandle))
(defn node/rect2 [x : float y : float w : float h : float] : Rect2Handle
  (:: (godot-rect2 x y w h) :Rect2Handle))

;; --- Component accessors (T2.A -- typed Handle inputs) ---------------------
;; Each accessor takes the Handle newtype, demoting back to :int at the
;; native-call boundary. The arg-typed-native pass (deferred per the
;; typed-native-registration archive) makes the demotion required; if
;; arg-type-checking lands later, the ascriptions become structural noops.
(defn node/vec2-x [v : Vec2Handle] : float (godot-vec2-x (:: v :int)))
(defn node/vec2-y [v : Vec2Handle] : float (godot-vec2-y (:: v :int)))
(defn node/vec3-x [v : Vec3Handle] : float (godot-vec3-x (:: v :int)))
(defn node/vec3-y [v : Vec3Handle] : float (godot-vec3-y (:: v :int)))
(defn node/vec3-z [v : Vec3Handle] : float (godot-vec3-z (:: v :int)))
(defn node/color-r [c : ColorHandle] : float (godot-color-r (:: c :int)))
(defn node/color-g [c : ColorHandle] : float (godot-color-g (:: c :int)))
(defn node/color-b [c : ColorHandle] : float (godot-color-b (:: c :int)))
(defn node/color-a [c : ColorHandle] : float (godot-color-a (:: c :int)))
(defn node/rect2-x [r : Rect2Handle] : float (godot-rect2-x (:: r :int)))
(defn node/rect2-y [r : Rect2Handle] : float (godot-rect2-y (:: r :int)))
(defn node/rect2-w [r : Rect2Handle] : float (godot-rect2-w (:: r :int)))
(defn node/rect2-h [r : Rect2Handle] : float (godot-rect2-h (:: r :int)))

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
;; T2.A -- typed pos/scale args + typed get-position return. self stays
;; :int so the curated prelude composes with both raw (godot-self) and
;; NodeHandle ascriptions; the generator's typed-self path lives in the
;; per-class facade (node2d/set-position, ...) where the ancestor is
;; CanvasItemHandle / Node2DHandle.
(defn node/set-position [self : int pos : Vec2Handle]
  (godot-call self "set_position" (:: pos :int)))
(defn node/get-position [self : int] : Vec2Handle
  (:: (godot-call self "get_position") :Vec2Handle))
(defn node/set-scale [self : int scale : Vec2Handle]
  (godot-call self "set_scale" (:: scale :int)))

;; --- CanvasItem modulate -- c is an arena color handle ---------------------
(defn node/set-modulate [self : int c : ColorHandle]
  (godot-call self "set_modulate" (:: c :int)))
(defn node/get-modulate [self : int] : ColorHandle
  (:: (godot-call self "get_modulate") :ColorHandle))

;; --- Node traversal --------------------------------------------------------
;; T2.B -- node/get-node returns a NodeHandle (the looked-up node).
;; Comparison-with-null still needs the underlying :int -- ascribe back
;; with (:: h :int) at the call site if you need (= h 0).
(defn node/get-node [self : int path : cstr] : NodeHandle
  (:: (godot-call self "get_node" path) :NodeHandle))
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

;; --- T3.C: typed Array/Dictionary prelude wrappers -----------------------
;; Thin defopaque-fluent shells over the godot-array-* / godot-dict-*
;; natives. The natives themselves take/return raw :int arena handles;
;; these wrappers carry the ArrayHandle / DictHandle defopaque the rest
;; of the generated facade hands you back, so the typical call shape is
;;   (let [d (engine/get-version-info)]            ; d : DictHandle
;;     (godot-println (dict-get-c d "string")))
;; instead of forcing a (:: ... :int) ascription at every read site.

(defn array-new [] : ArrayHandle
  (:: (godot-array-new) :ArrayHandle))
(defn array-size [a : ArrayHandle] : int
  (godot-array-len (:: a :int)))
(defn array-push-i [a : ArrayHandle v : int]   (godot-array-push (:: a :int) v))
(defn array-push-f [a : ArrayHandle v : float] (godot-array-push (:: a :int) v))
(defn array-push-b [a : ArrayHandle v : bool]  (godot-array-push (:: a :int) v))
(defn array-push-c [a : ArrayHandle v : cstr]  (godot-array-push (:: a :int) v))
(defn array-get-i [a : ArrayHandle i : int] : int
  (godot-array-get-i (:: a :int) i))
(defn array-get-f [a : ArrayHandle i : int] : float
  (godot-array-get-f (:: a :int) i))
(defn array-get-b [a : ArrayHandle i : int] : bool
  (godot-array-get-b (:: a :int) i))
(defn array-get-c [a : ArrayHandle i : int] : cstr
  (godot-array-get-c (:: a :int) i))

(defn dict-new [] : DictHandle
  (:: (godot-dict-new) :DictHandle))
(defn dict-set-i [d : DictHandle k : cstr v : int]   (godot-dict-set (:: d :int) k v))
(defn dict-set-f [d : DictHandle k : cstr v : float] (godot-dict-set (:: d :int) k v))
(defn dict-set-b [d : DictHandle k : cstr v : bool]  (godot-dict-set (:: d :int) k v))
(defn dict-set-c [d : DictHandle k : cstr v : cstr]  (godot-dict-set (:: d :int) k v))
(defn dict-get-i [d : DictHandle k : cstr] : int
  (godot-dict-get-i (:: d :int) k))
(defn dict-get-f [d : DictHandle k : cstr] : float
  (godot-dict-get-f (:: d :int) k))
(defn dict-get-b [d : DictHandle k : cstr] : bool
  (godot-dict-get-b (:: d :int) k))
(defn dict-get-c [d : DictHandle k : cstr] : cstr
  (godot-dict-get-c (:: d :int) k))

;; --- T3.D: PackedXxxArray + RID typed wrappers ----------------------------
;; Each Packed family has -new / -push / -get; -size is polymorphic across
;; all nine. The element-type defopaques (Vec2Handle, Vec3Handle, ColorHandle)
;; thread through for the aggregate-element variants.

(defn packed-byte-new    [] : PackedByteHandle    (:: (godot-packed-byte-new)    :PackedByteHandle))
(defn packed-int32-new   [] : PackedInt32Handle   (:: (godot-packed-int32-new)   :PackedInt32Handle))
(defn packed-int64-new   [] : PackedInt64Handle   (:: (godot-packed-int64-new)   :PackedInt64Handle))
(defn packed-float32-new [] : PackedFloat32Handle (:: (godot-packed-float32-new) :PackedFloat32Handle))
(defn packed-float64-new [] : PackedFloat64Handle (:: (godot-packed-float64-new) :PackedFloat64Handle))
(defn packed-string-new  [] : PackedStringHandle  (:: (godot-packed-string-new)  :PackedStringHandle))
(defn packed-vec2-new    [] : PackedVec2Handle    (:: (godot-packed-vec2-new)    :PackedVec2Handle))
(defn packed-vec3-new    [] : PackedVec3Handle    (:: (godot-packed-vec3-new)    :PackedVec3Handle))
(defn packed-color-new   [] : PackedColorHandle   (:: (godot-packed-color-new)   :PackedColorHandle))

(defn packed-byte-size    [h : PackedByteHandle]    : int (godot-packed-size (:: h :int)))
(defn packed-int32-size   [h : PackedInt32Handle]   : int (godot-packed-size (:: h :int)))
(defn packed-int64-size   [h : PackedInt64Handle]   : int (godot-packed-size (:: h :int)))
(defn packed-float32-size [h : PackedFloat32Handle] : int (godot-packed-size (:: h :int)))
(defn packed-float64-size [h : PackedFloat64Handle] : int (godot-packed-size (:: h :int)))
(defn packed-string-size  [h : PackedStringHandle]  : int (godot-packed-size (:: h :int)))
(defn packed-vec2-size    [h : PackedVec2Handle]    : int (godot-packed-size (:: h :int)))
(defn packed-vec3-size    [h : PackedVec3Handle]    : int (godot-packed-size (:: h :int)))
(defn packed-color-size   [h : PackedColorHandle]   : int (godot-packed-size (:: h :int)))

(defn packed-byte-push    [h : PackedByteHandle    v : int]   (godot-packed-byte-push    (:: h :int) v))
(defn packed-int32-push   [h : PackedInt32Handle   v : int]   (godot-packed-int32-push   (:: h :int) v))
(defn packed-int64-push   [h : PackedInt64Handle   v : int]   (godot-packed-int64-push   (:: h :int) v))
(defn packed-float32-push [h : PackedFloat32Handle v : float] (godot-packed-float32-push (:: h :int) v))
(defn packed-float64-push [h : PackedFloat64Handle v : float] (godot-packed-float64-push (:: h :int) v))
(defn packed-string-push  [h : PackedStringHandle  v : cstr]  (godot-packed-string-push  (:: h :int) v))
(defn packed-vec2-push    [h : PackedVec2Handle    v : Vec2Handle]  (godot-packed-vec2-push  (:: h :int) (:: v :int)))
(defn packed-vec3-push    [h : PackedVec3Handle    v : Vec3Handle]  (godot-packed-vec3-push  (:: h :int) (:: v :int)))
(defn packed-color-push   [h : PackedColorHandle   v : ColorHandle] (godot-packed-color-push (:: h :int) (:: v :int)))

(defn packed-byte-get    [h : PackedByteHandle    i : int] : int   (godot-packed-byte-get    (:: h :int) i))
(defn packed-int32-get   [h : PackedInt32Handle   i : int] : int   (godot-packed-int32-get   (:: h :int) i))
(defn packed-int64-get   [h : PackedInt64Handle   i : int] : int   (godot-packed-int64-get   (:: h :int) i))
(defn packed-float32-get [h : PackedFloat32Handle i : int] : float (godot-packed-float32-get (:: h :int) i))
(defn packed-float64-get [h : PackedFloat64Handle i : int] : float (godot-packed-float64-get (:: h :int) i))
(defn packed-string-get  [h : PackedStringHandle  i : int] : cstr  (godot-packed-string-get  (:: h :int) i))
(defn packed-vec2-get    [h : PackedVec2Handle    i : int] : Vec2Handle  (:: (godot-packed-vec2-get  (:: h :int) i) :Vec2Handle))
(defn packed-vec3-get    [h : PackedVec3Handle    i : int] : Vec3Handle  (:: (godot-packed-vec3-get  (:: h :int) i) :Vec3Handle))
(defn packed-color-get   [h : PackedColorHandle   i : int] : ColorHandle (:: (godot-packed-color-get (:: h :int) i) :ColorHandle))

(defn rid-id      [r : RidHandle] : int  (godot-rid-id     (:: r :int)))
(defn rid-valid?  [r : RidHandle] : bool (godot-rid-valid? (:: r :int)))

;; --- T2.C: runtime-checked downcast --------------------------------------
;; is-class? consults Object::is_class via the typed godot-call-b path.
;; Pairs with the generated try-as-<class> helpers (bridge/generated_facade)
;; to give scripts a runtime-checked narrowing without committing to a
;; full Option<T> ergonomic. The generated helpers return the target
;; Handle either populated or as a wrapped-0 sentinel; the standard idiom
;; is `(if (= (:: result :int) 0) ... ...)`.
(defn is-class? [h : int class-name : cstr] : bool
  (godot-call-b h "is_class" class-name))

;; --- G6.3 -- curated one-shot patterns -------------------------------------
;; Patterns every gameplay script reaches for at least once. Pure prelude
;; additions; no new natives. Each entry composes godot-call / godot-call-*
;; or godot-connect-typed.

;; SceneTree access. T2.B -- get-tree returns the SceneTree handle as
;; the typed defopaque the generator already declares; internal callers
;; demote with (:: t :int) at the godot-call boundary.
(defn get-tree [] : SceneTreeHandle
  (:: (godot-call (godot-self) "get_tree") :SceneTreeHandle))
(defn tree/quit [] : void
  (godot-call-v (:: (get-tree) :int) "quit"))
(defn tree/get-root [] : NodeHandle
  (:: (godot-call (:: (get-tree) :int) "get_root") :NodeHandle))
(defn tree/change-scene-to-file [path : cstr] : void
  (godot-call-v (:: (get-tree) :int) "change_scene_to_file" path))

;; Timer one-shot creation -- common enough to deserve a helper. The
;; handler is a closure value type-checked at the call site (G6.1).
;; The returned handle is a SceneTreeTimer, which isn't in the
;; allowlist, so it stays bare :int.
(defn timer/one-shot [seconds : float handler : (fn [] void)] : int
  (let [t (godot-call (:: (get-tree) :int) "create_timer" seconds)]
    (godot-connect-typed t "timeout" handler)
    t))

;; Engine.
(defn engine [] : int (godot-singleton "Engine"))
(defn engine/get-frames-drawn [] : int
  (godot-call (engine) "get_frames_drawn"))
(defn engine/get-process-fps [] : float
  (godot-call-f (engine) "get_frames_per_second"))

;; OS (Time provides the ticks; OS lacks a millisecond clock in 4.3).
(defn os [] : int (godot-singleton "OS"))
(defn time [] : int (godot-singleton "Time"))
(defn os/get-system-time-msecs [] : int
  (godot-call (time) "get_ticks_msec"))

;; Logging that isn't just godot-println. Both route through godot-println
;; with a tag prefix for v1; a real (push_warning / push_error) path can
;; replace these once UtilityFunctions exposure lands as a native.
(defn log/info [msg : cstr] : void
  (godot-println msg))
(defn log/error [msg : cstr] : void
  (godot-println msg))

;; Compose timer/one-shot for the deferred-call pattern. The timer
;; handle is the typed callee target only; the script side only cares
;; about the closure firing once `seconds` later, so godot-connect-typed
;; (TUR_NRT_VOID) is the tail expression.
(defn after [seconds : float handler : (fn [] void)] : void
  (godot-connect-typed
    (godot-call (:: (get-tree) :int) "create_timer" seconds)
    "timeout"
    handler))

;; --- T4.D: preload (compile-time-validated resource load) ---------------
;;
;; (preload "res://things/sword.tscn") returns a ResourceHandle pointing
;; at the loaded resource. The actual file existence check happens inside
;; (godot-preload ...) at native-call time -- which, for a top-level
;; (preload ...) form, IS the script's load-time (TurmericScript::_reload
;; evaluates the source on every reload). A typo'd path fails fast at
;; reload with `preload: missing resource '...'` before any gameplay runs.
;;
;; The loaded resource is cached by path for the process lifetime, so a
;; defn that calls (preload ...) repeatedly only loads on the first call;
;; later calls return the cached handle without disk IO.
;;
;; The return type is ResourceHandle -- the abstract Resource. Down-cast
;; with `(:: (preload ...) :PackedSceneHandle)` etc. at the use site if
;; the concrete subclass matters; both PackedScene and Image are
;; allowlisted so their Handle defopaques exist.
(defn preload [path : cstr] : ResourceHandle
  (:: (godot-preload path) :ResourceHandle))

;; --- T4.A starter: cross-script calls via Godot Callable -----------------
;;
;; Each .tur script AOT-compiles to its own dlopen'd shared library, so
;; one script's defns are NOT directly callable from another at the C
;; ABI level. Until the Tier 4 build-graph (T4.A approach 2) lands,
;; cross-script calls go through Godot's Callable system -- the same
;; round-trip GDScript pays per inter-script call. The substrate is
;; already in place (godot-call routes through Object::callv); these
;; wrappers just name it as "the cross-script path" so it's discoverable
;; instead of buried in a generic call.
;;
;;   (cross-call other-node "do-thing" arg1 arg2 ...)
;;     -- variadic-positional flavour; up to 4 fixed args; uses
;;        godot-call directly so primitives stay primitive and arena
;;        handles stay arena handles.
;;   (cross-call-pack other-node "do-thing" extras-array)
;;     -- arbitrary-arity flavour; pass a (array-new ...) extras
;;        handle for any arg count. Same machinery the T3.E generated
;;        vararg wrappers use.
;;
;; Note: dispatch on the other script's name uses the kebab-case
;; turmeric defn name -- e.g. a (defn do-thing [x] ...) in Enemy.tur
;; is reached as (cross-call enemy-node "do-thing" ...). The bridge's
;; cb_call normalises the underscore/dash spelling.
(defn cross-call [other : int method : cstr] : int
  (godot-call other method))
(defn cross-call-pack [other : int method : cstr extras : ArrayHandle] : int
  (godot-call-pack other method (:: extras :int)))

;; --- G7 -- defgodot-script ergonomic shell --------------------------------
;; Plan-shape surface (see docs/upcoming/v1/godot-language-binding-plan.md
;; in the turmeric repo for the wider design):
;;
;;   (defgodot-script Player :extends Node2D
;;     :exports ((speed   : float 200.0)
;;               (counter : int   7))
;;     :signals (pinged
;;               (hit (damage : int) (impulse : float))
;;               died)
;;     (defn _ready [] ...))
;;
;; ClassName is informational; Godot's base-class typing comes from
;; set_script() being attached to a node of the right type. :extends
;; is accepted and ignored for v1 -- it documents intent without yet
;; constraining the attachment.
;;
;; defgodot-script walks its body in order. Each form is one of:
;;   - the keyword :extends followed by a parent-class symbol (ignored)
;;   - the keyword :exports followed by a list of (name : type default)
;;     decls -- each lowered to (godot-export "name" "type" default)
;;   - the keyword :signals followed by a list of signal decls -- each
;;     a bare symbol (zero-arg) or (name (arg : type) ...) (flat-arg).
;;     Each lowered to (godot-signal "name" "arg1" "type1" ...)
;;   - any other form passes through unchanged.
;;
;; Per-decl call shapes (defgodot-export / defgodot-signal) are kept
;; available too, for users who want to spell things out without the
;; block surface.
;;
;; Backed by type-ann-inner / type-ann? CT primitives and the
;; ^syntax param marker; see docs/archive/ in the turmeric repo.

;; --- Per-decl exports (variadic over (name : type default)) ---------------
(defmacro defgodot-export [& ^syntax decls]
  (if (empty? decls)
    `(do)
    (let [d        (first decls)
          name-sym (first d)
          type-ann (first (rest d))
          type-sym (type-ann-inner type-ann)
          default  (first (rest (rest d)))
          name-str (symbol-name name-sym)
          type-str (symbol-name type-sym)]
      `(do
         (godot-export ~name-str ~type-str ~default)
         (defgodot-export ~@(rest decls))))))

;; --- Per-decl signals (variadic over bare names or (name (arg : type)...))-
;; tg-signal-acc__ folds a signal's (arg : type) decls into the godot-signal
;; native's flat string-pair-after-name signature, using an accumulator
;; threaded through self-re-expansion. The ~@acc splice carries the
;; growing arg-list across passes.
(defmacro tg-signal-acc__ [name-str ^syntax acc & ^syntax arg-decls]
  (if (empty? arg-decls)
    `(godot-signal ~name-str ~@acc)
    (let [d        (first arg-decls)
          arg-name (symbol-name (first d))
          arg-type (symbol-name (type-ann-inner (first (rest d))))]
      `(tg-signal-acc__ ~name-str (~@acc ~arg-name ~arg-type)
                        ~@(rest arg-decls)))))

(defmacro tg-signal-one__ [^syntax decl]
  (let [name-str (symbol-name (first decl))]
    `(tg-signal-acc__ ~name-str () ~@(rest decl))))

(defmacro defgodot-signal [& ^syntax decls]
  (if (empty? decls)
    `(do)
    (let [d (first decls)]
      (if (list? d)
        `(do (tg-signal-one__ ~d)
             (defgodot-signal ~@(rest decls)))
        (let [name-str (symbol-name d)]
          `(do (godot-signal ~name-str)
               (defgodot-signal ~@(rest decls))))))))

;; --- Body walker for the block surface ------------------------------------
;; tg-script-walk__ scans the body in order, accumulating a list of
;; forms to emit. Keyword markers (:extends, :exports, :signals)
;; consume the following form as their payload; everything else
;; passes through. The accumulator is spliced via ~@emits across
;; self-re-expansions until the body is empty.
(defmacro tg-script-walk__ [^syntax emits & ^syntax body]
  (if (empty? body)
    `(do ~@emits)
    (let [f (first body)]
      (if (= f :extends)
        `(tg-script-walk__ (~@emits) ~@(rest (rest body)))
        (if (= f :exports)
          `(tg-script-walk__
              (~@emits (defgodot-export ~@(first (rest body))))
              ~@(rest (rest body)))
          (if (= f :signals)
            `(tg-script-walk__
                (~@emits (defgodot-signal ~@(first (rest body))))
                ~@(rest (rest body)))
            `(tg-script-walk__ (~@emits ~f) ~@(rest body))))))))

(defmacro defgodot-script [^syntax _name & ^syntax body]
  `(tg-script-walk__ () ~@body))
)TURMERIC";

} // namespace godot
