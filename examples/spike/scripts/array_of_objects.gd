@tool
extends SceneTree
# Smoke test that Array of Object handles round-trips through
# godot-array-get. The parent node has three named children; the
# script reads the first child and renames the parent to that child's
# name.

var fail_count := 0
func _fail(m: String) -> void: fail_count += 1; printerr("[array-of-objects.gd] FAIL: ", m)

func _init() -> void:
	var script := load("res://scripts/array_of_objects.tur")
	if script == null:
		_fail("load returned null"); quit(1); return

	var parent := Node.new()
	parent.name = "Parent"
	parent.set_script(script)
	root.add_child(parent)

	# Three children with distinct names. Script reads kids[0] and
	# renames the parent to its name.
	for name in ["Alpha", "Beta", "Gamma"]:
		var c := Node.new(); c.name = name
		parent.add_child(c)

	await process_frame

	if parent.name != "Alpha":
		_fail("expected parent renamed to 'Alpha' (kids[0]); got '%s'" % str(parent.name))

	if fail_count == 0:
		print("[array-of-objects.gd] all assertions passed (Array<Object> -> handle -> godot-call works)")
	else:
		printerr("[array-of-objects.gd] %d FAILURES" % fail_count)

	parent.queue_free()
	quit(0 if fail_count == 0 else 1)
