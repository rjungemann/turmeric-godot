@tool
extends SceneTree

# Per-script env test: two scripts both define `_ready` with different
# bodies. With a shared env they'd collide and one would win; with
# per-script envs both should fire when their respective nodes enter
# the tree.

func _init() -> void:
	var script_a = ClassDB.instantiate("TurmericScript")
	script_a.source_code = """
(defn _ready []
  (godot-println "[script A] _ready"))
"""
	var err_a: int = script_a.reload()
	print("[driver] script_a.reload = ", err_a)

	var script_b = ClassDB.instantiate("TurmericScript")
	script_b.source_code = """
(defn _ready []
  (godot-println "[script B] _ready"))
"""
	var err_b: int = script_b.reload()
	print("[driver] script_b.reload = ", err_b)

	var node_a := Node.new()
	node_a.set_script(script_a)
	var node_b := Node.new()
	node_b.set_script(script_b)

	root.add_child(node_a)
	root.add_child(node_b)
	print("[driver] both nodes added; waiting one frame ...")
	await process_frame

	# Explicit calls -- prove each script's _ready resolves to its own body.
	print("[driver] calling node_a._ready() explicitly:")
	node_a.call("_ready")
	print("[driver] calling node_b._ready() explicitly:")
	node_b.call("_ready")

	node_a.queue_free()
	node_b.queue_free()
	quit()
