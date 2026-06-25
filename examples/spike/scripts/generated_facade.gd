@tool
extends SceneTree

# G3.c -- drives generated_facade.tur and asserts the codegen'd wrappers
# round-trip through to the engine.

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[generated_facade.gd] FAIL: ", msg)

func _approx_eq(a: float, b: float, eps: float = 1e-4) -> bool:
	return abs(a - b) <= eps

func _init() -> void:
	var script := load("res://scripts/generated_facade.tur")
	if script == null:
		_fail("load returned null"); quit(); return

	var node := Node2D.new()
	node.set_script(script)
	root.add_child(node)

	await process_frame

	if not (_approx_eq(node.position.x, 10.0) and _approx_eq(node.position.y, 20.0)):
		_fail("position: expected (10, 20), got %s" % str(node.position))
	if not _approx_eq(node.skew, 0.1):
		_fail("skew: expected ~0.1, got %s" % str(node.skew))
	if not (_approx_eq(node.scale.x, 2.0) and _approx_eq(node.scale.y, 0.5)):
		_fail("scale: expected (2, 0.5), got %s" % str(node.scale))

	if fail_count == 0:
		print("[generated_facade.gd] all assertions passed: pos=%s skew=%s scale=%s" %
			[str(node.position), str(node.skew), str(node.scale)])
	else:
		printerr("[generated_facade.gd] %d FAILURES" % fail_count)

	node.queue_free()
	quit(0 if fail_count == 0 else 1)
