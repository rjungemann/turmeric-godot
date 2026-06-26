@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/defgodot_script.tur")
	if s == null:
		printerr("[defgodot_script.gd] FAIL: load returned null"); quit(1); return

	var n := Node.new()
	n.name = "Subject"
	n.set_script(s)
	root.add_child(n)

	for _i in range(3):
		await process_frame

	if n.name != "ok-from-export":
		printerr("[defgodot_script.gd] FAIL: expected rename to 'ok-from-export', got '%s'" % str(n.name))
		n.queue_free(); quit(1); return

	# Confirm the export shows the script-side default via the property API.
	if n.get("counter") != 7:
		printerr("[defgodot_script.gd] FAIL: expected counter=7, got %s" % str(n.get("counter")))
		n.queue_free(); quit(1); return

	print("[defgodot_script.gd] PASS: defgodot-script body, :exports, :signals all wired")
	n.queue_free(); quit(0)
