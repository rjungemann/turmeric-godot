@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/handle_hierarchy.tur")
	if s == null:
		printerr("[handle_hierarchy.gd] FAIL: load returned null"); quit(1); return

	# Attach to a Node2D so (godot-self) is reachable via Node2DHandle and
	# the up-coercion chain through CanvasItem -> Node lands cleanly.
	var n := Node2D.new()
	n.name = "Subject"
	n.set_script(s)
	root.add_child(n)

	# _ready dispatch is deferred to the next frame in SceneTree --script
	# mode; wait for it before checking the rename.
	for _i in range(3):
		await process_frame

	if n.name != "handle-hierarchy-ok":
		printerr("[handle_hierarchy.gd] FAIL: expected 'handle-hierarchy-ok', got '%s'" % str(n.name))
		n.queue_free(); quit(1); return

	print("[handle_hierarchy.gd] PASS: generated up-coercion chain composed via NodeHandle")
	n.queue_free(); quit(0)
