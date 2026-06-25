@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/string_export.tur")
	if s == null:
		printerr("[str-export.gd] FAIL: load returned null"); quit(1); return
	var n := Node2D.new(); n.set_script(s)
	n.set("label-prefix", "hello-from-inspector")
	root.add_child(n)
	await process_frame
	if n.name != "hello-from-inspector":
		printerr("[str-export.gd] FAIL: name expected 'hello-from-inspector', got '%s'" % str(n.name))
		n.queue_free(); quit(1); return
	print("[str-export.gd] all assertions passed; name=%s" % str(n.name))
	n.queue_free(); quit(0)
