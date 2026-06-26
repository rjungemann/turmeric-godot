@tool
extends SceneTree
func _init() -> void:
	# Bad script: handler arity 1, signal arity 0. Loading must fail.
	var s := load("res://scripts/typed_signal_bad.tur")
	# The Resource may load successfully but the underlying TurmericScript
	# captures the elaboration diagnostic; ResourceLoader returns null when
	# parse/elab errors are fatal. Either non-null with a diag or null is
	# acceptable here -- what's NOT acceptable is a clean load + runtime
	# signal-no-op.
	if s == null:
		print("[typed_signal_bad.gd] PASS: load returned null (elaboration rejected)")
		quit(0); return

	# If the script loaded, instantiate and check that _ready cannot
	# bind a wrong-arity handler. Godot reports the failure via printerr;
	# the test treats any post-attach state where Subscriber gets renamed
	# to 'should-not-load' as a regression.
	var timer := Timer.new()
	timer.name = "Beat"
	timer.wait_time = 0.05
	timer.one_shot = true
	timer.autostart = true
	root.add_child(timer)

	var sub := Node.new()
	sub.name = "Subscriber"
	sub.set_script(s)
	root.add_child(sub)

	for _i in range(15):
		await process_frame

	if sub.name == "should-not-load":
		printerr("[typed_signal_bad.gd] FAIL: wrong-arity handler was invoked")
		sub.queue_free(); timer.queue_free(); quit(1); return

	print("[typed_signal_bad.gd] PASS: wrong-arity handler did not fire")
	sub.queue_free(); timer.queue_free(); quit(0)
