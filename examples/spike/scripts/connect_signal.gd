@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/connect_signal.tur")
	if s == null:
		printerr("[connect.gd] FAIL: load returned null"); quit(1); return

	# Sibling timer fires after 50ms.
	var timer := Timer.new()
	timer.name = "Beat"
	timer.wait_time = 0.05
	timer.one_shot = true
	timer.autostart = true
	root.add_child(timer)

	# Subscriber attaches `on-tick` to Beat.timeout in _ready.
	var sub := Node.new()
	sub.name = "Subscriber"
	sub.set_script(s)
	root.add_child(sub)

	# Wait ~10 frames -- 60fps means ~166ms simulated, plenty for the
	# 50ms timer.
	for _i in range(15):
		await process_frame

	if sub.name != "tick-received":
		printerr("[connect.gd] FAIL: expected Subscriber renamed to 'tick-received', got '%s'" % str(sub.name))
		sub.queue_free(); timer.queue_free(); quit(1); return

	print("[connect.gd] all assertions passed; on-tick fired via godot-connect")
	sub.queue_free(); timer.queue_free(); quit(0)
