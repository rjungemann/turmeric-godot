@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/oneshot_prelude.tur")
	if s == null:
		printerr("[oneshot_prelude.gd] FAIL: load returned null"); quit(1); return

	var n := Node.new()
	n.name = "Subject"
	n.set_script(s)
	root.add_child(n)

	# Wait long enough for the 50ms SceneTreeTimer to fire and dispatch.
	for _i in range(20):
		await process_frame

	if n.name != "after-fired":
		printerr("[oneshot_prelude.gd] FAIL: expected Subject renamed to 'after-fired', got '%s'" % str(n.name))
		n.queue_free(); quit(1); return

	print("[oneshot_prelude.gd] PASS: (timer/one-shot) closure fired via (get-tree).create_timer")
	n.queue_free(); quit(0)
