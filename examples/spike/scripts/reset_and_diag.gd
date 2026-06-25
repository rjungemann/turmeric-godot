@tool
extends SceneTree

# Exercises two of the per-embed-env peripherals:
#   - Gap 2: turi_env_reset clears defns between reloads, keeps natives.
#   - Gap 3: diag_sink routes parse errors through Godot's printerr with
#     the script's path attributed in the prefix.

func _init() -> void:
	# --- Reset semantics --------------------------------------------------
	var script = ClassDB.instantiate("TurmericScript")
	script.set_path("res://scripts/reset_demo.tur")  # for diag attribution

	script.source_code = '(defn greet [] (godot-println "[A] first version"))'
	script.reload()
	var node := Node.new()
	node.set_script(script)
	root.add_child(node)
	await process_frame
	node.call("greet")

	# Reload with new source. Old greet should be gone; new greet wins.
	script.source_code = '(defn greet [] (godot-println "[B] second version"))'
	script.reload()
	node.call("greet")

	# --- Diag sink --------------------------------------------------------
	var bad = ClassDB.instantiate("TurmericScript")
	bad.set_path("res://scripts/broken.tur")
	# Unclosed paren -- a parse error that has to surface during elaboration.
	bad.source_code = "(defn _ready []\n  (godot-println \"oops\""
	var err: int = bad.reload()
	print("[driver] bad.reload returned err=", err, " (expected nonzero)")

	node.queue_free()
	quit()
