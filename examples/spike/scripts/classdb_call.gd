@tool
extends SceneTree

# G3.a — drives classdb_call.tur and asserts the round-trip values landed.
# Mirrors signals.gd: attach the script to a Node2D, wait for _ready,
# then check rotation / position / get_name flowed through (godot-call ...).

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[classdb_call.gd] FAIL: ", msg)

func _approx_eq(a: float, b: float, eps: float = 1e-4) -> bool:
	return abs(a - b) <= eps

func _init() -> void:
	var script := load("res://scripts/classdb_call.tur")
	if script == null:
		_fail("load returned null"); quit(); return

	var node := Node2D.new()
	node.name = "RoundTripNode"
	node.set_script(script)
	root.add_child(node)

	await process_frame

	# (godot-call self "set_rotation" 0.5)
	if not _approx_eq(node.rotation, 0.5):
		_fail("rotation: expected ~0.5, got %s" % str(node.rotation))

	# (godot-call self "set_position" (godot-vec2 100.0 50.0))
	if not (_approx_eq(node.position.x, 100.0) and _approx_eq(node.position.y, 50.0)):
		_fail("position: expected (100, 50), got %s" % str(node.position))

	# (godot-call self "set_modulate" (godot-color 0.5 0.25 0.75 1.0))
	if not (_approx_eq(node.modulate.r, 0.5) and _approx_eq(node.modulate.g, 0.25)
			and _approx_eq(node.modulate.b, 0.75) and _approx_eq(node.modulate.a, 1.0)):
		_fail("modulate: expected (0.5, 0.25, 0.75, 1.0), got %s" % str(node.modulate))

	# get_name round-trip: the script prints the name via (godot-println).
	# We just confirm the node was reachable; the printed output is captured
	# in the test log.

	if fail_count == 0:
		print("[classdb_call.gd] all assertions passed: rotation=%s position=%s" %
			[str(node.rotation), str(node.position)])
	else:
		printerr("[classdb_call.gd] %d FAILURES" % fail_count)

	node.queue_free()
	quit(0 if fail_count == 0 else 1)
