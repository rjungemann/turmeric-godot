@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/typed_handles.tur")
	if s == null:
		printerr("[typed-handles.gd] FAIL: load returned null"); quit(1); return
	var n := Node2D.new(); n.set_script(s); root.add_child(n)
	await process_frame
	if abs(n.rotation - 0.75) > 0.01:
		printerr("[typed-handles.gd] FAIL: rotation expected 0.75 got %s" % str(n.rotation))
		n.queue_free(); quit(1); return
	print("[typed-handles.gd] all assertions passed; rotation=%s" % str(n.rotation))
	n.queue_free(); quit(0)
