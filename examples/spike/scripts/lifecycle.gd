@tool
extends SceneTree

# G2 probe: do the rest of Godot's lifecycle hooks reach Turmeric without
# any cb_call changes? cb_call routes by method name and we already marshal
# floats, so _process(dt) should "just work."

var node: Node = null
var tick: int = 0

func _init() -> void:
	var script = ClassDB.instantiate("TurmericScript")
	script.set_path("res://scripts/lifecycle_demo.tur")
	script.source_code = """
(defn _ready []
  (godot-println "[lifecycle] _ready"))

(defn _process [dt : float]
  (godot-println "[lifecycle] _process tick"))
"""
	var err: int = script.reload()
	print("[driver] reload returned: ", err)

	node = Node.new()
	node.set_script(script)
	root.add_child(node)

	# Process a handful of frames; _process should fire on each.
	for i in range(3):
		await process_frame
		tick += 1
		print("[driver] frame ", tick, " done")

	node.queue_free()
	quit()
