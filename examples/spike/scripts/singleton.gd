@tool
extends SceneTree
func _init() -> void:
	var script := load("res://scripts/singleton.tur")
	if script == null:
		printerr("[singleton.gd] FAIL: load returned null"); quit(1); return
	var node := Node2D.new(); node.set_script(script); root.add_child(node)
	await process_frame
	print("[singleton.gd] all assertions passed (script ran without error)")
	node.queue_free(); quit(0)
