@tool
extends SceneTree

# G3.a follow-up driver -- attaches aggregates.tur to a Node2D and asserts
# the position / rotation round-trip (which exercises Transform2D and
# Array on the script side via get_transform / get_children).

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[aggregates.gd] FAIL: ", msg)

func _approx_eq(a: float, b: float, eps: float = 1e-4) -> bool:
	return abs(a - b) <= eps

func _init() -> void:
	var script := load("res://scripts/aggregates.tur")
	if script == null:
		_fail("load returned null"); quit(); return

	var node := Node2D.new()
	node.name = "AggNode"
	node.set_script(script)
	root.add_child(node)

	await process_frame

	if not (_approx_eq(node.position.x, 42.0) and _approx_eq(node.position.y, 17.0)):
		_fail("position: expected (42, 17), got %s" % str(node.position))
	if not _approx_eq(node.rotation, 0.25):
		_fail("rotation: expected ~0.25, got %s" % str(node.rotation))
	if node.get_child_count() != 0:
		_fail("expected 0 children, got %d" % node.get_child_count())

	if fail_count == 0:
		print("[aggregates.gd] all assertions passed")
	else:
		printerr("[aggregates.gd] %d FAILURES" % fail_count)

	node.queue_free()
	quit(0 if fail_count == 0 else 1)
