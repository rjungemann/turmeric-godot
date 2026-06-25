@tool
extends SceneTree

# G1 follow-up: prove that _ready fires when a Node with a TurmericScript
# attached enters the scene tree.

func _init() -> void:
	print("[driver] building TurmericScript ...")
	var script = ClassDB.instantiate("TurmericScript")
	if script == null:
		printerr("[driver] ClassDB.instantiate returned null")
		quit()
		return

	script.source_code = """
(defn _ready []
  (godot-println "[turmeric] _ready fired from inside a Node"))

(defn greet [name : cstr]
  (godot-println name))
"""
	var err: int = script.reload()
	print("[driver] reload returned: ", err)
	print("[driver] script.can_instantiate() = ", script.can_instantiate())

	var node := Node.new()
	node.set_script(script)
	print("[driver] node.get_script() = ", node.get_script())

	# Adding to the SceneTree root triggers NOTIFICATION_READY on the next
	# idle frame. Process one frame then quit.
	root.add_child(node)
	print("[driver] node added to tree; waiting one frame for _ready ...")
	await process_frame
	print("[driver] frame processed; explicitly calling greet(...) ...")
	node.call("greet", "[turmeric] hello from a String arg")
	print("[driver] explicit call returned; tearing down")
	node.queue_free()
	quit()
