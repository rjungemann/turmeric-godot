@tool
extends SceneTree

# G2: now that TurmericResourceLoader is registered, load() should
# round-trip through the resource pipeline and hand us a TurmericScript.

func _init() -> void:
	print("[driver] loading res://scripts/hello.tur ...")
	var script = load("res://scripts/hello.tur")
	if script == null:
		printerr("[driver] load returned null")
		quit()
		return
	print("[driver] loaded: ", script)

	# Attach to a node and let _ready fire.
	var node := Node.new()
	node.set_script(script)
	root.add_child(node)
	await process_frame

	# Exercise the typed method from a string roundtrip.
	var sum: int = node.call("add", 17, 25)
	print("[driver] node.call(add, 17, 25) = ", sum)

	node.queue_free()
	quit()
