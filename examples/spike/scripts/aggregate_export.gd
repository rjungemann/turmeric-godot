@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/aggregate_export.tur")
	if s == null:
		printerr("[agg-export.gd] FAIL: load returned null"); quit(1); return
	var n := Node2D.new(); n.set_script(s); root.add_child(n)
	await process_frame
	if not (abs(n.position.x - 5.0) < 0.01 and abs(n.position.y - 7.0) < 0.01):
		printerr("[agg-export.gd] FAIL: position expected (5, 7) got %s" % str(n.position))
		n.queue_free(); quit(1); return
	if not (abs(n.modulate.r - 0.1) < 0.01 and abs(n.modulate.g - 0.2) < 0.01):
		printerr("[agg-export.gd] FAIL: modulate expected (0.1, 0.2, ...) got %s" % str(n.modulate))
		n.queue_free(); quit(1); return
	print("[agg-export.gd] all assertions passed; pos=%s modulate=%s" % [str(n.position), str(n.modulate)])
	n.queue_free(); quit(0)
