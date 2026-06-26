@tool
extends SceneTree
func _init() -> void:
	var s := load("res://scripts/defgodot_script_block.tur")
	if s == null:
		printerr("[defgodot_script_block.gd] FAIL: load returned null"); quit(1); return

	var n := Node.new()
	n.name = "Subject"
	n.set_script(s)
	root.add_child(n)

	for _i in range(3):
		await process_frame

	if n.name != "ok-from-export":
		printerr("[defgodot_script_block.gd] FAIL: expected rename to 'ok-from-export', got '%s'" % str(n.name))
		n.queue_free(); quit(1); return

	if n.get("counter") != 7:
		printerr("[defgodot_script_block.gd] FAIL: expected counter=7, got %s" % str(n.get("counter")))
		n.queue_free(); quit(1); return

	# Confirm the multi-arg signal `hit` registered with its declared args.
	var sigs: Array = n.get_signal_list()
	var saw_hit := false
	for sig in sigs:
		if sig.name == "hit":
			saw_hit = true
			if sig.args.size() != 2:
				printerr("[defgodot_script_block.gd] FAIL: hit expected 2 args, got %d" % sig.args.size())
				n.queue_free(); quit(1); return
			if sig.args[0].name != "damage" or sig.args[1].name != "impulse":
				printerr("[defgodot_script_block.gd] FAIL: hit arg names wrong: %s" % str(sig.args))
				n.queue_free(); quit(1); return
	if not saw_hit:
		printerr("[defgodot_script_block.gd] FAIL: 'hit' signal not declared")
		n.queue_free(); quit(1); return

	print("[defgodot_script_block.gd] PASS: block-surface :exports / :signals / pass-through all wired")
	n.queue_free(); quit(0)
